# 锁表哈希表升级 设计文档

> 版本: 0.1.0 | 日期: 2026-06-14 | 状态: 草案
>
> 关联: ROADMAP.md Iteration 1.7.2

---

## 1. 背景与动机

### 1.1 现状

当前锁表 (`lock_table.c`) 使用**单链表**管理节点 (`treelock_node_t`)，节点查找为 O(n) 线性扫描：

```c
// 当前实现 — O(n) 链表遍历
treelock_node_t *treelock_table_find(treelock_t *tl, treelock_node_id_t node_id) {
    treelock_node_t *node = tl->lock_table;
    while (node != NULL) {
        if (node->node_id == node_id) return node;
        node = node->next;
    }
    return NULL;
}
```

### 1.2 问题

| 问题 | 影响 |
|------|------|
| 大量活跃节点时查找退化 | 1 万节点 → 每次 lock/unlock 平均 5000 次比较 |
| 锁表全局锁持有时间过长 | `table_mutex` 锁内做 O(n) 扫描 |
| 为分布式阶段做准备 | Phase 2/3 服务端可能管理数十万节点 |

### 1.3 目标

将节点查找从 **O(n) 链表 → O(1) 哈希表**，使用 `uthash` (header-only, MIT 许可)。

---

## 2. 技术方案

### 2.1 选型: uthash

| 维度 | 评价 |
|------|------|
| 许可 | MIT — 兼容项目 |
| 依赖 | 零 — 单头文件 (`uthash.h`) |
| 接口 | 宏 API — `HASH_FIND`, `HASH_ADD_KEYPTR`, `HASH_DEL`, `HASH_ITER` |
| 性能 | O(1) 平均，O(n) 最坏 (哈希冲突链) |
| 内存 | 每节点额外 ~40 字节 (`UT_hash_handle` 内部有 prev/next/hash/等) |

### 2.2 数据结构变更

#### `treelock_node_t` 结构体 (internal.h)

```c
// 变更前
typedef struct treelock_node_s {
    treelock_node_id_t      node_id;
    treelock_grant_t       *grants;
    UINT_64                 grant_count;
    UINT_64                 grant_capacity;
    treelock_wait_entry_t  *wait_queue;
    UINT_64                 wait_count;
    UINT_64                 wait_capacity;
    pthread_mutex_t         mutex;
    struct treelock_node_s *next;            // 链表指针
} treelock_node_t;

// 变更后
typedef struct treelock_node_s {
    treelock_node_id_t      node_id;         // 哈希键
    treelock_grant_t       *grants;
    UINT_64                 grant_count;
    UINT_64                 grant_capacity;
    treelock_wait_entry_t  *wait_queue;
    UINT_64                 wait_count;
    UINT_64                 wait_capacity;
    pthread_mutex_t         mutex;
    struct treelock_node_s *next;            // 保留 (uthash 的 HASH_ITER 遍历用)
    UT_hash_handle          hh;              // 新增: uthash 哈希句柄
} treelock_node_t;
```

关键决策：
- **保留 `next` 指针**：不用作哈希链（uthash 内部管理），改作 `HASH_ITER` 宏的遍历辅助字段。`treelock_destroy` 中遍历锁表清理时，uthash 的 `HASH_ITER` 正是通过 `hh.next` 遍历，但我们保留 `next` 用于 `HASH_ITER` 的临时指针语义（uthash 的 `HASH_ITER(hh, head, elt, tmp)` 需要 `elt` 和 `tmp` 两个同类型指针，不依赖自定义 `next`）。
- **实际变更**：`next` 字段不再被代码使用（uthash 用自己的 `hh.next`/`hh.prev`），可从结构体中删除以省内存。**保守策略：先保留 `next`，所有测试通过后再做清理**。

### 2.3 接口变更清单

| 函数 | 变更 | 说明 |
|------|------|------|
| `treelock_table_find` | 链表遍历 → `HASH_FIND` | 核心变更，O(1) |
| `treelock_table_get_or_create` | 链表头插 → `HASH_ADD_KEYPTR` | 新节点加入哈希表 |
| `treelock_destroy` (client.c) | `node = node->next` → `HASH_ITER` | 销毁遍历 |
| 其他函数 | 无变更 | 仅通过 find / get_or_create 间接使用 |

### 2.4 哈希键

- **键**：`treelock_node_id_t`（即 `uint64_t`）
- **键大小**：`sizeof(treelock_node_id_t)` = 8 字节
- **uthash API**：`HASH_FIND(hh, head, keyptr, keylen, out)` + `HASH_ADD_KEYPTR(hh, head, keyptr, keylen, item)`

### 2.5 并发安全

- 所有锁表操作已由 `tl->table_mutex` 保护
- uthash 本身不是线程安全的，但在 mutex 保护下安全
- 与链表方案相比**无新增并并发问题**

### 2.6 内存影响

| 项目 | 链表 | 哈希表 | 差异 |
|------|------|--------|------|
| 每节点 sizeof | ~120B | ~160B | +~40B (UT_hash_handle) |
| 哈希桶开销 | 0 | ~4KB (256桶初始) | uthash 动态扩展 |
| 10K 节点总开销 | ~1.2MB | ~1.6MB | 可接受 |

---

## 3. 实现步骤

```
步骤 1: 添加 uthash.h 到项目 (modules/uthash/uthash.h)
步骤 2: 修改 internal.h — treelock_node_t 添加 UT_hash_handle hh
步骤 3: 修改 lock_table.c — treelock_table_find (HASH_FIND)
步骤 4: 修改 lock_table.c — treelock_table_get_or_create (HASH_ADD_KEYPTR)
步骤 5: 修改 client.c — treelock_destroy 遍历 (HASH_ITER)
步骤 6: 修改 CMakeLists.txt — 添加 uthash include 路径
步骤 7: 编译验证
步骤 8: 运行全量测试套件
步骤 9: 性能基准对比
```

---

## 4. 风险评估

| 风险 | 概率 | 缓解 |
|------|------|------|
| uthash 哈希冲突退化 | 低 | uint64_t 分布均匀，uthash 自动扩容 |
| 编译警告 | 中 | uthash 宏某些路径可能触发 -Wconversion，需验证 |
| 现有测试失败 | 低 | 功能等价变换，所有接口语义不变 |
| destroy 遍历遗漏节点 | 低 | HASH_ITER 保证遍历所有元素 |
| 树模块兼容性 | 低 | tree_core.c 不直接访问锁表链表，无影响 |

---

## 5. 验收标准

- [ ] 所有现有 113 个测试用例零回归
- [ ] `treelock_table_find` 时间复杂度 O(1)（1 万节点下查找时间恒定）
- [ ] `treelock_destroy` 正确释放所有节点资源（valgrind 0 leak）
- [ ] 编译零警告（`-Wall -Wextra -Wshadow -Wconversion`）
- [ ] ROADMAP.md 中 Iteration 1.7.2 标记为完成

---

## 6. 文件清单

| 文件 | 操作 | 说明 |
|------|------|------|
| `modules/uthash/uthash.h` | 新增 | uthash v2.3.0 头文件 |
| `modules/treelock_core/src/internal.h` | 修改 | 添加 `UT_hash_handle hh` 字段 |
| `modules/treelock_core/src/lock_table.c` | 修改 | find/get_or_create 使用 uthash |
| `modules/treelock_core/src/client.c` | 修改 | destroy 遍历使用 HASH_ITER |
| `modules/treelock_core/CMakeLists.txt` | 修改 | 添加 uthash include 路径 |
| `tests/test_concurrent.cc` | 修改 | 新增哈希表专项测试 |
| `docs/ROADMAP.md` | 修改 | 标记 1.7.2 完成 |

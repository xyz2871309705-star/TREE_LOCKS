# 锁表哈希表升级 — 测试策略

> 版本: 0.1.0 | 日期: 2026-06-14
>
> 关联: docs/design/hash-table-upgrade.md

---

## 1. 测试目标

验证锁表从单链表迁移到 uthash 哈希表后：

1. **功能等价** — 所有现有 API 行为不变
2. **O(1) 查找** — 大量节点下查找时间为常数级
3. **遍历完整性** — destroy 不遗漏任何节点
4. **并发安全** — 哈希表在现有互斥锁保护下正常工作
5. **零泄漏** — valgrind/ASan 零泄漏

---

## 2. 测试层次

```
Level 1: 单元测试 — 直接测试 lock_table.c 函数
          (find / get_or_create / grant / release / wake_waiters)

Level 2: 集成测试 — 通过 client.c 公共 API 间接测试
          (lock / unlock / escalate / downgrade / query / destroy)

Level 3: 回归测试 — 现有 113 用例全部通过
          (test_protocol / test_log / test_concurrent / test_tree)
```

---

## 3. 测试分类

### 3.1 功能正确性 (Functional)

| 类别 | 说明 | 优先级 |
|------|------|--------|
| 基本查找 | find 返回正确节点 / 不存在返回 NULL | P0 |
| 创建-查找 | get_or_create → find 能找回 | P0 |
| 重复创建 | 同 key 多次 get_or_create 返回同一节点 | P0 |
| 删除 | 释放锁后节点状态正确 | P0 |
| 遍历 | 遍历所有节点不遗漏不重复 | P0 |

### 3.2 性能 (Performance)

| 类别 | 说明 | 优先级 |
|------|------|--------|
| 大量节点 | 1000 节点创建+查找延迟 | P1 |
| 查找时间恒定 | 10 vs 1000 节点查找延迟无明显差异 | P1 |

### 3.3 边界 (Boundary)

| 类别 | 说明 | 优先级 |
|------|------|--------|
| NULL 参数 | find/get_or_create 传入 NULL tl | P1 |
| 零节点 | 空哈希表 find 返回 NULL | P1 |
| 大批量创建-销毁 | 100 次 create/destroy 循环 | P1 |

### 3.4 并发 (Concurrency)

| 类别 | 说明 | 优先级 |
|------|------|--------|
| 多线程创建不同节点 | 哈希表并发插入不冲突 | P1 |
| 共享实例并发 lock/unlock | 现有 SharedInstanceConsistency 覆盖 | P1 |

### 3.5 回归 (Regression)

| 类别 | 说明 | 优先级 |
|------|------|--------|
| 全量套件 | 113 用例全部通过 | P0 |
| 树模块 | lock_path/unlock_path 正常 | P0 |
| 并发模块 | 多线程竞争不变 | P0 |

---

## 4. 测试文件

| 文件 | 操作 | 说明 |
|------|------|------|
| `tests/test_concurrent.cc` | 追加 | 新增哈希表专项测试 (HashTable suite) |
| 现有 3 个测试文件 | 不变 | 回归验证 |

---

## 5. 与现有测试规范对齐

按照 CLAUDE.md 和 TEST_STRATEGY.md 要求：

- 每 API 必有专属测试 ✅ — find / get_or_create 的新增测试
- 每 API 必有关联测试 ✅ — lock/unlock/destroy 间接覆盖
- 测试注释规范 ✅ — 每个 TEST() 上方 目标/路径/覆盖 块
- 密度要求 ✅ — lock_table.c ~0.5K → 8 新增用例达 ≥15/KLOC

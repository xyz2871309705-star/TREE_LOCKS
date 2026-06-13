# TreeLocks JSON 树定义格式规格

> 版本: 1.0 | 日期: 2026-06-13

---

## 1. 概述

TreeLocks 使用 JSON 文件描述树结构的拓扑关系。库解析该文件后自动注册所有节点的父子关系，并启用协议自动校验。

**支持两种等价的 JSON 格式**：嵌套格式（直观）和扁平格式（易于程序生成）。两种格式定义完全相同的树结构，内部统一处理。

---

## 2. 顶层结构

| 字段 | 类型 | 必需 | 说明 |
|------|------|------|------|
| `version` | integer | 是 | 格式版本号，当前为 `1` |
| `name` | string | 否 | 树的名称（注释用途，不影响逻辑） |
| `tree` | object | 二选一 | **嵌套格式**的根节点对象 |
| `nodes` | array | 二选一 | **扁平格式**的节点列表数组 |

`tree` 和 `nodes` 必须提供且只能提供一个。

---

## 3. 格式 A：嵌套格式

树结构以递归的 `children` 数组表示，直观易读，适合手工编写。

### 3.1 节点对象字段

| 字段 | 类型 | 必需 | 说明 |
|------|------|------|------|
| `id` | integer (uint64) | **是** | 节点唯一标识，必须 > 0 |
| `label` | string | 否 | 节点标签，用于路径查找（如 `"/home/alice"`） |
| `children` | array | 否 | 子节点数组，每个元素是一个节点对象 |

### 3.2 示例

```json
{
  "version": 1,
  "name": "filesystem_locks",
  "tree": {
    "id": 1,
    "label": "/",
    "children": [
      {
        "id": 2,
        "label": "home",
        "children": [
          { "id": 10, "label": "alice" },
          { "id": 11, "label": "bob" }
        ]
      },
      {
        "id": 3,
        "label": "var",
        "children": [
          { "id": 20, "label": "log" },
          { "id": 21, "label": "cache" }
        ]
      }
    ]
  }
}
```

### 3.3 对应树结构

```
/ (id=1)
├── home (id=2)
│   ├── alice (id=10)
│   └── bob (id=11)
└── var (id=3)
    ├── log (id=20)
    └── cache (id=21)
```

### 3.4 路径规则

路径由根节点开始，沿 `label` 逐级拼接，以 `/` 分隔：

| 路径 | 对应节点 ID |
|------|-----------|
| `/` | 1 (根) |
| `/home` | 2 |
| `/home/alice` | 10 |
| `/var/log` | 20 |

如果根节点的 `label` 不是 `"/"`（如 `"root"`），则路径为 `/root/home/alice`。

---

## 4. 格式 B：扁平格式

所有节点平铺在一个数组中，通过 `parent` 字段引用父节点。

### 4.1 节点对象字段

| 字段 | 类型 | 必需 | 说明 |
|------|------|------|------|
| `id` | integer (uint64) | **是** | 节点唯一标识，必须 > 0 |
| `parent` | integer (uint64) | 否 | 父节点 ID。`0` 或省略表示根节点 |
| `label` | string | 否 | 节点标签，用于路径查找 |

### 4.2 示例

```json
{
  "version": 1,
  "name": "filesystem_locks",
  "nodes": [
    { "id": 1,  "label": "/" },
    { "id": 2,  "label": "home",   "parent": 1 },
    { "id": 10, "label": "alice",  "parent": 2 },
    { "id": 11, "label": "bob",    "parent": 2 },
    { "id": 3,  "label": "var",    "parent": 1 },
    { "id": 20, "label": "log",    "parent": 3 },
    { "id": 21, "label": "cache",  "parent": 3 }
  ]
}
```

### 4.3 特点

- 节点顺序无关（父节点可以先于或后于子节点出现）
- 适合程序/脚本批量生成
- 更容易进行 diff 和增量更新

---

## 5. 校验规则

库在加载 JSON 时自动执行以下校验，**任一失败即拒绝加载**：

| 编号 | 规则 | 错误行为 |
|------|------|---------|
| V1 | `id` 必须为大于 0 的整数 | 返回 `TREELOCK_ERR_INVAL` |
| V2 | 所有 `id` 必须唯一 | 返回 `TREELOCK_ERR_INVAL` |
| V3 | `parent` 必须指向已存在的节点（或 `0`/省略） | 返回 `TREELOCK_ERR_INVAL` |
| V4 | 有且仅有一个根节点（`parent == 0` 的节点） | 返回 `TREELOCK_ERR_INVAL` |
| V5 | 树中不得存在环路 | 返回 `TREELOCK_ERR_INVAL` |
| V6 | 嵌套格式最大深度不超过 256 层 | 返回 `TREELOCK_ERR_INVAL` |

### 校验错误类型对照

| 错误场景 | JSON 示例（扁平） | 日志输出 |
|----------|------------------|---------|
| ID 重复 | `{"id":1},{"id":1}` | `duplicate node_id=1` |
| 无根节点 | `{"id":1,"parent":2},{"id":2,"parent":1}` | `no root node found` |
| 多根节点 | `{"id":1,"parent":0},{"id":2,"parent":0}` | `multiple root nodes (2)` |
| parent 无效 | `{"id":1,"parent":99}` | `parent_id=99 does not exist` |
| 环路 | `1→2→3→1` | `cycle detected at node_id=X` |

---

## 6. 使用 API

```c
#include "treelock_tree.h"

// 从文件加载
treelock_load_tree_from_file(tl, "tree.json");

// 从内存字符串加载
treelock_load_tree_from_string(tl, json_string);

// 验证是否加载成功
if (treelock_tree_loaded(tl)) {
    // 树已就绪，可使用 treelock_lock_path 等 API
}
```

---

## 7. 完整示例：数据库分片场景

```json
{
  "version": 1,
  "name": "database_shards",
  "tree": {
    "id": 1,
    "label": "db",
    "children": [
      {
        "id": 10,
        "label": "cluster_a",
        "children": [
          { "id": 100, "label": "shard_01" },
          { "id": 101, "label": "shard_02" },
          { "id": 102, "label": "shard_03" }
        ]
      },
      {
        "id": 20,
        "label": "cluster_b",
        "children": [
          { "id": 200, "label": "shard_01" },
          { "id": 201, "label": "shard_02" }
        ]
      }
    ]
  }
}
```

对应的锁操作：

```c
// 排他锁住整个 cluster_a（包括其所有 shard）
treelock_lock_path(tl, "/db/cluster_a", TREELOCK_X);

// 共享读单个 shard
treelock_lock_path(tl, "/db/cluster_b/shard_01", TREELOCK_S);
```

> **注意**：同层级的 label 可以重复（如两个 `shard_01`），因为路径查找基于父节点内子节点的局部匹配，而非全局 label 索引。

---

## 8. 最佳实践

1. **根节点的 label 建议设为 `"/"`**：这样路径风格与文件系统一致（如 `"/home/alice"`）
2. **优先使用嵌套格式进行手工编写**：层次结构一目了然
3. **使用扁平格式进行程序生成**：避免递归嵌套，易于脚本拼装
4. **label 仅在需要进行路径查找时必须**：纯 ID 模式的加锁不需要 label
5. **node_id 使用有意义的编号方案**：如 `1xx` = 集群 A，`2xx` = 集群 B
6. **label 使用字母数字和下划线**：避免 `/` 出现在 label 中（会干扰路径解析）

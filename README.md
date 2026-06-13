# TreeLocks — 分布式多粒度树状锁管理器

[![Version](https://img.shields.io/badge/version-0.1.0-blue)](CHANGELOG.md)
[![Language](https://img.shields.io/badge/language-C11-green)]()
[![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux%20%7C%20macOS-lightgrey)]()

> 实现一套**分布式多粒度树状锁**系统，为通用 N 叉树结构提供一致性保护、死锁预防和分布式协调。

**GitHub**: https://github.com/xyz2871309705-star/TREE_LOCKS

---

## 目录

- [快速开始](#快速开始)
- [项目结构](#项目结构)
- [锁协议](#锁协议)
- [API 概览](#api-概览)
- [模块说明](#模块说明)
- [实现阶段](#实现阶段)
- [测试](#测试)
- [文档索引](#文档索引)
- [参考资料](#参考资料)

---

## 快速开始

### 环境要求

| 依赖 | 版本 | 说明 |
|------|------|------|
| CMake | >= 3.16 | 构建系统 |
| GCC / Clang / MSVC | 支持 C11 | MinGW GCC 13.1 已验证 |
| pthread | 系统自带 | 线程安全 |
| Python | >= 3.6 (可选) | clangd 辅助 |

### 克隆与构建

```bash
git clone git@github.com:xyz2871309705-star/TREE_LOCKS.git
cd TREE_LOCKS

# Linux / macOS / MinGW
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build

# Windows MSVC
cmake -B build -G "Visual Studio 17 2022"
cmake --build build --config Release
```

### CMake 选项

| 选项 | 默认值 | 说明 |
|------|--------|------|
| `CMAKE_BUILD_TYPE` | Debug | Debug / Release / RelWithDebInfo / MinSizeRel |
| `TREELOCK_BUILD_SHARED` | OFF | ON=动态库 (.dll/.so)，OFF=静态库 (.a) |
| `TREELOCK_BUILD_EXAMPLES` | ON | 构建示例程序 |
| `TREELOCK_BUILD_TESTS` | ON | 构建测试程序 |

### 运行测试

```bash
cd build
ctest --output-on-failure
# 或单独运行
./tests/test_protocol
./tests/test_concurrent
```

### 运行示例

```bash
./examples/basic_usage          # 4 个基础使用示例
./examples/log_callback_demo    # 日志回调注册示例
./examples/tree_usage           # 3 个树结构管理示例
```

---

## 项目结构

```
TREE_LOCKS/
├── CMakeLists.txt                  # 顶层 CMake (聚合模块)
├── README.md                       # 本文件
├── .gitignore
│
├── docs/                           # 文档
│   ├── 设计.md                     # 设计文档
│   ├── DEVELOPER.md                # 开发者文档 (详细)
│   └── ROADMAP.md                  # 迭代计划
│
├── modules/                        # ★ 模块 (独立 include/ + src/)
│   ├── treelock_log/               # 模块 0: 统一日志 [基础依赖]
│   │   ├── include/treelock_log.h      # 日志 API (6 级 + 回调注册)
│   │   └── src/log_core.c              # 线程安全 + 递归保护
│   │
│   ├── treelock_core/              # 模块 1: 核心库 [阶段一 ✅]
│   │   ├── include/
│   │   │   ├── treelock.h              # 公共 API
│   │   │   ├── treelock_types.h        # 类型封装 (INT_32/IN/OUT 等)
│   │   │   └── treelock_platform.h     # 平台抽象 (时间/DLL/TLS)
│   │   └── src/
│   │       ├── protocol.c              # 兼容矩阵、模式转换、协议校验
│   │       ├── lock_table.c            # 锁表、FIFO 等待队列
│   │       └── client.c                # 客户端实现 (线程安全+引用计数)
│   │
│   ├── treelock_tree/              # 模块 1.5: 树结构管理 [Iteration 1.5 ✅]
│   │   ├── include/treelock_tree.h     # 公共 API (树加载/路径加锁/查询)
│   │   └── src/
│   │       ├── tree_internal.h         # 内部数据结构 (hash 表/树索引)
│   │       ├── tree_core.c             # 树节点管理 + hash 表操作
│   │       ├── tree_json.c             # JSON 解析 (扁平/嵌套双格式)
│   │       ├── tree_validate.c         # 树校验 (ID 唯一/单根/无环)
│   │       ├── tree_path.c             # 路径解析 + 祖先锁推导
│   │       └── tree_api.c              # 公共 API 桥接实现
│   │
│   ├── cjson/                      # 第三方: cJSON v1.7.18 (MIT License)
│   │   ├── cJSON.h
│   │   └── cJSON.c
│   │
│   ├── treelock_comm/              # 模块 2: 通信层 [阶段二/三]
│   └── treelock_server/            # 模块 3: 服务端 [阶段三]
│
├── cmake/                          # CMake 辅助
│   ├── CompilerWarnings.cmake          # 编译器警告配置
│   └── expand_cc.py                    # clangd: 展开 @rsp 响应文件
│
├── proto/
│   └── treelock.proto              # Protobuf 消息定义
│
├── tests/
│   ├── test_protocol.c             # 协议正确性 (12 用例)
│   ├── test_concurrent.c           # 并发压力 (3 场景)
│   └── test_tree.c                 # 树结构管理 (51 用例)
│
└── examples/
    ├── basic_usage.c               # 基础使用 (4 示例)
    ├── log_callback_demo.c         # 日志回调注册
    └── tree_usage.c                # 树结构管理 (3 示例)
```

---

## 锁协议

### 锁模式

| 模式 | 符号 | 含义 |
|------|------|------|
| `TREELOCK_NL`  | NL  | 无锁 |
| `TREELOCK_IS`  | IS  | 意向共享 — 子树中有 S 锁 |
| `TREELOCK_IX`  | IX  | 意向排他 — 子树中有 X 锁 |
| `TREELOCK_S`   | S   | 共享 — 读整棵子树 |
| `TREELOCK_SIX` | SIX | S + IX — 读子树但某子节点被排他写 |
| `TREELOCK_X`   | X   | 排他 — 独占整棵子树 |

### 兼容矩阵

```
              NL   IS   IX    S   SIX   X
      NL  |   ✅   ✅   ✅   ✅   ✅   ✅
      IS  |   ✅   ✅   ✅   ✅   ✅   ❌
      IX  |   ✅   ✅   ✅   ❌   ❌   ❌
      S   |   ✅   ✅   ❌   ✅   ❌   ❌
      SIX |   ✅   ✅   ❌   ❌   ❌   ❌
      X   |   ✅   ❌   ❌   ❌   ❌   ❌
```

### 加锁协议 (4 条规则)

1. 获取 **S / IS** → 必须先持有父节点的 **IS** 或更强锁
2. 获取 **X / IX / SIX** → 必须先持有父节点的 **IX** 或更强锁
3. 释放锁 → 必须先释放子节点，再释放父节点 (**自底向上**)
4. 根节点 → 无父节点，可直接获取

### 使用示例

```c
/* 路径加锁: 读目录 + 写文件 */
treelock_lock(tl, root, TREELOCK_IS);    // 根: 意向共享
treelock_lock(tl, dir,  TREELOCK_IS);    // 目录: 意向共享
treelock_lock(tl, file, TREELOCK_X);     // 文件: 排他

/* ... 执行写操作 ... */

treelock_unlock(tl, file);               // 自底向上释放
treelock_unlock(tl, dir);
treelock_unlock(tl, root);
```

```c
/* 树结构管理: 从 JSON 加载树 + 路径加锁 (★ 新增) */
treelock_load_tree_from_file(tl, "my_tree.json");

// 一行加锁：自动 root(IX) → /home(IX) → /home/alice(X)
treelock_lock_path(tl, "/home/alice", TREELOCK_X);

// ... 安全写入 alice 的数据 ...

treelock_unlock_path(tl, "/home/alice");
```

---

## API 概览

```c
/* ========== 生命周期 ========== */
treelock_t *treelock_create (const treelock_config_t *config);
void        treelock_destroy(treelock_t *tl);

/* ========== 锁操作 ========== */
int treelock_lock    (treelock_t *tl, treelock_node_id_t node_id, treelock_mode_t mode);
int treelock_try_lock(treelock_t *tl, treelock_node_id_t node_id, treelock_mode_t mode, int timeout_ms);
int treelock_unlock  (treelock_t *tl, treelock_node_id_t node_id);
int treelock_unlock_all(treelock_t *tl);

/* ========== 升级/降级 ========== */
int treelock_escalate (treelock_t *tl, treelock_node_id_t node_id, treelock_mode_t new_mode);
int treelock_downgrade(treelock_t *tl, treelock_node_id_t node_id, treelock_mode_t new_mode);

/* ========== 查询 ========== */
treelock_mode_t treelock_get_mode  (treelock_t *tl, treelock_node_id_t node_id);
int             treelock_query_node(treelock_t *tl, treelock_node_id_t node_id, char **json_result);

/* ========== 回调 ========== */
int treelock_set_lost_callback(treelock_t *tl, treelock_lost_cb cb, void *user_data);

/* ========== 树结构管理 (libtreelock_tree) ========== */
int  treelock_load_tree_from_file(treelock_t *tl, const char *filepath);
int  treelock_load_tree_from_string(treelock_t *tl, const char *json_string);
int  treelock_register_node(treelock_t *tl, uint64_t node_id, uint64_t parent_id, const char *label);
int  treelock_lock_path(treelock_t *tl, const char *path, treelock_mode_t mode);
int  treelock_unlock_path(treelock_t *tl, const char *path);
int  treelock_get_parent(treelock_t *tl, uint64_t node_id, uint64_t *parent_id);
int  treelock_lookup_path(treelock_t *tl, const char *path, uint64_t *node_id);
```

---

## 模块说明

### treelock_log — 日志模块

统一日志接口，项目内所有输出必须通过此模块。

```c
#include "treelock_log.h"

// 6 级日志宏
TREELOCK_LOG_FATAL("TAG", "msg: %s", detail);
TREELOCK_LOG_ERROR("TAG", "error: %d", code);
TREELOCK_LOG_WARN ("TAG", "warning");
TREELOCK_LOG_INFO ("TAG", "client created");
TREELOCK_LOG_DEBUG("TAG", "mode=%s", name);
TREELOCK_LOG_TRACE("TAG", "--> enter func()");

// 外部回调注册 (接入自有日志系统)
treelock_log_set_callback(my_logger, my_context);
treelock_log_set_level(TREELOCK_LOG_WARN);  // 运行期过滤
```

### treelock_core — 核心库

产出 `libtreelock_core.a`，依赖 `treelock_log` + `pthread`。

关键特性:
- **线程安全**: 节点级互斥锁 + 全局锁表锁
- **引用计数**: 同客户端重入加锁，ref_count 归零才释放
- **FIFO 唤醒**: 条件变量 + 兼容性检查，不饿死等待者
- **原地升级**: escalate/downgrade 直接操作锁表，避免双重 entry

---

## 实现阶段

| 阶段 | 状态 | 内容 | 代码量 |
|------|------|------|--------|
| 阶段一 | 🚧 80% | 单机版库：协议 + 锁表 + 客户端 + 日志 + 树结构 + 跨平台 | ~3500 行 |
| 阶段二 | 📋 规划 | ZK 协调版：分布式锁协调 + Watch 回调 | +~2500 行 |
| 阶段三 | 📋 规划 | 自研服务版：Raft + gRPC + 租约管理 | +~6000 行 |

### 阶段一进度

- [x] 6 种锁模式 + 完整兼容矩阵
- [x] 锁升级/降级路径验证
- [x] FIFO 等待队列 + 条件变量唤醒
- [x] 同客户端重入加锁 (引用计数)
- [x] 线程安全 (pthread mutex)
- [x] 统一日志模块 (6 级 + 外部回调)
- [x] 跨平台构建 (Windows/Linux/macOS)
- [x] 12 协议测试 + 3 并发测试
- [x] **树结构管理** (JSON 加载 + 路径加锁 + 协议自动校验) ← **Iteration 1.5 完成**
- [ ] 租约与故障恢复
- [ ] 性能基准与内存检测

---

## 测试

| 测试 | 用例数 | 命令 |
|------|--------|------|
| 协议正确性 | 12 | `./build/tests/test_protocol` |
| 并发压力 | 3 | `./build/tests/test_concurrent` |
| 树结构管理 | 51 | `./build/tests/test_tree` |

```
$ ./tests/test_protocol
兼容矩阵测试:
  TEST: NL compatible with all modes ... PASSED
  TEST: X compatible only with NL ... PASSED
  ...
结果: 12/12 通过, 0 失败

$ ./tests/test_concurrent
  TEST: concurrent multi-client lock/unlock ... PASSED (8000 ops)
  TEST: concurrent consistency (shared instance) ... PASSED
  TEST: concurrent lock escalate ... PASSED
结果: 3/3 通过, 0 失败

$ ./tests/test_tree
--- Test 1: manual register_node ---
  PASS: create client
  ...
--- Test 9: backward compatibility (no tree) ---
  PASS: lock without tree works (backward compat)
结果: 51/51 通过, 0 失败
```

---

## 文档索引

| 文档 | 说明 |
|------|------|
| [设计.md](docs/设计.md) | 完整设计思路、协议推导、架构总览 |
| [DEVELOPER.md](docs/DEVELOPER.md) | 开发者文档：上手、架构、编码规范、模块详解、FAQ |
| [ROADMAP.md](docs/ROADMAP.md) | 迭代计划：14 个迭代、预估工时、优先级、验收标准 |

---

## 参考资料

- Gray, J. et al. (1976). *Granularity of Locks and Degrees of Consistency in a Shared Data Base*
- Mohan, C. et al. (1992). *ARIES/IM: An Efficient and High Concurrency Index Management Method*
- Ongaro, D. & Ousterhout, J. (2014). *In Search of an Understandable Consensus Algorithm (Raft)*

# TreeLocks — 开发者文档

> 版本: 0.2.0 | 状态: 阶段一 Iteration 1.5 完成 | 更新: 2026-06-13

---

## 1. 项目信息

| 项目 | 内容 |
|------|------|
| **仓库地址** | https://github.com/xyz2871309705-star/TREE_LOCKS |
| **项目名称** | TreeLocks — 分布式多粒度树状锁管理器 |
| **开发语言** | C11 |
| **构建系统** | CMake 3.16+ |
| **版本控制** | Git (master 分支) |
| **开源协议** | 待定 |

---

## 2. 快速上手

### 2.1 环境要求

| 依赖 | 版本 | 说明 |
|------|------|------|
| CMake | >= 3.16 | 构建系统 |
| GCC / Clang / MSVC | 支持 C11 | MinGW GCC 13.1 已验证 |
| pthread | 系统自带 | 线程安全锁 |
| Python | >= 3.6 (可选) | compile_commands.json @rsp 展开 |

### 2.2 克隆与构建

```bash
# 克隆
git clone git@github.com:xyz2871309705-star/TREE_LOCKS.git
cd TREE_LOCKS

# 构建 (Linux / MinGW)
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build

# 运行测试
cd build && ctest --output-on-failure

# 运行示例
./examples/basic_usage
```

### 2.3 平台构建命令速查

```bash
# Linux (GCC/Clang)
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# Windows (MinGW)
cmake -B build -G "MinGW Makefiles"
cmake --build build

# Windows (MSVC)
cmake -B build -G "Visual Studio 17 2022"
cmake --build build --config Release

# macOS
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(sysctl -n hw.ncpu)

# 构建动态库
cmake -B build -DTREELOCK_BUILD_SHARED=ON
```

### 2.4 CMake 选项

| 选项 | 默认值 | 说明 |
|------|--------|------|
| `CMAKE_BUILD_TYPE` | Debug | Debug / Release / RelWithDebInfo / MinSizeRel |
| `TREELOCK_BUILD_SHARED` | OFF | ON=动态库(.dll/.so), OFF=静态库(.a) |
| `TREELOCK_BUILD_TESTS` | ON | 是否构建测试 |
| `TREELOCK_BUILD_EXAMPLES` | ON | 是否构建示例 |

---

## 3. 项目架构

### 3.1 目录结构

```
TREE_LOCKS/
├── CMakeLists.txt                # 顶层 CMake (聚合模块)
├── README.md                     # 项目简介
├── .gitignore                    # Git 忽略规则
│
├── docs/                         # 文档
│   ├── 设计.md                   # 设计文档
│   └── DEVELOPER.md              # 本文件 — 开发者文档
│
├── modules/                      # ★ 模块目录 (各模块独立 include/ + src/)
│   ├── treelock_log/             # 模块 0: 统一日志
│   │   ├── CMakeLists.txt
│   │   ├── include/
│   │   │   └── treelock_log.h        # 日志公共 API
│   │   └── src/
│   │       ├── log_internal.h        # 日志内部结构
│   │       └── log_core.c            # 日志核心实现
│   │
│   ├── treelock_core/            # 模块 1: 核心锁协议与客户端 API [阶段一]
│   │   ├── CMakeLists.txt
│   │   ├── include/
│   │   │   ├── treelock.h            # 公共 API (锁操作、升级/降级、查询)
│   │   │   ├── treelock_types.h      # 基础类型封装 (INT_32, IN/OUT 等宏)
│   │   │   └── treelock_platform.h   # 平台抽象层 (时间、DLL 导出、TLS)
│   │   └── src/
│   │       ├── internal.h            # 内部数据结构 (锁表、等待队列)
│   │       ├── protocol.c            # 兼容矩阵、模式转换、锁升级/降级路径
│   │       ├── lock_table.c          # 锁表管理 (授权列表、等待队列、FIFO 唤醒)
│   │       └── client.c              # 客户端 API 实现 (线程安全、引用计数)
│   │
│   ├── treelock_tree/             # 模块 1.5: 树结构管理 [Iteration 1.5 ✅]
│   │   ├── CMakeLists.txt
│   │   ├── include/
│   │   │   └── treelock_tree.h        # 公共 API (树加载/路径加锁/查询)
│   │   └── src/
│   │       ├── tree_internal.h        # 内部数据结构 (hash 表/树索引/桥接)
│   │       ├── tree_core.c            # 树节点管理 + hash 表 CRUD
│   │       ├── tree_json.c            # JSON 解析 (扁平/嵌套双格式)
│   │       ├── tree_validate.c        # 树结构校验 (环检测/ID 唯一/单根)
│   │       ├── tree_path.c            # 路径解析 + 祖先锁推导
│   │       └── tree_api.c             # 公共 API 桥接 (文件读取/树加载/路径加锁)
│   │
│   ├── cjson/                     # 第三方: cJSON v1.7.18 (MIT License)
│   │   ├── CMakeLists.txt
│   │   ├── cJSON.h
│   │   └── cJSON.c
│   │
│   ├── treelock_comm/            # 模块 2: 通信层 [阶段二/三 占位]
│   │   ├── CMakeLists.txt
│   │   ├── include/
│   │   │   └── treelock_comm.h       # 通信层 API 声明
│   │   └── src/                      # (待实现)
│   │
│   └── treelock_server/          # 模块 3: 锁管理服务端 [阶段三 占位]
│       ├── CMakeLists.txt
│       ├── include/
│       │   └── treelock_server.h     # 服务端 API 声明
│       └── src/                      # (待实现)
│
├── cmake/                        # CMake 辅助脚本
│   ├── CompilerWarnings.cmake        # 编译器警告配置
│   └── expand_cc.py                  # 展开 compile_commands.json 中的 @rsp
│
├── proto/                        # 通信协议定义
│   └── treelock.proto                # Protobuf 消息格式
│
├── tests/                        # 测试
│   ├── CMakeLists.txt
│   ├── test_protocol.cc              # 协议正确性测试 (12 GTest 用例)
│   ├── test_log.cc                   # 日志模块测试 (12 GTest 用例)
│   ├── test_concurrent.cc            # 并发压力测试 (3 GTest 用例)
│   └── test_tree.cc                  # 树结构管理测试 (9 GTest 用例)
│
└── examples/                     # 示例
    ├── CMakeLists.txt
    ├── src/
    │   ├── basic_usage.c                 # 4 个基础使用示例
    │   ├── log_callback_demo.c           # 日志回调注册示例
    │   └── tree_usage.c                  # 树结构管理示例 (4 场景)
    └── json/
        ├── filesystem_tree.json          # 嵌套格式 JSON 树定义
        └── filesystem_tree_flat.json     # 扁平格式 JSON 树定义
```

### 3.2 模块依赖关系

```
                    ┌──────────────┐
                    │  treelock_log │  ← 模块 0: 基础模块，零外部依赖
                    │  (日志模块)   │     仅依赖 pthread + C 标准库
                    └──────┬───────┘
                           │ 依赖
            ┌──────────────┼──────────────┐
            │              │              │
    ┌───────▼──────┐ ┌─────▼──────┐ ┌─────▼──────┐
    │ treelock_core│ │ cjson      │ │ 其他模块   │
    │ (锁协议+API) │ │ (第三方)   │ │            │
    └───────┬──────┘ └─────┬──────┘ └────────────┘
            │              │
            └──────┬───────┘
                   │ 依赖
          ┌────────▼────────┐
          │  treelock_tree   │  ← 模块 1.5: 树结构管理 [阶段一]
          │  (JSON加载+路径  │     依赖: treelock_core + cjson
          │   加锁+协议校验) │
          └─────────────────┘

未来阶段:
  treelock_core ──→ treelock_comm ──→ treelock_server
       [阶段一]        [阶段二/三]         [阶段三]
```

### 3.3 分层架构

```
┌─────────────────────────────────────────────┐
│  API 层 (treelock.h)                        │
│  lock / unlock / try_lock / escalate / ...  │
├─────────────────────────────────────────────┤
│  协议层 (protocol.c)                        │
│  兼容矩阵 / 模式转换 / 升级降级路径          │
├─────────────────────────────────────────────┤
│  锁表层 (lock_table.c)                      │
│  内存锁表 / 授权列表 / FIFO 等待队列        │
├─────────────────────────────────────────────┤
│  客户端层 (client.c)                        │
│  线程安全封装 / 引用计数 / 协议校验          │
├─────────────────────────────────────────────┤
│  平台抽象层 (treelock_platform.h)           │
│  时间函数 / DLL 导出 / TLS / 跨平台 sleep   │
└─────────────────────────────────────────────┘
```

---

## 4. 编码规范

### 4.1 类型系统

项目**禁止直接使用原始 C 类型**，必须使用 `treelock_types.h` 中的封装宏：

| 原始类型 | 封装宏 | 用途 |
|----------|--------|------|
| `int8_t` | `INT_8` | 8 位有符号整数 |
| `uint8_t` | `UINT_8` | 8 位无符号整数 |
| `int32_t` | `INT_32` | 32 位有符号整数 |
| `uint64_t` | `UINT_64` | 64 位无符号整数 |
| `char` | `CHAR` | 字符 |
| `void` | `VOID` | 空类型 |
| `void*` | `PTR_VOID` | 无类型指针 |
| `const char*` | `CSTR_PTR` | 常量字符串指针 |
| `int` (返回值) | `RET_CODE` / `BOOL` | 错误码 / 布尔值 |
| `uint64_t` (节点ID) | `TREE_NODE_ID` | 树节点 ID |
| `int64_t` (时间戳) | `TIMESTAMP_MS` | 毫秒级时间戳 |

### 4.2 参数方向宏

在函数声明和注释中使用，标注参数传递语义：

```c
IN      // 只入不出：调用者传入，函数只读
OUT     // 只出不入：函数写入结果
INOUT   // 入且出：传入初始值，可能被修改
```

### 4.3 函数头注释格式

**每个函数**（包括 static 内部函数）必须包含：

```c
/**
 * 函数名称：function_name
 *
 * 功能描述：简要说明函数功能（可以多行）
 *
 * @param[IN]    param_name - 参数说明 (类型/用途)
 * @param[OUT]   param_name - 输出参数说明
 * @param[INOUT] param_name - 参数说明
 *
 * @return 返回值说明
 * @retval VALUE - 特定返回值含义 (可选)
 */
```

### 4.4 命名约定

| 类型 | 命名规则 | 示例 |
|------|---------|------|
| 公开函数 | `treelock_<动词>_<名词>` | `treelock_lock`, `treelock_get_mode` |
| 模块内部函数 | `_<动词>_<名词>` | `_do_lock_core`, `_remove_held_lock` |
| 静态全局变量 | `g_<名称>` | `g_log_ctx`, `g_tests_passed` |
| 宏常量 | `TREELOCK_<模块>_<描述>` | `TREELOCK_GRANT_INIT_CAP` |
| 类型名 | `treelock_<描述>_t` | `treelock_node_t`, `treelock_mode_t` |

### 4.5 日志规范

**禁止使用 `printf` / `fprintf`** 直接输出。必须通过日志模块：

```c
#include "treelock_log.h"

TREELOCK_LOG_FATAL("TAG", "致命错误: %s", msg);
TREELOCK_LOG_ERROR("TAG", "操作失败: node=%llu", nid);
TREELOCK_LOG_WARN ("TAG", "潜在问题");
TREELOCK_LOG_INFO ("TAG", "关键流程: 客户端已创建");
TREELOCK_LOG_DEBUG("TAG", "调试信息: mode=%s", treelock_mode_name(m));
TREELOCK_LOG_TRACE("TAG", "追踪: --> enter func()");
```

---

## 5. 模块详解

### 5.1 treelock_log — 日志模块

**产出**: `libtreelock_log.a`

**公共 API**：

```c
// 运行期等级控制
VOID treelock_log_set_level(IN treelock_log_level_t level);

// 注册自定义回调 (接入客户自有日志系统)
VOID treelock_log_set_callback(IN treelock_log_callback_t cb, IN PTR_VOID user_data);

// 核心输出 (通常通过宏调用)
VOID treelock_log_write(IN treelock_log_level_t level, IN CSTR_PTR tag, ...);
```

**日志等级**：

```
OFF (0) → FATAL (1) → ERROR (2) → WARN (3) → INFO (4) → DEBUG (5) → TRACE (6)
```

**特性**：
- 编译期裁剪：Release 构建自动移除 DEBUG/TRACE (宏内 `if (LEVEL <= MAX_LEVEL)` 被编译器优化掉)
- 运行期过滤：`treelock_log_set_level(TREELOCK_LOG_WARN)` 只输出 WARN 及以上
- 线程安全：mutex + 递归保护
- 回调机制：外部可注册自定义输出函数

### 5.2 treelock_core — 核心锁协议

**产出**: `libtreelock_core.a`

**锁模式**：

| 模式 | 符号 | 含义 |
|------|------|------|
| `TREELOCK_NL`  | NL  | 无锁 |
| `TREELOCK_IS`  | IS  | 意向共享 (子树中有 S 锁) |
| `TREELOCK_IX`  | IX  | 意向排他 (子树中有 X 锁) |
| `TREELOCK_S`   | S   | 共享 (读整棵子树) |
| `TREELOCK_SIX` | SIX | S + IX |
| `TREELOCK_X`   | X   | 排他 (独占整棵子树) |

**兼容矩阵**：

```
              NL   IS   IX   S   SIX   X
      NL  |   Y    Y    Y   Y    Y    Y
      IS  |   Y    Y    Y   Y    Y    N
      IX  |   Y    Y    Y   N    N    N
      S   |   Y    Y    N   Y    N    N
      SIX |   Y    Y    N   N    N    N
      X   |   Y    N    N   N    N    N
```

**加锁协议 (4 条规则)**：

1. 获取 S/IS → 必须先持有父节点 IS 或更强
2. 获取 X/IX/SIX → 必须先持有父节点 IX 或更强
3. 释放锁 → 必须先释放子节点，再释放父节点 (自底向上)
4. 根节点无父节点，可直接获取

**核心 API**：

```c
// 生命周期
treelock_t *treelock_create(IN const treelock_config_t *config);
VOID        treelock_destroy(IN treelock_t *tl);

// 锁操作
RET_CODE treelock_lock     (IN treelock_t *tl, IN treelock_node_id_t nid, IN treelock_mode_t mode);
RET_CODE treelock_try_lock (IN treelock_t *tl, IN treelock_node_id_t nid, IN treelock_mode_t mode, IN INT_32 timeout_ms);
RET_CODE treelock_unlock   (IN treelock_t *tl, IN treelock_node_id_t nid);

// 升级/降级
RET_CODE treelock_escalate  (IN treelock_t *tl, IN treelock_node_id_t nid, IN treelock_mode_t new_mode);
RET_CODE treelock_downgrade (IN treelock_t *tl, IN treelock_node_id_t nid, IN treelock_mode_t new_mode);
```

**关键数据结构**：

```
treelock_t (客户端实例)
  ├── lock_table  → treelock_node_t 链表 (按 node_id 索引)
  │   ├── grants[]        ← 已授权锁列表
  │   ├── wait_queue[]    ← FIFO 等待队列 (条件变量)
  │   └── mutex           ← 节点级互斥锁
  ├── held_locks[]        ← 当前客户端已持有锁 (含 ref_count)
  └── held_mutex / table_mutex
```

### 5.3 treelock_tree — 树结构管理

**产出**: `libtreelock_tree.a`

**职责**: JSON 树定义加载、路径加锁/解锁、协议自动校验。

**关键特性**:
- **双格式 JSON**: 支持扁平 `[{"id":...}]` 和嵌套 `{"tree":...}` 两种格式
- **树结构校验**: ID 唯一性、parent 有效性、单根节点、环路检测
- **协议自动校验**: lock 时自动检查父节点是否持有适当的意向锁（IS/IX）
- **路径加锁**: `treelock_lock_path(tl, "/home/alice", X)` 自动沿祖先路由，为祖先节点获取适当的意向锁
- **向后兼容**: 未加载树时协议校验跳过，现有代码不受影响
- **hash 表索引**: 256 桶链式 hash 实现 O(1) 节点查找

**公共 API**:

```c
// L1: 树加载
int treelock_load_tree_from_file(tl, "tree.json");
int treelock_load_tree_from_string(tl, json_str);
int treelock_register_node(tl, node_id, parent_id, label);

// L2: 路径加锁
int treelock_lock_path(tl, "/home/alice", TREELOCK_X);
int treelock_unlock_path(tl, "/home/alice");

// 查询
int treelock_get_parent(tl, node_id, &parent_id);
int treelock_lookup_path(tl, "/home/alice", &node_id);
int treelock_tree_loaded(tl);
```

**内部架构**:

```
treelock_tree.h (公共 API)
      │
      ▼
tree_api.c ── 桥接 treelock_core 与 tree_index
      │
      ├── tree_json.c      ── JSON 解析 (依赖 cJSON)
      ├── tree_validate.c  ── 树结构校验 (5 项检查)
      ├── tree_core.c      ── 树索引管理 (hash 表 CRUD)
      └── tree_path.c      ── 路径解析 + 祖先锁推导
```

**与 treelock_core 的桥接**:

`treelock_s` 新增两个字段:
- `void *tree_data` — 指向 `treelock_tree_index_t` 的不透明指针
- `treelock_tree_get_parent_fn tree_get_parent` — 父节点查询回调

树模块通过 `tree_data` 注入索引，通过 `tree_get_parent` 回调让 `treelock_validate_protocol()` 在 `_do_lock_core()` 中自动执行协议校验。

### 5.4 treelock_platform — 平台抽象层

**职责**：统一 Windows / Linux / macOS 平台差异。

```c
TREELOCK_API              // DLL 导入导出 (Windows __declspec, Linux visibility)
TREELOCK_INLINE           // 跨编译器内联
TREELOCK_THREAD_LOCAL     // 线程局部存储

TIMESTAMP_MS treelock_platform_time_ms(VOID);      // 毫秒时间戳
VOID         treelock_platform_local_time(...);     // 本地时间格式化
```

**实现细节**：
- Windows: `_ftime64_s` (无需 `<windows.h>`，避免类型冲突)
- Linux: `gettimeofday` + `localtime_r`

---

## 6. 实现阶段

| 阶段 | 状态 | 内容 | 代码量 (估) |
|------|------|------|-------------|
| **阶段一** | ✅ 80% | 单机版库：协议 + 锁表 + 客户端 + 树结构管理 | ~3500 行 |
| **阶段二** | 📋 规划 | ZK 集成版：分布式协调 + Watch 回调 | +~2500 行 |
| **阶段三** | 📋 规划 | 自研服务版：Raft + gRPC + 租约管理 | +~6000 行 |

**阶段一已完成功能**：
- [x] 6 种锁模式 + 完整兼容矩阵
- [x] 锁升级/降级路径验证
- [x] FIFO 等待队列 + 条件变量唤醒
- [x] 同客户端重入加锁 (引用计数)
- [x] 线程安全 (pthread mutex)
- [x] 统一日志模块 (6 级 + 外部回调)
- [x] 跨平台构建 (Windows/Linux/macOS)
- [x] 12 个协议测试 + 3 个并发测试
- [x] 树结构管理 (JSON 加载 + 路径加锁 + 协议自动校验) ← Iteration 1.5

---

## 7. 测试

### 7.1 测试文件

| 文件 | 用例数 | 说明 |
|------|--------|------|
| `tests/test_protocol.cc` | 12 | 兼容矩阵、模式转换、升级/降级路径 |
| `tests/test_log.cc` | 12 | 文件输出、日志等级过滤、文件切换、回调共存 |
| `tests/test_concurrent.cc` | 3 | 多客户端并发、共享实例一致性、锁升级竞态 |
| `tests/test_tree.cc` | 9 | JSON 解析、树校验、协议自动执行、路径加锁、向后兼容 |

### 7.2 测试框架

使用 **Google Test v1.15.2** 框架。GTest 通过 `_deps/googletest/` 本地副本提供（由 `gh repo clone` 获取），CMake `FetchContent` 集成。

```cmake
# tests/CMakeLists.txt
include(FetchContent)
FetchContent_Declare(googletest SOURCE_DIR "${CMAKE_SOURCE_DIR}/_deps/googletest")
FetchContent_MakeAvailable(googletest)
```

测试文件为 C++ (`.cc`)，被测 C 代码通过 `extern "C"` 调用。

### 7.3 运行测试

```bash
# 构建并运行
cmake -B build && cmake --build build
cd build && ctest --output-on-failure

# 或单独运行（GTest 原生输出）
./tests/test_protocol
./tests/test_tree

# 运行特定测试用例
./tests/test_protocol --gtest_filter='CompatMatrix.*'
```

### 7.4 编写新测试

测试文件放在 `tests/` 目录，使用 GTest 框架：

```cpp
#include <gtest/gtest.h>

extern "C" {
#include "treelock.h"
}

TEST(MySuite, MyTest) {
    treelock_t *tl = treelock_create(nullptr);
    ASSERT_NE(tl, nullptr);
    EXPECT_EQ(treelock_lock(tl, 1, TREELOCK_X), TREELOCK_OK);
    treelock_destroy(tl);
}
```

---

## 8. 常见问题

### Q: clangd 代码跳转不工作？

```bash
# 1. 重新生成 compile_commands.json
cmake -B build

# 2. VS Code 重启 clangd
Ctrl+Shift+P → clangd: Restart language server
```

`compile_commands.json` 生成在 `build/` 目录，clangd 通过 VS Code 设置 `--compile-commands-dir=build` 读取。

### Q: Windows 上编译报 `time_t` 找不到？

需要 MinGW 工具链。项目已验证 CLion 自带的 MinGW GCC 13.1。

### Q: 阶段一中两个锁实例如何互斥？

阶段一是进程内单客户端锁表。跨客户端互斥需要阶段三分布式锁管理器。

### Q: 如何将日志接入自己的系统？

```c
// 注册自定义回调
void my_logger(level, tag, file, line, func, msg, user_data) {
    // 写入你的日志系统
}
treelock_log_set_callback(my_logger, NULL);
```

---

## 9. 参考资料

- [设计文档](设计.md) — 完整的设计思路和协议推导
- Gray, J. et al. (1976). *Granularity of Locks and Degrees of Consistency in a Shared Data Base*
- Mohan, C. et al. (1992). *ARIES/IM: An Efficient and High Concurrency Index Management Method*
- Ongaro, D. & Ousterhout, J. (2014). *In Search of an Understandable Consensus Algorithm (Raft)*

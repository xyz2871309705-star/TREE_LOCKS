# TreeLocks - 分布式多粒度树状锁管理器

[![Version](https://img.shields.io/badge/version-0.1.0-blue)](CHANGELOG.md)
[![Language](https://img.shields.io/badge/language-C11-green)]()

> 实现一套 **分布式多粒度树状锁** 系统，为通用 N 叉树结构提供一致性保护、死锁预防和分布式协调。

## 快速开始

### 前置条件

- CMake >= 3.16
- C11 编译器（GCC、Clang、MSVC）
- pthread（系统自带）

### 构建

```bash
mkdir build && cd build
cmake ..
cmake --build .
```

### 构建选项

| 选项 | 默认值 | 说明 |
|------|--------|------|
| `TREELOCK_BUILD_EXAMPLES` | ON | 构建示例程序 |
| `TREELOCK_BUILD_TESTS` | ON | 构建测试程序 |
| `CMAKE_BUILD_TYPE` | Debug | 构建类型 |

示例：

```bash
cmake -DCMAKE_BUILD_TYPE=Release -DTREELOCK_BUILD_TESTS=OFF ..
```

### 运行测试

```bash
cd build
ctest --output-on-failure
```

### 运行示例

```bash
cd build
./examples/basic_usage
```

## 项目结构

```
TreeLocks/
├── CMakeLists.txt              # 顶层 CMake 构建
├── README.md                   # 本文件
├── .gitignore                  # Git 忽略规则
├── docs/
│   └── 设计.md                 # 设计文档
├── include/
│   └── treelock.h              # 公共 API 头文件
├── src/
│   ├── CMakeLists.txt          # 核心库构建
│   ├── internal.h              # 内部数据结构
│   ├── protocol.c              # 兼容矩阵、模式转换
│   ├── lock_table.c            # 锁表、等待队列
│   └── client.c                # 客户端 API 实现
├── tests/
│   ├── CMakeLists.txt          # 测试构建
│   ├── test_protocol.c         # 协议正确性测试
│   └── test_concurrent.c       # 并发压力测试
├── examples/
│   ├── CMakeLists.txt          # 示例构建
│   └── basic_usage.c           # 基础使用示例
├── proto/
│   └── treelock.proto          # Protobuf 定义（阶段三）
└── server/
    └── CMakeLists.txt          # 服务端构建（阶段三）
```

## 实现阶段

| 阶段 | 状态 | 说明 |
|------|------|------|
| 阶段一：单机版 | 🚧 开发中 | 纯库，零外部依赖 |
| 阶段二：ZK 协调版 | 📋 规划中 | 基于 ZooKeeper 的分布式协调 |
| 阶段三：自研服务版 | 📋 规划中 | 基于 Raft 的完整分布式方案 |

## API 概览

```c
/* 创建/销毁 */
treelock_t *treelock_create(const treelock_config_t *config);
void treelock_destroy(treelock_t *tl);

/* 锁操作 */
int treelock_lock(treelock_t *tl, treelock_node_id_t node_id, treelock_mode_t mode);
int treelock_try_lock(treelock_t *tl, treelock_node_id_t node_id,
                      treelock_mode_t mode, int timeout_ms);
int treelock_unlock(treelock_t *tl, treelock_node_id_t node_id);

/* 锁升级/降级 */
int treelock_escalate(treelock_t *tl, treelock_node_id_t node_id, treelock_mode_t new_mode);
int treelock_downgrade(treelock_t *tl, treelock_node_id_t node_id, treelock_mode_t new_mode);

/* 查询 */
treelock_mode_t treelock_get_mode(treelock_t *tl, treelock_node_id_t node_id);
```

## 许可证

待定

## 参考资料

- Gray, J., et al. (1976). *Granularity of Locks and Degrees of Consistency in a Shared Data Base*
- Mohan, C., et al. (1992). *ARIES/IM: An Efficient and High Concurrency Index Management Method*
- Ongaro, D., & Ousterhout, J. (2014). *In Search of an Understandable Consensus Algorithm (Raft)*

# TreeLocks 日志模块 — 变更日志

本文档记录 `treelock_log` 模块的所有重要变更。

格式基于 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.0.0/)，
版本号遵循 [语义化版本](https://semver.org/lang/zh-CN/)。

---

## [未发布]

### 新增
- **文件输出 API**：新增 `treelock_log_set_file()`、`treelock_log_close_file()`、
  `treelock_log_get_file()` 三个公共函数，支持将日志写入用户指定的文件。
- 文件输出与回调机制**并行独立**：无论用户是否注册自定义回调，日志都会写入文件。
- 文件内容**不含 ANSI 颜色转义码**，格式为 `[时间戳] [等级] [标签] 文件:行 函数() 消息`。

### 修改
- `treelock_log_context_t` 结构新增 `FILE* log_file` 和 `CHAR log_file_path[512]` 字段。
- `treelock_log_write_va()` 在回调调用后追加文件写入逻辑。
- 新增 `_log_write_to_file()` 内部函数，负责格式化并写入文件（无颜色码）。

---

## [0.1.1] — 2026-06-13

### 修复
- **日志递归保护竞态**：递归保护检查从 `pthread_mutex_lock` 之前移到加锁之后，
  消除多线程同时通过检查的竞态条件。
- **localtime_s 指针类型不兼容**：添加显式 `time_t` 中间变量，解决 MinGW 下
  `tv.tv_sec`（32 位）与 `localtime_s` 参数（64 位）类型不匹配问题。

### 重构
- 日志模块的时间获取不再直接调用系统 API，改为委托 `treelock_platform.h` 中的
  `treelock_platform_local_time()` 函数。
- 移除 `log_core.c` 中对 `<sys/time.h>` / `<time.h>` 的直接包含。

---

## [0.1.0] — 2026-06-13

### 新增
- **模块初始化**：创建 `treelock_log` 模块，提供统一的日志记录能力。
- **6 级日志等级**：`FATAL` / `ERROR` / `WARN` / `INFO` / `DEBUG` / `TRACE`。
- **编译期 + 运行期双重控制**：
  - 编译期：通过 `TREELOCK_LOG_MAX_LEVEL` 宏裁剪代码（Debug 保留全部，Release 裁剪 DEBUG/TRACE）。
  - 运行期：通过 `treelock_log_set_level()` / `treelock_log_get_level()` 动态调整。
- **日志宏**：`TREELOCK_LOG_FATAL` / `ERROR` / `WARN` / `INFO` / `DEBUG` / `TRACE`，
  以及函数追踪便捷宏 `TREELOCK_LOG_FUNC_ENTER` / `FUNC_EXIT` / `FUNC_EXIT_RC`。
- **自定义回调机制**：通过 `treelock_log_set_callback()` 注册回调，客户可接入
  自有日志系统（syslog、文件、远程收集等）。
- **默认 stderr 回调**：带 ANSI 颜色标记的格式化终端输出，含时间戳、等级、标签、
  文件名、行号、函数名。
- **线程安全**：使用 `pthread_mutex_t` 保证日志输出的原子性。
- **递归保护**：防止回调中再次写日志导致死锁。
- **等级名称 API**：`treelock_log_level_name()` 将枚举值转为字符串。
- **零外部依赖**：仅依赖标准 C 库和 pthread。

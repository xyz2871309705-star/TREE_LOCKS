# TreeLocks 测试策略与用例文档

版本: 0.3.0 | 日期: 2026-06-14

## 1. 测试架构

```
┌──────────────────────────────────────────────────┐
│  E2E / 集成测试 (Phase 2/3: 多机分布式场景)        │
├──────────────────────────────────────────────────┤
│  test_concurrent.cc — 并发压力测试 (12 用例)       │
│  (多线程 lock/unlock/escalate/downgrade/query     │
│   + 等待队列压力 + 多实例创建/销毁)                 │
├──────────────────────────────────────────────────┤
│  test_tree.cc       — 树管理 + 协议校验集成 (29 用例)│
│  (JSON加载→校验→锁路径 + 卸载/重载 + 生命周期)     │
├──────────────────────────────────────────────────┤
│  test_protocol.cc   — 协议纯逻辑单元测试 (19 用例)  │
│  (兼容矩阵/升级降级/工具函数/边界)                  │
├──────────────────────────────────────────────────┤
│  test_log.cc        — 日志模块单元测试 (15 用例)    │
│  (文件输出/等级过滤/回调/切换)                      │
└──────────────────────────────────────────────────┘
```

## 2. 模块测试密度

| 模块 | 源文件 | 代码行数 | 测试文件 | 用例数 | 密度/KLOC | 目标 | 状态 |
|------|--------|---------|---------|--------|-----------|------|------|
| `treelock_log` | `log_core.c` | ~1.0K | `test_log.cc` | 15 | 15.0 | ≥15 | ✅ |
| `treelock_core/protocol` | `protocol.c` | ~0.3K | `test_protocol.cc` | 19 | — | — | ✅ |
| `treelock_core/client` | `client.c` | ~1.1K | `test_concurrent.cc` (间接) | — | — | — | — |
| `treelock_core/lock_table` | `lock_table.c` | ~0.5K | `test_concurrent.cc` (间接) | — | — | — | — |
| `treelock_core` (合计) | — | ~3.0K | `test_protocol.cc` + `test_concurrent.cc` | 31 | 10.3 | ≥45 | ⚠️ |
| `treelock_tree` | `tree_*.c` (5 files) | ~2.7K | `test_tree.cc` | 29 | 10.7 | ≥41 | ⚠️ |

> **测试密度规则**：每 1K 行模块源码至少 15 个测试用例。`treelock_core` 和 `treelock_tree` 的下限分别为 45 和 41 用例，当前均为 ⚠️ 待补充。

## 3. 测试用例清单

### 3.1 test_protocol.cc — 协议层 (19 用例)

被测文件: `modules/treelock_core/src/protocol.c`

#### 兼容矩阵

| 用例 | 描述 |
|------|------|
| `CompatMatrix.NL_CompatibleWithAll` | NL 模式与所有模式兼容 |
| `CompatMatrix.X_OnlyCompatibleWithNL` | X 模式只与 NL 兼容 |
| `CompatMatrix.IS_CompatibleWithAllExceptX` | IS 模式只与 X 不兼容 |
| `CompatMatrix.IX_OnlyCompatibleWithNL_IS_IX` | IX 模式只与 NL/IS/IX 兼容 |
| `CompatMatrix.S_OnlyCompatibleWithNL_IS_S` | S 模式只与 NL/IS/S 兼容 |
| `CompatMatrix.SIX_OnlyCompatibleWithNL_IS` | SIX 模式只与 NL/IS 兼容 |
| `CompatMatrix.FullMatrix` | 遍历全部 36 个组合，对照 Gray 6-mode 标准矩阵 |

#### 父节点锁模式

| 用例 | 描述 |
|------|------|
| `RequiredParentMode.AllModes` | 6 种请求模式对应的父节点最小锁模式 |

#### 越界参数

| 用例 | 描述 |
|------|------|
| `Boundary.ModeCompatibleOutOfRange` | mode > MAX 时返回 FALSE |
| `Boundary.RequiredParentModeOutOfRange` | mode > MAX 时返回 NL |
| `Boundary.EscalateValidOutOfRange` | 越界参数返回 FALSE |
| `Boundary.DowngradeValidOutOfRange` | 越界参数返回 FALSE |

#### 锁升级/降级路径

| 用例 | 描述 |
|------|------|
| `EscalatePaths.Valid` | 全部合法升级路径 (NL→*, IS→*, IX→*, S→*, SIX→X) |
| `EscalatePaths.Invalid` | 非法升级路径 (向下/相同/越级) |
| `DowngradePaths.Valid` | 全部合法降级路径 (X→*, SIX→*) |
| `DowngradePaths.Invalid` | 非法降级路径 (向上/相同) |

#### 工具函数

| 用例 | 描述 |
|------|------|
| `Utils.ModeName` | 6 种模式名称 + 非法值 → "UNKNOWN" |
| `Utils.StrError` | 7 种错误码返回非空描述 |
| `Utils.StrErrorUnknownCode` | 未知错误码 → "Unknown error" |

### 3.2 test_log.cc — 日志模块 (15 用例)

被测文件: `modules/treelock_log/src/log_core.c`

#### 文件输出

| 用例 | 描述 |
|------|------|
| `LogFileTest.BasicFileOutput` | 写文件 + 内容验证 |
| `LogFileTest.NoAnsiCodes` | 文件中不含 ANSI 颜色码 |
| `LogFileTest.AllLevels` | 6 种等级全部输出到文件 |
| `LogFileTest.RespectsLogLevel` | WARN 级别过滤 INFO/DEBUG |
| `LogFileTest.WriteIntegrity` | 连续 50 条日志完整性 |

#### 文件切换

| 用例 | 描述 |
|------|------|
| `LogFileTest.SwitchFile` | 切换文件后旧文件不再增长 |
| `LogFileTest.CloseStopsOutput` | close 后日志不再写入文件 |
| `LogFileTest.GetFilePath` | get_file 返回正确路径 |

#### 边界条件

| 用例 | 描述 |
|------|------|
| `LogFileTest.NullAndEmptySafety` | NULL/空字符串/双重 close 安全 |
| `LogFileTest.InvalidPath` | 无效路径返回 FALSE |
| `LogFileTest.TimestampFormat` | 时间戳格式 YYYY-MM-DD HH:MM:SS.mmm |

#### 回调

| 用例 | 描述 |
|------|------|
| `LogFileTest.FileWithCallback` | 文件 + 自定义回调共存 |
| `LogFileTest.GetSetCallback` | 回调注册/查询往返 |

#### 等级 API

| 用例 | 描述 |
|------|------|
| `LogFileTest.LevelName` | 8 种等级名称 + UNKNOWN |
| `LogFileTest.GetSetLevelRoundtrip` | set_level / get_level 往返验证 |

### 3.3 test_concurrent.cc — 并发 (12 用例)

被测文件: `modules/treelock_core/src/client.c`, `lock_table.c`, `modules/treelock_tree/src/tree_api.c` (部分)

#### 基础并发

| 用例 | 线程 | 操作 | 描述 |
|------|------|------|------|
| `MultiClientLockUnlock` | 8 | 1000 ops/thread | 多客户端 IS/IX/X try_lock/unlock |
| `SharedInstanceConsistency` | 8 | 1000 ops/thread | 共享 treelock_t 实例并发 lock/unlock |
| `TryLockTimeout` | 1 | — | try_lock 立即获取 + 重入锁耗时 |
| `ReentrantLock` | 1 | — | 同客户端同模式 3 次 lock → ref_count 验证 |
| `ReentrantLockUpgrade` | 1 | — | IS → escalate S → re-entrant S → 释放 |

#### 升级/降级并发

| 用例 | 线程 | 操作 | 描述 |
|------|------|------|------|
| `LockEscalate` | 4 | 100 ops/thread | 并发 IS → S 升级 |
| `LockDowngrade` | 4 | 100 ops/thread | 并发 X → IS 降级 |

#### 批量操作

| 用例 | 线程 | 操作 | 描述 |
|------|------|------|------|
| `UnlockAll` | 4 | 50 ops/thread | 并发批量加锁 → unlock_all |
| `QueryDuringConcurrent` | 4 | 500 ops/thread | 并发中 query_node + get_mode |

#### 等待队列与生命周期

| 用例 | 线程 | 操作 | 描述 |
|------|------|------|------|
| `MultipleWaitersSameNode` | 8 | — | 同节点 8 waiter 竞争 X 锁 + FIFO 唤醒 |
| `WaitQueueChurn` | 5 | ~800ms | 高频等待队列进出 (swap-remove + cond 销毁压力) |
| `MultiInstanceCreateDestroy` | 6 | 30 cycles | 并发 create→load→use→destroy (destroy 屏障 + 树清理) |

### 3.4 test_tree.cc — 树管理 (29 用例)

被测文件: `modules/treelock_tree/src/tree_*.c` (5 files)

#### 手动注册

| 用例 | 描述 |
|------|------|
| `ManualRegisterNode` | 注册根节点 + 子节点 + 重复 ID 拒绝 + parent 查询 |
| `RegisterNodeEdgeCases` | ID=0 拒绝 / 根重复 / 父不存在 / 重复 ID |
| `SingleNodeTree` | 单节点树 (仅根) lock/unlock |

#### JSON 加载

| 用例 | 描述 |
|------|------|
| `FlatJson` | 扁平格式解析 + path lookup + 无效路径 |
| `NestedJson` | 嵌套格式递归解析 + path lookup + 叶子节点 |
| `InvalidJsonSyntax` | 裸数字 / 缺括号 / 空对象 / 空字符串 / NULL |
| `InvalidJsonEmptyNodes` | 空节点数组 `{"nodes":[]}` |
| `InvalidJsonMissingId` | 节点缺少必须的 id 字段 |
| `InvalidJsonSelfParent` | 自引用 parent==id → 环检测 |
| `ReloadTree` | 加载新树覆盖旧树（旧树清理 + 新路径生效） |

#### 结构校验

| 用例 | 描述 |
|------|------|
| `ValidateDuplicateId` | 重复 ID 拒绝 |
| `ValidateMultiRoot` | 多根节点拒绝 |
| `ValidateCycle` | A→B→C→A 环路检测 |

#### 路径加锁/解锁

| 用例 | 描述 |
|------|------|
| `LockPathAndUnlockPath` | IX→IX→X 全路径锁 + 逆序释放 |
| `LockPathISMode` | S 目标 → 祖先用 IS (非 IX) |
| `LockPathInvalidPath` | 树未加载 / 空路径 / NULL / 不存在段 / NL mode |
| `LockPathRootOnly` | lock_path("/") 仅锁根节点 |
| `UnlockPathEdgeCases` | 树未加载 / 空路径 / NULL / 未持有锁 |

#### 协议强制

| 用例 | 描述 |
|------|------|
| `ProtocolEnforcement` | 无父锁→协议违规 / IS父→S子 通过 / IX父→X子 通过 |

#### 查询

| 用例 | 描述 |
|------|------|
| `AncestorModeForAllModes` | IS/S→IS, IX/SIX/X→IX, NL→NL, 非法→NL |
| `LookupPathEdgeCases` | 树未加载 / NULL path / NULL output |
| `GetParentEdgeCases` | NULL / 树未加载 / 不存在节点 / 根→0 |
| `BackwardCompatibility` | 无树时可任意 lock (向后兼容) |
| `TreeNotLoadedAfterDestroy` | destroy 后 tree_loaded(nullptr) = FALSE |

#### 树卸载与生命周期 (v0.3.0 新增)

| 用例 | 描述 |
|------|------|
| `UnloadTreeBasic` | unload 后 tree_loaded→FALSE + lock 向后兼容 + lock_path 失败 |
| `UnloadTreeSafety` | NULL/未加载/双重卸载 安全 |
| `UnloadAndReload` | 卸载→重新加载(验证 hash 桶遍历释放 + 新树生效) |
| `RepeatedCreateLoadDestroy` | 20 次完整生命周期(验证 #2 树索引泄露已修复) |
| `UnloadThenManualRegister` | 卸载后手动注册新树(懒初始化恢复 + 协议校验) |

## 4. 测试规范

### 4.1 API 测试规则

1. **每一个 `treelock_*.h` 中的公开 API 必须有专属测试用例**：直接调用该 API 并验证返回值/副作用。

2. **API 之间的协作链必须有关联测试用例**：
   - 示例：`lock_path` 依赖 `resolve_path` + `ancestor_mode_for` + `lock` + `validate_protocol`，因此 `LockPathAndUnlockPath` 测试了整条链。
   - 示例：`UnloadAndReload` 测试 `treelock_tree_unload` + `treelock_load_tree_from_string` 的互操作。

3. **新增模块测试密度 ≥ 15 用例/KLOC**（见第 2 节表格）。

### 4.2 测试注释规范

每个 `TEST()` 上方必须有 `/** … */` 块注释：

```
/**
 * 测试目标: What is being verified
 *
 * 运行路径:
 *   function1() → function2() → function3()
 *
 * 覆盖: file.c — function_name()
 */
TEST(Suite, Name) { ... }
```

### 4.3 文档同步

修改测试文件后，必须同步更新本文件 (`docs/TEST_STRATEGY.md`) 中的：
- 用例清单（添加/删除/改名）
- 各模块用例计数
- 测试密度表

## 5. 已知限制与未来规划

### Phase 1 限制

- **锁表按实例隔离**: 每个 `treelock_t` 持有独立锁表，不同实例间无真正锁竞争
- **单客户端 ID**: 每个实例只有一个 `client_id`，无法模拟多客户端共享同一锁表
- **跨客户端超时测试**: 需 Phase 2 server 模式才能验证真正的跨客户端锁竞争 + 超时

### 待补充用例

| 优先级 | 模块 | 缺口 | 目标数 |
|--------|------|------|--------|
| P1 | `treelock_core` | +14 用例 | 45 (15/KLOC) |
| P1 | `treelock_tree` | +12 用例 | 41 (15/KLOC) |
| P2 | 压力 | 大规模树 (1000+ 节点) 加载 + 路径遍历性能 |
| P2 | 故障注入 | OOM 模拟、mutex/cond 初始化失败 |
| P3 | 分布式 | 多节点 server 通信、租约过期、心跳 |

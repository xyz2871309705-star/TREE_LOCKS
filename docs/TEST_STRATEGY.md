# TreeLocks 测试策略与用例文档

版本: 0.4.0 | 日期: 2026-06-14

## 1. 测试架构

```
┌──────────────────────────────────────────────────────────┐
│  E2E / 集成测试 (Phase 2/3: 多机分布式场景)               │
├──────────────────────────────────────────────────────────┤
│  test_concurrent.cc — 并发 + API 全覆盖 (27 用例)         │
│  (多线程 lock/unlock + escalate/downgrade/query          │
│   + 数组扩展 + 升降级链 + destroy 屏障)                   │
├──────────────────────────────────────────────────────────┤
│  test_tree.cc       — 树管理 + 协议校验 (41 用例)         │
│  (JSON/文件加载→校验→锁路径 + unload/reload              │
│   + 生命周期 + 路径边界 + 大规模/深度嵌套)                 │
├──────────────────────────────────────────────────────────┤
│  test_protocol.cc   — 协议 + 压力/死锁/内存 (30 用例)     │
│  (兼容矩阵/升级降级 + 100万次压力 + 死锁预防 + 内存回收)   │
├──────────────────────────────────────────────────────────┤
│  test_log.cc        — 日志模块 (15 用例)                  │
│  (文件输出/等级过滤/回调/切换)                             │
└──────────────────────────────────────────────────────────┘
```

## 2. 模块测试密度

| 模块 | 源文件 | 行数 | 测试文件 | 用例 | 密度/KLOC | 目标 | 状态 |
|------|--------|------|---------|------|-----------|------|------|
| `treelock_log` | `log_core.c` | ~1.0K | `test_log.cc` | 15 | 15.0 | ≥15 | ✅ |
| `treelock_core/protocol` | `protocol.c` | ~0.3K | `test_protocol.cc` | 30 | — | — | ✅ |
| `treelock_core/client` | `client.c` | ~1.1K | `test_concurrent.cc` | 27 | — | — | ✅ |
| `treelock_core/lock_table` | `lock_table.c` | ~0.5K | `test_concurrent.cc` | — | — | — | ✅ |
| `treelock_core` (合计) | — | ~3.0K | 2 files | 57 | **19.0** | ≥45 | ✅ |
| `treelock_tree` | `tree_*.c` (5 files) | ~2.7K | `test_tree.cc` | 41 | **15.2** | ≥41 | ✅ |
| **总计** | | ~6.7K | 4 files | **113** | 16.9 | ≥100 | ✅ |

## 3. 测试用例清单

### 3.1 test_protocol.cc — 协议 + 压力 + 死锁 + 内存 (30 用例)

被测文件: `modules/treelock_core/src/protocol.c`, `client.c`, `lock_table.c`

#### 协议纯逻辑 — CompatMatrix (7)

| 用例 | 描述 |
|------|------|
| `CompatMatrix.NL_CompatibleWithAll` | NL 与所有模式兼容 |
| `CompatMatrix.X_OnlyCompatibleWithNL` | X 只与 NL 兼容 |
| `CompatMatrix.IS_CompatibleWithAllExceptX` | IS 只与 X 不兼容 |
| `CompatMatrix.IX_OnlyCompatibleWithNL_IS_IX` | IX 只与 NL/IS/IX 兼容 |
| `CompatMatrix.S_OnlyCompatibleWithNL_IS_S` | S 只与 NL/IS/S 兼容 |
| `CompatMatrix.SIX_OnlyCompatibleWithNL_IS` | SIX 只与 NL/IS 兼容 |
| `CompatMatrix.FullMatrix` | 遍历全部 36 个组合对照标准矩阵 |

#### 协议纯逻辑 — RequiredParentMode (1)

| 用例 | 描述 |
|------|------|
| `RequiredParentMode.AllModes` | 6 种请求模式对应的父节点最小锁模式 |

#### 协议纯逻辑 — Boundary (4)

| 用例 | 描述 |
|------|------|
| `Boundary.ModeCompatibleOutOfRange` | mode > MAX 返回 FALSE |
| `Boundary.RequiredParentModeOutOfRange` | mode > MAX 返回 NL |
| `Boundary.EscalateValidOutOfRange` | 越界参数返回 FALSE |
| `Boundary.DowngradeValidOutOfRange` | 越界参数返回 FALSE |

#### 协议纯逻辑 — Escalate/Downgrade Paths (4)

| 用例 | 描述 |
|------|------|
| `EscalatePaths.Valid` | 全部合法升级路径 |
| `EscalatePaths.Invalid` | 非法升级 (向下/相同/越级) |
| `DowngradePaths.Valid` | 全部合法降级路径 |
| `DowngradePaths.Invalid` | 非法降级 (向上/相同) |

#### 协议纯逻辑 — Utils (3)

| 用例 | 描述 |
|------|------|
| `Utils.ModeName` | 6 种模式名称 + 非法→UNKNOWN |
| `Utils.StrError` | 7 种错误码返回非空描述 |
| `Utils.StrErrorUnknownCode` | 未知错误码→"Unknown error" |

#### 压力测试 — StressTest (4) ⭐ v0.4.0

| 用例 | 规模 | 描述 |
|------|------|------|
| `StressTest.RandomCompatMatrix1M` | 100 万次 | 随机兼容矩阵查询，每 10 万次抽样验证 |
| `StressTest.EscalateDowngradeCycle1K` | 1000 循环 | IS→IX→SIX→X→SIX→IS 循环，验证状态一致性 |
| `StressTest.MultiNodeHeavyLockUnlock` | 50 节点×100 轮 | 多种模式的批量 lock/unlock |
| `StressTest.WaitQueueHighChurn` | 1 holder+6 waiter, 1.5s | 高频等待队列进出，验证 swap-remove |

#### 死锁测试 — DeadlockTest (4) ⭐ v0.4.0

| 用例 | 线程 | 描述 |
|------|------|------|
| `DeadlockTest.ProtocolPreventsLockOrderingDeadlock` | 1 | 自底向上 lock 被协议校验阻止 |
| `DeadlockTest.SharedInstanceNoDeadlock` | 8 | 共享实例 500 ops/thread，全部完成=无死锁 |
| `DeadlockTest.EscalateConcurrentNoDeadlock` | 6 | 并发 escalate 300 ops/thread |
| `DeadlockTest.CrossSubtreeNoDeadlock` | 2 | lock_path 不同子树 50 轮，共享根节点 |

#### 内存测试 — MemoryTest (3) ⭐ v0.4.0

| 用例 | 规模 | 描述 |
|------|------|------|
| `MemoryTest.RepeatedCreateLockDestroy100` | 100 周期 | create→load→lock→unlock→destroy，验证树索引+锁表+mutex 全量释放 |
| `MemoryTest.GrantWaiterArrayRecycle` | 200 周期 | grant 数组扩容回缩 + waiter cond 进出 |
| `MemoryTest.ConcurrentCreateDestroy` | 6 线程×50 周期 | 并发 create/lock/destroy，互不干扰 |

---

### 3.2 test_log.cc — 日志模块 (15 用例)

被测文件: `modules/treelock_log/src/log_core.c`

| 类别 | 用例 | 描述 |
|------|------|------|
| 文件输出 | `BasicFileOutput` | 写文件 + 内容验证 |
| | `NoAnsiCodes` | 文件中不含 ANSI 颜色码 |
| | `AllLevels` | 6 种等级全部输出 |
| | `RespectsLogLevel` | WARN 过滤 INFO/DEBUG |
| | `WriteIntegrity` | 连续 50 条日志完整性 |
| 文件切换 | `SwitchFile` | 切换后旧文件不再增长 |
| | `CloseStopsOutput` | close 后不再写入 |
| | `GetFilePath` | 路径查询往返 |
| 边界 | `NullAndEmptySafety` | NULL/空字符串/双重 close |
| | `InvalidPath` | 无效路径返回 FALSE |
| | `TimestampFormat` | `YYYY-MM-DD HH:MM:SS.mmm` |
| 回调 | `FileWithCallback` | 文件+回调共存 |
| | `GetSetCallback` | 回调注册/查询往返 |
| 等级API | `LevelName` | 8 种等级名称 + UNKNOWN |
| | `GetSetLevelRoundtrip` | set/get 往返验证 |

---

### 3.3 test_concurrent.cc — 并发 + API 全覆盖 (27 用例)

被测文件: `modules/treelock_core/src/client.c`, `lock_table.c`

#### 基础并发 (5)

| 用例 | 线程 | 操作 | 描述 |
|------|------|------|------|
| `MultiClientLockUnlock` | 8 | 1000 ops/thread | 多客户端 IS/IX/X lock/unlock |
| `SharedInstanceConsistency` | 8 | 1000 ops/thread | 共享 treelock_t 并发一致性 |
| `TryLockTimeout` | 1 | — | 锁可用立即获取 + 重入耗时 |
| `ReentrantLock` | 1 | — | 3 次重入 lock → ref_count |
| `ReentrantLockUpgrade` | 1 | — | IS→escalate S→re-entrant S |

#### 升级/降级并发 (2)

| 用例 | 线程 | 操作 | 描述 |
|------|------|------|------|
| `LockEscalate` | 4 | 100 ops/thread | 并发 IS→S 升级 |
| `LockDowngrade` | 4 | 100 ops/thread | 并发 X→IS 降级 |

#### 批量操作 (2)

| 用例 | 线程 | 操作 | 描述 |
|------|------|------|------|
| `UnlockAll` | 4 | 50 ops | 并发批量加锁→unlock_all |
| `QueryDuringConcurrent` | 4 | 500 ops | 并发 query_node+get_mode |

#### 等待队列与生命周期 (3)

| 用例 | 线程 | 操作 | 描述 |
|------|------|------|------|
| `MultipleWaitersSameNode` | 8 | — | 同节点 8 waiter 竞争 X + FIFO 唤醒 |
| `WaitQueueChurn` | 5 | ~800ms | 高频进出 swap-remove + cond 销毁压力 |
| `MultiInstanceCreateDestroy` | 6 | 30 cycles | create→load→use→destroy 周期 |

#### API 全覆盖 (15) ⭐

| 用例 | 描述 |
|------|------|
| `CreateWithConfig` | 自定义/默认 config 创建实例 |
| `DestroyedInstanceRejected` | destroy 释放所有锁不崩溃 |
| `LockInvalidParams` | NULL/mode/timeout 参数校验 |
| `UnlockErrorPaths` | 未持有/重复释放/NULL 错误 |
| `EscalateErrorPaths` | 未持有/非法路径/相同模式 |
| `DowngradeErrorPaths` | 未持有/非法路径/相同模式 |
| `QueryApiErrors` | NULL/不存在/JSON 格式验证 |
| `MultiStepUpgradeChains` | 3 条完整升级链 |
| `MultiStepDowngradeChains` | 2 条完整降级链 |
| `GrantArrayExpansion` | grant 数组 0→4→8→16 扩展 |
| `HeldArrayExpansion` | 20 节点触发 held 8→16 扩展 |
| `UnlockAllMassRelease` | 30 锁 unlock_all 验证全 NL |
| `SetLostCallback` | 回调设置/清除/NULL 守卫 |
| `TryLockShortTimeout` | 50 次短超时统计 |
| `EscalateThenReentrant` | escalate 后重入锁 ref_count |

---

### 3.4 test_tree.cc — 树管理 (41 用例)

被测文件: `modules/treelock_tree/src/tree_*.c` (5 files)

#### 手动注册 (3)

| 用例 | 描述 |
|------|------|
| `ManualRegisterNode` | 根+子节点注册，重复ID拒绝，parent查询 |
| `RegisterNodeEdgeCases` | ID=0/根重复/父不存在/ID重复 |
| `SingleNodeTree` | 单节点 lock/unlock |

#### JSON 加载 (7)

| 用例 | 描述 |
|------|------|
| `FlatJson` | 扁平格式解析 + path lookup |
| `NestedJson` | 嵌套格式递归解析 + path lookup |
| `InvalidJsonSyntax` | 裸数字/缺括号/空对象/空字符串/NULL |
| `InvalidJsonEmptyNodes` | `{"nodes":[]}` |
| `InvalidJsonMissingId` | 缺少 id 字段 |
| `InvalidJsonSelfParent` | 自引用 parent==id |
| `ReloadTree` | 覆盖旧树 + 旧路径失效 |

#### 结构校验 (3)

| 用例 | 描述 |
|------|------|
| `ValidateDuplicateId` | 重复 ID 拒绝 |
| `ValidateMultiRoot` | 多根拒绝 |
| `ValidateCycle` | 环路检测 |

#### 路径加锁/解锁 (5)

| 用例 | 描述 |
|------|------|
| `LockPathAndUnlockPath` | IX→IX→X 全路径 + 逆序释放 |
| `LockPathISMode` | S 目标→祖先 IS |
| `LockPathInvalidPath` | 树未加载/空/NULL/不存在/NL |
| `LockPathRootOnly` | lock_path("/") 仅根 |
| `UnlockPathEdgeCases` | 树未加载/空/未持有 |

#### 协议强制 (1)

| 用例 | 描述 |
|------|------|
| `ProtocolEnforcement` | 无父锁→违规 / 正确顺序通过 |

#### 查询 (5)

| 用例 | 描述 |
|------|------|
| `AncestorModeForAllModes` | 全部模式祖先推导 |
| `LookupPathEdgeCases` | NULL/NULL output |
| `GetParentEdgeCases` | NULL/不存在/根→0 |
| `BackwardCompatibility` | 无树可任意 lock |
| `TreeNotLoadedAfterDestroy` | destroy 后安全返回 |

#### 树卸载与生命周期 (5) ⭐

| 用例 | 描述 |
|------|------|
| `UnloadTreeBasic` | unload 后状态清除 + lock 恢复 |
| `UnloadTreeSafety` | NULL/重复卸载安全 |
| `UnloadAndReload` | 卸载→重载新树 |
| `RepeatedCreateLoadDestroy` | 20 次完整生命周期 |
| `UnloadThenManualRegister` | 卸载后手动注册新树 |

#### 边界与大规模 (12) ⭐

| 用例 | 规模 | 描述 |
|------|------|------|
| `LoadTreeFromFile` | 3 节点 | 文件 I/O 加载 + 不存在文件错误 |
| `DeepNestedJson` | 10 层 | 深度嵌套 JSON + 长路径 lock_path |
| `LargeFlatJson` | 100 节点 | 批量解析/校验/注册/路径查询 |
| `ResolvePathEdgeCases` | 3 节点 | 尾随/连续斜杠 + root label 匹配 |
| `LockPathRollback` | 3 节点 | 中间失败回滚验证 |
| `LockPathIXSIXMode` | 3 节点 | IX/SIX 目标祖先模式 |
| `ValidateZeroId` | — | node_id=0 拒绝 |
| `ValidateParentNotExist` | — | 无效 parent_id 拒绝 |
| `NoLabelNodes` | — | NULL/空 label 注册+查询 |
| `ReloadSwitchesProtocolScope` | — | reload 后协议作用域切换 |
| `UnloadPreservesExistingLocks` | — | unload 后已有锁仍可释放 |
| `LoadTreeFromFileErrors` | — | NULL/不存在/损坏文件 |

## 4. 测试规范

### 4.1 API 测试规则 (来自 CLAUDE.md)

1. **每个对外 API 必须有专属测试**
2. **API 间协作必须有关联测试**
3. **新增模块 ≥ 15 用例/KLOC**
4. **测试注释规范** (目标/路径/覆盖)
5. **修改测试同步更新本文档**

### 4.2 测试准则

- **压力测试**: 操作量 ≥ 1000 次或持续 ≥ 1 秒，验证无崩溃 + 结果一致性
- **死锁测试**: 多线程并发锁操作，所有线程在超时(5s)内完成 = 无死锁
- **内存测试**: 重复 create/destroy 周期 ≥ 100 次，验证无 OOM/崩溃

## 5. 已知限制与未来规划

### Phase 1 当前覆盖状态

| 类别 | 状态 | 说明 |
|------|------|------|
| 协议纯逻辑 | ✅ 100% | 兼容矩阵、升级降级全路径 |
| API 参数校验 | ✅ 覆盖 | NULL/destroyed/非法参数 |
| 并发安全性 | ✅ 覆盖 | 8线程共享实例、死锁预防 |
| 内存管理 | ✅ 覆盖 | 100周期 create/destroy、数组回缩 |
| 压力 | ✅ 覆盖 | 1M查询、1K升级循环、1.5s队列churn |
| 树加载 | ✅ 覆盖 | 文件/字符串、平面/嵌套、10层深度、100节点 |
| 路径操作 | ✅ 覆盖 | 回滚、边界斜杠、无标签节点 |
| 生命周期 | ✅ 覆盖 | create→load→use→unload→destroy |

### Phase 1 限制

- **锁表按实例隔离**: `treelock_t` 持有独立锁表，跨实例无真正锁竞争
- **单客户端 ID**: 无法模拟多客户端共享同一锁表
- **跨客户端冲突**: 需 Phase 2 server 模式

### 待补充 (P3)

| 类别 | 内容 |
|------|------|
| 超大规模 | 1000+ 节点树加载 + 路径遍历性能 |
| 故障注入 | OOM 模拟 (malloc/realloc 失败) |
| 分布式 | 多节点 server 通信、租约过期、心跳 |
| 长时间运行 | 24h+ soak test 检测慢泄露 |

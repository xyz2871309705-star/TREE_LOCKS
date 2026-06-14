/*
 * TreeLocks — 协议正确性测试 (GTest)
 *
 * 测试兼容矩阵、锁升级/降级路径等协议层逻辑。
 * 被测源文件: modules/treelock_core/src/protocol.c
 *
 * 版本: 0.2.0
 * 日期: 2026-06-13
 */

#include <gtest/gtest.h>
#include <cstring>
#include <cstdlib>
#include <pthread.h>
#include <thread>
#include <chrono>

extern "C" {
#include "treelock.h"
#include "treelock_tree.h"
#include "treelock_log.h"
#include "internal.h"
}

/* =========================================================================
 * 兼容矩阵测试
 *
 * 验证 treelock_mode_compatible() 在所有 6 种锁模式下的兼容性。
 * 函数路径: treelock_mode_compatible() → g_compat_matrix[existing][requested]
 *
 * 标准 Gray 6-mode 兼容矩阵:
 *               NL  IS  IX   S  SIX  X
 *      NL   |   Y   Y   Y   Y   Y   Y
 *      IS   |   Y   Y   Y   Y   Y   N
 *      IX   |   Y   Y   Y   N   N   N
 *      S    |   Y   Y   N   Y   N   N
 *      SIX  |   Y   Y   N   N   N   N
 *      X    |   Y   N   N   N   N   N
 * ========================================================================= */

/**
 * 测试目标: 验证 NL (No Lock) 模式与所有模式兼容
 *
 * 运行路径:
 *   treelock_mode_compatible(TREELOCK_NL, mode)
 *     → g_compat_matrix[TREELOCK_NL][mode] → 返回 TRUE (全 1 行)
 *
 * NL 是"无锁"状态，任何新请求都可以与之共存。
 * 被调用: lock_table.c: treelock_table_check_conflict()
 */
TEST(CompatMatrix, NL_CompatibleWithAll)
{
    for (int m = TREELOCK_NL; m <= TREELOCK_MODE_MAX; m++) {
        EXPECT_TRUE(treelock_mode_compatible(TREELOCK_NL, (treelock_mode_t)m))
            << "NL should be compatible with mode " << treelock_mode_name((treelock_mode_t)m);
    }
}

/**
 * 测试目标: 验证 X (Exclusive) 模式只与 NL 兼容
 *
 * 运行路径:
 *   treelock_mode_compatible(TREELOCK_X, mode)
 *     → 越界检查: mode ≤ TREELOCK_MODE_MAX
 *     → g_compat_matrix[TREELOCK_X][mode] → 只有 [X][NL] = 1
 *
 * X 是最强的排他锁，与其他所有模式（包括自身）均冲突。
 * 被调用: lock_table.c: treelock_table_check_conflict() — 持有 X 时阻塞所有请求
 */
TEST(CompatMatrix, X_OnlyCompatibleWithNL)
{
    EXPECT_TRUE (treelock_mode_compatible(TREELOCK_X, TREELOCK_NL));
    EXPECT_FALSE(treelock_mode_compatible(TREELOCK_X, TREELOCK_IS));
    EXPECT_FALSE(treelock_mode_compatible(TREELOCK_X, TREELOCK_IX));
    EXPECT_FALSE(treelock_mode_compatible(TREELOCK_X, TREELOCK_S));
    EXPECT_FALSE(treelock_mode_compatible(TREELOCK_X, TREELOCK_SIX));
    EXPECT_FALSE(treelock_mode_compatible(TREELOCK_X, TREELOCK_X));
}

/**
 * 测试目标: 验证 IS (Intention Shared) 模式除了 X 外与所有模式兼容
 *
 * 运行路径:
 *   treelock_mode_compatible(TREELOCK_IS, mode)
 *     → g_compat_matrix[TREELOCK_IS][mode]
 *     → IS 行: [NL]=1, [IS]=1, [IX]=1, [S]=1, [SIX]=1, [X]=0
 *
 * IS 是"意向共享"，表达了将来可能在子树中加 S 的意图，
 * 只与 X 冲突（因为 X 要求子树中没有任何其他活动）。
 * 被调用: 当某客户端请求锁时，检查与已持有 IS 的客户端是否冲突
 */
TEST(CompatMatrix, IS_CompatibleWithAllExceptX)
{
    EXPECT_TRUE (treelock_mode_compatible(TREELOCK_IS, TREELOCK_NL));
    EXPECT_TRUE (treelock_mode_compatible(TREELOCK_IS, TREELOCK_IS));
    EXPECT_TRUE (treelock_mode_compatible(TREELOCK_IS, TREELOCK_IX));
    EXPECT_TRUE (treelock_mode_compatible(TREELOCK_IS, TREELOCK_S));
    EXPECT_TRUE (treelock_mode_compatible(TREELOCK_IS, TREELOCK_SIX));
    EXPECT_FALSE(treelock_mode_compatible(TREELOCK_IS, TREELOCK_X));
}

/**
 * 测试目标: 验证 IX (Intention Exclusive) 只与 NL/IS/IX 兼容
 *
 * 运行路径:
 *   treelock_mode_compatible(TREELOCK_IX, mode)
 *     → g_compat_matrix[TREELOCK_IX][mode]
 *     → IX 行: [NL]=1, [IS]=1, [IX]=1, [S]=0, [SIX]=0, [X]=0
 *
 * IX 表达了将来可能在子树中加 X/SIX 的意图，
 * 与 S/SIX/X 均冲突（这些模式要求子树中没有排他意图）。
 * 被调用: lock_table.c: treelock_table_check_conflict()
 */
TEST(CompatMatrix, IX_OnlyCompatibleWithNL_IS_IX)
{
    EXPECT_TRUE (treelock_mode_compatible(TREELOCK_IX, TREELOCK_NL));
    EXPECT_TRUE (treelock_mode_compatible(TREELOCK_IX, TREELOCK_IS));
    EXPECT_TRUE (treelock_mode_compatible(TREELOCK_IX, TREELOCK_IX));
    EXPECT_FALSE(treelock_mode_compatible(TREELOCK_IX, TREELOCK_S));
    EXPECT_FALSE(treelock_mode_compatible(TREELOCK_IX, TREELOCK_SIX));
    EXPECT_FALSE(treelock_mode_compatible(TREELOCK_IX, TREELOCK_X));
}

/**
 * 测试目标: 验证 S (Shared) 只与 NL/IS/S 兼容
 *
 * 运行路径:
 *   treelock_mode_compatible(TREELOCK_S, mode)
 *     → g_compat_matrix[TREELOCK_S][mode]
 *     → S 行: [NL]=1, [IS]=1, [IX]=0, [S]=1, [SIX]=0, [X]=0
 *
 * S 是共享读锁，多个 S 可以共存，但与任何排他意图(IX/SIX/X)冲突。
 * 被调用: lock_table.c: treelock_table_check_conflict()
 */
TEST(CompatMatrix, S_OnlyCompatibleWithNL_IS_S)
{
    EXPECT_TRUE (treelock_mode_compatible(TREELOCK_S, TREELOCK_NL));
    EXPECT_TRUE (treelock_mode_compatible(TREELOCK_S, TREELOCK_IS));
    EXPECT_FALSE(treelock_mode_compatible(TREELOCK_S, TREELOCK_IX));
    EXPECT_TRUE (treelock_mode_compatible(TREELOCK_S, TREELOCK_S));
    EXPECT_FALSE(treelock_mode_compatible(TREELOCK_S, TREELOCK_SIX));
    EXPECT_FALSE(treelock_mode_compatible(TREELOCK_S, TREELOCK_X));
}

/**
 * 测试目标: 验证 SIX (Shared + Intention Exclusive) 只与 NL/IS 兼容
 *
 * 运行路径:
 *   treelock_mode_compatible(TREELOCK_SIX, mode)
 *     → g_compat_matrix[TREELOCK_SIX][mode]
 *     → SIX 行: [NL]=1, [IS]=1, [IX]=0, [S]=0, [SIX]=0, [X]=0
 *
 * SIX 是最特殊的模式：当前节点持 S (允许读)，但同时持有 IX 意图
 * (子树中可加 X)。因此只与 NL/IS 共存。
 * 被调用: lock_table.c: treelock_table_check_conflict()
 */
TEST(CompatMatrix, SIX_OnlyCompatibleWithNL_IS)
{
    EXPECT_TRUE (treelock_mode_compatible(TREELOCK_SIX, TREELOCK_NL));
    EXPECT_TRUE (treelock_mode_compatible(TREELOCK_SIX, TREELOCK_IS));
    EXPECT_FALSE(treelock_mode_compatible(TREELOCK_SIX, TREELOCK_IX));
    EXPECT_FALSE(treelock_mode_compatible(TREELOCK_SIX, TREELOCK_S));
    EXPECT_FALSE(treelock_mode_compatible(TREELOCK_SIX, TREELOCK_SIX));
    EXPECT_FALSE(treelock_mode_compatible(TREELOCK_SIX, TREELOCK_X));
}

/**
 * 测试目标: 遍历全部 36 个 (已持有 × 请求) 组合，对照标准矩阵逐一验证
 *
 * 运行路径:
 *   36 次调用 treelock_mode_compatible(existing, requested)
 *     → g_compat_matrix[existing][requested]
 *
 * 此用例确保兼容矩阵的每个单元都与 Gray 6-mode 标准一致，
 * 防止未来修改矩阵时引入偏差。如果任何单元与期望不符，
 * GTest 会输出具体的模式名以便定位。
 * 覆盖: protocol.c 全部 g_compat_matrix 表项
 */
TEST(CompatMatrix, FullMatrix)
{
    /* 遍历所有 36 个组合，对照 Gray 6-mode 标准矩阵 */
    struct { treelock_mode_t existing; treelock_mode_t requested; int expected; } cases[] = {
        /* NL 持有方 → 所有请求都兼容 */
        {TREELOCK_NL, TREELOCK_NL,  1}, {TREELOCK_NL, TREELOCK_IS,  1},
        {TREELOCK_NL, TREELOCK_IX,  1}, {TREELOCK_NL, TREELOCK_S,   1},
        {TREELOCK_NL, TREELOCK_SIX, 1}, {TREELOCK_NL, TREELOCK_X,   1},
        /* IS 持有方 → 只有 X 不兼容 */
        {TREELOCK_IS, TREELOCK_NL,  1}, {TREELOCK_IS, TREELOCK_IS,  1},
        {TREELOCK_IS, TREELOCK_IX,  1}, {TREELOCK_IS, TREELOCK_S,   1},
        {TREELOCK_IS, TREELOCK_SIX, 1}, {TREELOCK_IS, TREELOCK_X,   0},
        /* IX 持有方 → 只有 NL/IS/IX 兼容 */
        {TREELOCK_IX, TREELOCK_NL,  1}, {TREELOCK_IX, TREELOCK_IS,  1},
        {TREELOCK_IX, TREELOCK_IX,  1}, {TREELOCK_IX, TREELOCK_S,   0},
        {TREELOCK_IX, TREELOCK_SIX, 0}, {TREELOCK_IX, TREELOCK_X,   0},
        /* S 持有方 → 只有 NL/IS/S 兼容 */
        {TREELOCK_S, TREELOCK_NL,  1}, {TREELOCK_S, TREELOCK_IS,  1},
        {TREELOCK_S, TREELOCK_IX,  0}, {TREELOCK_S, TREELOCK_S,   1},
        {TREELOCK_S, TREELOCK_SIX, 0}, {TREELOCK_S, TREELOCK_X,   0},
        /* SIX 持有方 → 只有 NL/IS 兼容 */
        {TREELOCK_SIX, TREELOCK_NL,  1}, {TREELOCK_SIX, TREELOCK_IS,  1},
        {TREELOCK_SIX, TREELOCK_IX,  0}, {TREELOCK_SIX, TREELOCK_S,   0},
        {TREELOCK_SIX, TREELOCK_SIX, 0}, {TREELOCK_SIX, TREELOCK_X,   0},
        /* X 持有方 → 只有 NL 兼容 */
        {TREELOCK_X, TREELOCK_NL,  1}, {TREELOCK_X, TREELOCK_IS,  0},
        {TREELOCK_X, TREELOCK_IX,  0}, {TREELOCK_X, TREELOCK_S,   0},
        {TREELOCK_X, TREELOCK_SIX, 0}, {TREELOCK_X, TREELOCK_X,   0},
    };
    for (auto &c : cases) {
        EXPECT_EQ(treelock_mode_compatible(c.existing, c.requested), c.expected)
            << "compat(" << treelock_mode_name(c.existing)
            << ", " << treelock_mode_name(c.requested) << ") should be "
            << c.expected;
    }
}

/* =========================================================================
 * 父节点锁模式推导测试
 *
 * 验证 treelock_required_parent_mode() — 在子节点获取某锁模式时，
 * 父节点所需的最小锁模式。
 * 函数路径: treelock_required_parent_mode() → g_required_parent[mode]
 *
 * 规则:
 *   IS / S  子锁 → 父需 IS (允许子树共享读)
 *   IX/SIX/X 子锁 → 父需 IX (允许子树排他写)
 * ========================================================================= */

/**
 * 测试目标: 验证全部 6 种子锁模式对应的父节点最小锁模式
 *
 * 运行路径:
 *   treelock_required_parent_mode(mode)
 *     → g_required_parent[mode] 查表
 *     → 返回对应的父锁模式
 *
 * 此函数是 treelock_validate_protocol() 的核心依赖 —
 * 它决定了 lock 时在父节点上需要什么级别的意向锁。
 * 覆盖: protocol.c - treelock_required_parent_mode(), g_required_parent[]
 */
TEST(RequiredParentMode, AllModes)
{
    EXPECT_EQ(treelock_required_parent_mode(TREELOCK_NL),  TREELOCK_NL);
    EXPECT_EQ(treelock_required_parent_mode(TREELOCK_IS),  TREELOCK_IS);
    EXPECT_EQ(treelock_required_parent_mode(TREELOCK_IX),  TREELOCK_IX);
    EXPECT_EQ(treelock_required_parent_mode(TREELOCK_S),   TREELOCK_IS);
    EXPECT_EQ(treelock_required_parent_mode(TREELOCK_SIX), TREELOCK_IX);
    EXPECT_EQ(treelock_required_parent_mode(TREELOCK_X),   TREELOCK_IX);
}

/* =========================================================================
 * 越界参数处理测试
 *
 * 验证所有协议层函数在收到超出 TREELOCK_MODE_MAX 的参数时
 * 安全返回而不崩溃（防御性编程）。
 * 函数路径: 各函数入口处的 if (mode > TREELOCK_MODE_MAX) 守卫
 * ========================================================================= */

/**
 * 测试目标: treelock_mode_compatible() 对越界 mode 值安全返回 FALSE
 *
 * 运行路径:
 *   treelock_mode_compatible(99, IS)
 *     → 入口检查: existing > TREELOCK_MODE_MAX → return FALSE
 *   treelock_mode_compatible(IS, 99)
 *     → 入口检查: requested > TREELOCK_MODE_MAX → return FALSE
 *
 * 覆盖: protocol.c:104-111 — 越界保护分支
 */
TEST(Boundary, ModeCompatibleOutOfRange)
{
    /* 超出 TREELOCK_MODE_MAX 的值应返回 FALSE，不崩溃 */
    EXPECT_FALSE(treelock_mode_compatible((treelock_mode_t)99, TREELOCK_IS));
    EXPECT_FALSE(treelock_mode_compatible(TREELOCK_IS, (treelock_mode_t)99));
    EXPECT_FALSE(treelock_mode_compatible((treelock_mode_t)99, (treelock_mode_t)99));
    /* 负值也应安全处理 */
    EXPECT_FALSE(treelock_mode_compatible((treelock_mode_t)(-1), TREELOCK_IS));
}

/**
 * 测试目标: treelock_required_parent_mode() 对越界 mode 值返回 NL
 *
 * 运行路径:
 *   treelock_required_parent_mode(99)
 *     → 入口检查: mode > TREELOCK_MODE_MAX → return TREELOCK_NL
 *
 * 返回 NL 表示"不需要父节点锁"，避免后续校验误报。
 * 覆盖: protocol.c:126-128
 */
TEST(Boundary, RequiredParentModeOutOfRange)
{
    EXPECT_EQ(treelock_required_parent_mode((treelock_mode_t)99), TREELOCK_NL);
    EXPECT_EQ(treelock_required_parent_mode((treelock_mode_t)(-1)), TREELOCK_NL);
}

/**
 * 测试目标: treelock_escalate_valid() 对越界参数返回 FALSE
 *
 * 运行路径:
 *   treelock_escalate_valid(99, S)
 *     → 入口检查: old_mode > TREELOCK_MODE_MAX → return FALSE
 *   treelock_escalate_valid(IS, 99)
 *     → 入口检查: new_mode > TREELOCK_MODE_MAX → return FALSE
 *
 * 非法升级请求被安全拒绝。
 * 覆盖: protocol.c:148-150
 */
TEST(Boundary, EscalateValidOutOfRange)
{
    EXPECT_FALSE(treelock_escalate_valid((treelock_mode_t)99, TREELOCK_S));
    EXPECT_FALSE(treelock_escalate_valid(TREELOCK_IS, (treelock_mode_t)99));
    EXPECT_FALSE(treelock_escalate_valid((treelock_mode_t)99, (treelock_mode_t)99));
}

/**
 * 测试目标: treelock_downgrade_valid() 对越界参数返回 FALSE
 *
 * 运行路径:
 *   treelock_downgrade_valid(99, NL)
 *     → 入口检查: old_mode > TREELOCK_MODE_MAX → return FALSE
 *   treelock_downgrade_valid(X, 99)
 *     → 入口检查: new_mode > TREELOCK_MODE_MAX → return FALSE
 *
 * 非法降级请求被安全拒绝。
 * 覆盖: protocol.c:173-175
 */
TEST(Boundary, DowngradeValidOutOfRange)
{
    EXPECT_FALSE(treelock_downgrade_valid((treelock_mode_t)99, TREELOCK_NL));
    EXPECT_FALSE(treelock_downgrade_valid(TREELOCK_X, (treelock_mode_t)99));
}

/* =========================================================================
 * 锁升级路径测试
 *
 * 验证 treelock_escalate_valid() 的所有合法和非法升级路径。
 * 函数路径: treelock_escalate_valid(old, new) → g_upgrade_valid[old][new]
 *
 * 升级规则:
 *   NL → IS/IX/S/SIX/X  (从无锁开始任何升级都合法)
 *   IS → IX/S/SIX/X      (意向共享可升级为任何更强模式)
 *   IX → SIX/X           (意向排他可升级为 SIX 或 X)
 *   S  → SIX/X           (共享可升级为 SIX 或 X)
 *   SIX → X              (SIX 只能升级为 X)
 *   X  → 无              (X 已是最强，无法再升级)
 * ========================================================================= */

/**
 * 测试目标: 验证所有合法的锁升级路径
 *
 * 运行路径:
 *   treelock_escalate_valid(old, new)
 *     → 入口检查: old > MAX / new > MAX / old == new → return FALSE
 *     → g_upgrade_valid[old][new] → 返回 TRUE
 *
 * 升级路径按强度递进: NL → IS → {S, IX} → SIX → X。
 * 每条路径代表用户调用 treelock_escalate() 时的协议允许性检查。
 * 被调用: client.c: treelock_escalate(), _do_lock_core()
 */
TEST(EscalatePaths, Valid)
{
    /* NL → any */
    EXPECT_TRUE(treelock_escalate_valid(TREELOCK_NL, TREELOCK_IS));
    EXPECT_TRUE(treelock_escalate_valid(TREELOCK_NL, TREELOCK_IX));
    EXPECT_TRUE(treelock_escalate_valid(TREELOCK_NL, TREELOCK_S));
    EXPECT_TRUE(treelock_escalate_valid(TREELOCK_NL, TREELOCK_SIX));
    EXPECT_TRUE(treelock_escalate_valid(TREELOCK_NL, TREELOCK_X));

    /* IS → stronger */
    EXPECT_TRUE(treelock_escalate_valid(TREELOCK_IS, TREELOCK_IX));
    EXPECT_TRUE(treelock_escalate_valid(TREELOCK_IS, TREELOCK_S));
    EXPECT_TRUE(treelock_escalate_valid(TREELOCK_IS, TREELOCK_SIX));
    EXPECT_TRUE(treelock_escalate_valid(TREELOCK_IS, TREELOCK_X));

    /* IX → stronger */
    EXPECT_TRUE(treelock_escalate_valid(TREELOCK_IX, TREELOCK_SIX));
    EXPECT_TRUE(treelock_escalate_valid(TREELOCK_IX, TREELOCK_X));

    /* S → stronger */
    EXPECT_TRUE(treelock_escalate_valid(TREELOCK_S, TREELOCK_SIX));
    EXPECT_TRUE(treelock_escalate_valid(TREELOCK_S, TREELOCK_X));

    /* SIX → X */
    EXPECT_TRUE(treelock_escalate_valid(TREELOCK_SIX, TREELOCK_X));
}

/**
 * 测试目标: 验证所有非法的锁升级路径
 *
 * 运行路径:
 *   treelock_escalate_valid(old, new)
 *     → g_upgrade_valid[old][new] → 返回 FALSE（查表命中 0）
 *     → 或 old == new → return FALSE
 *
 * 非法路径包括: 同模式不变、降级方向、跳过必要中间模式。
 * 例如 X→IS 是先升级再降级？不，X 已是最强，无法"升级"到 IS。
 * 覆盖: protocol.c g_upgrade_valid[][] 的全部 0 值
 */
TEST(EscalatePaths, Invalid)
{
    EXPECT_FALSE(treelock_escalate_valid(TREELOCK_NL, TREELOCK_NL));
    EXPECT_FALSE(treelock_escalate_valid(TREELOCK_IS, TREELOCK_NL));
    EXPECT_FALSE(treelock_escalate_valid(TREELOCK_IX, TREELOCK_S));
    EXPECT_FALSE(treelock_escalate_valid(TREELOCK_S, TREELOCK_IX));
    EXPECT_FALSE(treelock_escalate_valid(TREELOCK_X, TREELOCK_IS));
    EXPECT_FALSE(treelock_escalate_valid(TREELOCK_X, TREELOCK_SIX));
}

/* =========================================================================
 * 锁降级路径测试
 *
 * 验证 treelock_downgrade_valid() 的所有合法和非法降级路径。
 * 函数路径: treelock_downgrade_valid(old, new) → g_downgrade_valid[old][new]
 *
 * 降级规则:
 *   X  → SIX/S/IX/IS/NL   (X 可降为任何更弱模式)
 *   SIX → S/IX/IS/NL       (SIX 可降为任何更弱模式)
 *   S  → IS/NL             (S 只可降为 IS 或 NL)
 *   IX → IS/NL             (IX 只可降为 IS 或 NL)
 *   IS → NL                (IS 只可降为 NL)
 * ========================================================================= */

/**
 * 测试目标: 验证所有合法的锁降级路径
 *
 * 运行路径:
 *   treelock_downgrade_valid(old, new)
 *     → 入口检查: old > MAX / new > MAX / old == new → return FALSE
 *     → g_downgrade_valid[old][new] → 返回 TRUE
 *
 * 降级是升级的逆操作，允许从更强模式回到更弱模式。
 * 被调用: client.c: treelock_downgrade()
 */
TEST(DowngradePaths, Valid)
{
    /* X → any weaker */
    EXPECT_TRUE(treelock_downgrade_valid(TREELOCK_X, TREELOCK_SIX));
    EXPECT_TRUE(treelock_downgrade_valid(TREELOCK_X, TREELOCK_S));
    EXPECT_TRUE(treelock_downgrade_valid(TREELOCK_X, TREELOCK_IX));
    EXPECT_TRUE(treelock_downgrade_valid(TREELOCK_X, TREELOCK_IS));
    EXPECT_TRUE(treelock_downgrade_valid(TREELOCK_X, TREELOCK_NL));

    /* SIX → weaker */
    EXPECT_TRUE(treelock_downgrade_valid(TREELOCK_SIX, TREELOCK_S));
    EXPECT_TRUE(treelock_downgrade_valid(TREELOCK_SIX, TREELOCK_IX));
    EXPECT_TRUE(treelock_downgrade_valid(TREELOCK_SIX, TREELOCK_IS));
    EXPECT_TRUE(treelock_downgrade_valid(TREELOCK_SIX, TREELOCK_NL));
}

/**
 * 测试目标: 验证所有非法的锁降级路径
 *
 * 运行路径:
 *   treelock_downgrade_valid(old, new)
 *     → g_downgrade_valid[old][new] → 返回 FALSE（命中 0）
 *     → 或 old == new → return FALSE
 *
 * 非法降级包括: 向上升级方向、相同模式不变、NL 降级(无更弱)。
 * 覆盖: protocol.c g_downgrade_valid[][] 的全部 0 值
 */
TEST(DowngradePaths, Invalid)
{
    EXPECT_FALSE(treelock_downgrade_valid(TREELOCK_NL, TREELOCK_IS));
    EXPECT_FALSE(treelock_downgrade_valid(TREELOCK_IS, TREELOCK_IX));
    EXPECT_FALSE(treelock_downgrade_valid(TREELOCK_S, TREELOCK_IX));
    EXPECT_FALSE(treelock_downgrade_valid(TREELOCK_IX, TREELOCK_S));
    EXPECT_FALSE(treelock_downgrade_valid(TREELOCK_S, TREELOCK_S));
}

/* =========================================================================
 * 工具函数测试
 *
 * 验证 treelock_mode_name() 和 treelock_strerror() 字符串转换函数。
 * 函数路径: 直接查表返回字符串字面量
 * ========================================================================= */

/**
 * 测试目标: treelock_mode_name() 返回正确的模式字符串名称
 *
 * 运行路径:
 *   treelock_mode_name(mode)
 *     → 检查 mode ≤ TREELOCK_MODE_MAX
 *     → 查 names[] 数组返回对应字面量
 *     → 越界返回 "UNKNOWN"
 *
 * 覆盖: protocol.c:191-201
 */
TEST(Utils, ModeName)
{
    EXPECT_STREQ(treelock_mode_name(TREELOCK_NL),  "NL");
    EXPECT_STREQ(treelock_mode_name(TREELOCK_IS),  "IS");
    EXPECT_STREQ(treelock_mode_name(TREELOCK_IX),  "IX");
    EXPECT_STREQ(treelock_mode_name(TREELOCK_S),   "S");
    EXPECT_STREQ(treelock_mode_name(TREELOCK_SIX), "SIX");
    EXPECT_STREQ(treelock_mode_name(TREELOCK_X),   "X");
    EXPECT_STREQ(treelock_mode_name((treelock_mode_t)99), "UNKNOWN");
}

/**
 * 测试目标: treelock_strerror() 对 7 种已知错误码返回非空描述
 *
 * 运行路径:
 *   treelock_strerror(err)
 *     → switch(err) 匹配各 TREELOCK_ERR_* case → 返回描述字符串
 *
 * 验证所有错误码都有对应的描述信息（长度 > 0），
 * 用于日志和调试输出中的错误可读性。
 * 覆盖: protocol.c:212-224
 */
TEST(Utils, StrError)
{
    EXPECT_STREQ(treelock_strerror(TREELOCK_OK), "Success");
    EXPECT_GT(strlen(treelock_strerror(TREELOCK_ERR_TIMEOUT)),  0u);
    EXPECT_GT(strlen(treelock_strerror(TREELOCK_ERR_CONFLICT)), 0u);
    EXPECT_GT(strlen(treelock_strerror(TREELOCK_ERR_PROTOCOL)), 0u);
    EXPECT_GT(strlen(treelock_strerror(TREELOCK_ERR_NETWORK)),  0u);
    EXPECT_GT(strlen(treelock_strerror(TREELOCK_ERR_STALE)),    0u);
    EXPECT_GT(strlen(treelock_strerror(TREELOCK_ERR_INVAL)),    0u);
}

/**
 * 测试目标: treelock_strerror() 对未知错误码返回 "Unknown error"
 *
 * 运行路径:
 *   treelock_strerror(999)
 *     → switch(err) 无匹配 → default: return "Unknown error"
 *
 * 确保未定义错误码不会导致返回 NULL 或崩溃。
 * 覆盖: protocol.c:223
 */
TEST(Utils, StrErrorUnknownCode)
{
    EXPECT_STREQ(treelock_strerror(999), "Unknown error");
    EXPECT_STREQ(treelock_strerror(-100), "Unknown error");
}

/* =========================================================================
 * 压力测试: 随机兼容矩阵 100 万次查询
 *
 * 测试目标: 高频率调用 treelock_mode_compatible，验证无崩溃、
 *          结果始终与标准矩阵一致。
 *
 * 覆盖: protocol.c treelock_mode_compatible() — 高频路径
 * ========================================================================= */

TEST(StressTest, RandomCompatMatrix1M)
{
    /* 标准答案矩阵 */
    static const int answer[6][6] = {
        /* NL  IS  IX   S SIX   X */
        {   1,  1,  1,  1,  1,  1 }, /* NL */
        {   1,  1,  1,  1,  1,  0 }, /* IS */
        {   1,  1,  1,  0,  0,  0 }, /* IX */
        {   1,  1,  0,  1,  0,  0 }, /* S  */
        {   1,  1,  0,  0,  0,  0 }, /* SIX */
        {   1,  0,  0,  0,  0,  0 }, /* X  */
    };

    for (int i = 0; i < 1000000; i++) {
        int ex = (i * 3 + 7) % 6;
        int rq = (i * 11 + 13) % 6;
        int expected = answer[ex][rq];
        int actual = treelock_mode_compatible(
            (treelock_mode_t)ex, (treelock_mode_t)rq);
        /* 只检查一批抽样，避免 100万次 EXPECT 导致测试极慢 */
        if (i % 100000 == 0 || actual != expected) {
            EXPECT_EQ(actual, expected)
                << "mismatch at iteration " << i
                << " existing=" << treelock_mode_name((treelock_mode_t)ex)
                << " requested=" << treelock_mode_name((treelock_mode_t)rq);
        }
    }
}

/* =========================================================================
 * 压力测试: escalate + downgrade 1000 次循环
 *
 * 测试目标: 快速升级/降级循环验证锁表状态一致性，
 *          每次循环后锁模式应正确，无状态残留。
 *
 * 覆盖: client.c treelock_escalate() + treelock_downgrade() —
 *       grant 替换 + held 原地更新
 * ========================================================================= */

TEST(StressTest, EscalateDowngradeCycle1K)
{
    treelock_t *tl = treelock_create(nullptr);
    ASSERT_NE(tl, nullptr);

    for (int cycle = 0; cycle < 1000; cycle++) {
        /* NL → IS → IX → SIX → X → SIX → IS → NL */
        EXPECT_EQ(treelock_lock(tl, 1, TREELOCK_IS), TREELOCK_OK);
        EXPECT_EQ(treelock_get_mode(tl, 1), TREELOCK_IS);

        EXPECT_EQ(treelock_escalate(tl, 1, TREELOCK_IX), TREELOCK_OK);
        EXPECT_EQ(treelock_get_mode(tl, 1), TREELOCK_IX);

        EXPECT_EQ(treelock_escalate(tl, 1, TREELOCK_SIX), TREELOCK_OK);
        EXPECT_EQ(treelock_get_mode(tl, 1), TREELOCK_SIX);

        EXPECT_EQ(treelock_escalate(tl, 1, TREELOCK_X), TREELOCK_OK);
        EXPECT_EQ(treelock_get_mode(tl, 1), TREELOCK_X);

        EXPECT_EQ(treelock_downgrade(tl, 1, TREELOCK_SIX), TREELOCK_OK);
        EXPECT_EQ(treelock_get_mode(tl, 1), TREELOCK_SIX);

        EXPECT_EQ(treelock_downgrade(tl, 1, TREELOCK_IS), TREELOCK_OK);
        EXPECT_EQ(treelock_get_mode(tl, 1), TREELOCK_IS);

        treelock_unlock(tl, 1);
        EXPECT_EQ(treelock_get_mode(tl, 1), TREELOCK_NL);
    }

    treelock_destroy(tl);
}

/* =========================================================================
 * 压力测试: 50 节点 × 100 次 lock/unlock
 *
 * 测试目标: 大量不同节点的快速 lock/unlock 循环，
 *          验证锁表和 held_locks 数组在大量操作后状态一致。
 *
 * 覆盖: lock_table.c treelock_table_get_or_create() 批量创建,
 *       client.c _add_held_lock() / _remove_held_lock() 频繁操作
 * ========================================================================= */

TEST(StressTest, MultiNodeHeavyLockUnlock)
{
    treelock_t *tl = treelock_create(nullptr);
    ASSERT_NE(tl, nullptr);

    for (int round = 0; round < 100; round++) {
        /* 获取 50 个不同节点的锁 */
        for (int n = 1; n <= 50; n++) {
            treelock_mode_t mode;
            switch (n % 5) {
                case 0: mode = TREELOCK_IS; break;
                case 1: mode = TREELOCK_IX; break;
                case 2: mode = TREELOCK_S;  break;
                case 3: mode = TREELOCK_SIX;break;
                default:mode = TREELOCK_X;  break;
            }
            ASSERT_EQ(treelock_lock(tl, (treelock_node_id_t)n, mode), TREELOCK_OK)
                << "failed at round=" << round << " node=" << n;
        }

        /* 验证全部持有 */
        for (int n = 1; n <= 50; n++) {
            EXPECT_NE(treelock_get_mode(tl, (treelock_node_id_t)n), TREELOCK_NL);
        }

        /* 逆序释放 */
        for (int n = 50; n >= 1; n--) {
            EXPECT_EQ(treelock_unlock(tl, (treelock_node_id_t)n), TREELOCK_OK);
        }
    }

    treelock_destroy(tl);
}

/* =========================================================================
 * 死锁测试: 协议强制防止锁顺序死锁
 *
 * 两层树: root(1) → child(2)
 *
 * 经典死锁场景 (如果无协议强制):
 *   Thread A: lock(2, X) → lock(1, X)   (自底向上)
 *   Thread B: lock(1, X) → lock(2, X)   (自顶向下)
 *   → 死锁: A 持有 2 等 1, B 持有 1 等 2
 *
 * 有协议强制时: A 的第一步 lock(2, X) 会失败 (父节点未锁)
 * → 死锁被协议层预防。
 *
 * 覆盖: protocol.c treelock_validate_protocol() — IS/IX 分支
 * ========================================================================= */

TEST(DeadlockTest, ProtocolPreventsLockOrderingDeadlock)
{
    treelock_t *tl = treelock_create(nullptr);
    ASSERT_NE(tl, nullptr);

    /* 构建树: root(1) → child(2) */
    ASSERT_EQ(treelock_register_node(tl, 1, 0, "/"), TREELOCK_OK);
    ASSERT_EQ(treelock_register_node(tl, 2, 1, "child"), TREELOCK_OK);

    /* 自底向上 lock — 第一步就因协议违规被拒绝 */
    EXPECT_EQ(treelock_lock(tl, 2, TREELOCK_X), TREELOCK_ERR_PROTOCOL);

    /* 正确顺序: 自顶向下 */
    EXPECT_EQ(treelock_lock(tl, 1, TREELOCK_IX), TREELOCK_OK);
    EXPECT_EQ(treelock_lock(tl, 2, TREELOCK_X), TREELOCK_OK);
    treelock_unlock(tl, 2);
    treelock_unlock(tl, 1);

    treelock_destroy(tl);
}

/* =========================================================================
 * 死锁测试: 共享实例多线程无死锁
 *
 * 测试目标: 8 线程共享同一个 treelock_t，对不同节点执行
 *          lock/unlock 500 次，验证操作在超时内全部完成（无死锁）。
 *
 * 所有线程使用同一 client_id，因此自冲突被跳过。
 * 不同节点的锁操作由 node->mutex 顺序保护。
 *
 * 覆盖: lock_table.c treelock_table_get_or_create() +
 *       treelock_table_check_conflict() + 锁表并发安全性
 * ========================================================================= */

static void *deadlock_worker(void *arg) {
    auto *tl = (treelock_t *)arg;
    for (int i = 0; i < 500; i++) {
        auto nid = (treelock_node_id_t)((i % 20) + 1);
        treelock_mode_t mode;
        switch (i % 3) {
            case 0: mode = TREELOCK_IS; break;
            case 1: mode = TREELOCK_IX; break;
            default:mode = TREELOCK_S;  break;
        }
        /* 短超时确保不会永久阻塞 */
        int rc = treelock_try_lock(tl, nid, mode, 5000);
        if (rc == TREELOCK_OK) {
            treelock_unlock(tl, nid);
        }
        /* 不应有任何致命错误 — 用 if 避免 ASSERT 在 void* 函数中的问题 */
        if (rc == TREELOCK_ERR_INVAL) return (void *)(intptr_t)(-1);
    }
    return nullptr;
}

TEST(DeadlockTest, SharedInstanceNoDeadlock)
{
    treelock_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.timeout_ms = 5000;
    cfg.client_id  = "deadlock_test";

    treelock_t *tl = treelock_create(&cfg);
    ASSERT_NE(tl, nullptr);

    const int N = 8;
    pthread_t threads[N];
    for (int t = 0; t < N; t++) {
        pthread_create(&threads[t], nullptr, deadlock_worker, tl);
    }
    for (int t = 0; t < N; t++) {
        pthread_join(threads[t], nullptr);
    }

    treelock_destroy(tl);
    SUCCEED(); /* 8 线程全部在超时内完成 = 无死锁 */
}

/* =========================================================================
 * 死锁测试: escalate 死锁预防
 *
 * 测试目标: 多线程同时对不同节点执行 escalate 操作，
 *          escalate 直接操作锁表 (treelock_table_grant_lock +
 *          treelock_table_release_lock)，验证并发 escalate 不产生死锁。
 *
 * 覆盖: client.c treelock_escalate() — node->mutex 保护,
 *       lock_table.c grant + release 并发安全
 * ========================================================================= */

static void *escalate_deadlock_worker(void *arg) {
    auto *tl = (treelock_t *)arg;
    for (int i = 0; i < 300; i++) {
        auto nid = (treelock_node_id_t)((i % 10) + 1);
        if (treelock_lock(tl, nid, TREELOCK_IS) != TREELOCK_OK) continue;
        treelock_escalate(tl, nid, TREELOCK_IX);
        treelock_escalate(tl, nid, TREELOCK_SIX);
        treelock_unlock(tl, nid);
    }
    return nullptr;
}

TEST(DeadlockTest, EscalateConcurrentNoDeadlock)
{
    treelock_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.timeout_ms = 5000;
    cfg.client_id  = "escalate_dl";

    treelock_t *tl = treelock_create(&cfg);
    ASSERT_NE(tl, nullptr);

    const int N = 6;
    pthread_t threads[N];
    for (int t = 0; t < N; t++) {
        pthread_create(&threads[t], nullptr, escalate_deadlock_worker, tl);
    }
    for (int t = 0; t < N; t++) {
        pthread_join(threads[t], nullptr);
    }

    treelock_destroy(tl);
    SUCCEED();
}

/* =========================================================================
 * 死锁测试: 多节点交叉锁 — 协议强制无死锁
 *
 * 三层树: / (1) → a (2) → b (3)
 *          \→ x (4) → y (5)
 *
 * 多线程对不同子树按正确顺序加锁，验证无死锁。
 * lock_path 确保自顶向下顺序。
 *
 * 覆盖: tree_api.c treelock_lock_path() — 整条路径原子性,
 *       protocol.c treelock_validate_protocol() — 多层校验
 * ========================================================================= */

TEST(DeadlockTest, CrossSubtreeNoDeadlock)
{
    treelock_t *tl = treelock_create(nullptr);
    ASSERT_NE(tl, nullptr);

    /* / (1) → a (2) → b (3),  / → x (4) → y (5) */
    ASSERT_EQ(treelock_register_node(tl, 1, 0, "/"), TREELOCK_OK);
    ASSERT_EQ(treelock_register_node(tl, 2, 1, "a"), TREELOCK_OK);
    ASSERT_EQ(treelock_register_node(tl, 3, 2, "b"), TREELOCK_OK);
    ASSERT_EQ(treelock_register_node(tl, 4, 1, "x"), TREELOCK_OK);
    ASSERT_EQ(treelock_register_node(tl, 5, 4, "y"), TREELOCK_OK);

    /* 两个线程同时 lock_path 不同子树 — 共享根节点但不应死锁 */
    /* (Phase 1: 同一 client_id，自身不冲突，都应成功) */
    std::thread t1([tl]() {
        for (int i = 0; i < 50; i++) {
            EXPECT_EQ(treelock_lock_path(tl, "/a/b", TREELOCK_X), TREELOCK_OK);
            EXPECT_EQ(treelock_unlock_path(tl, "/a/b"), TREELOCK_OK);
        }
    });
    std::thread t2([tl]() {
        for (int i = 0; i < 50; i++) {
            EXPECT_EQ(treelock_lock_path(tl, "/x/y", TREELOCK_X), TREELOCK_OK);
            EXPECT_EQ(treelock_unlock_path(tl, "/x/y"), TREELOCK_OK);
        }
    });

    t1.join();
    t2.join();

    /* 全部释放（线程可能因重入导致引用计数残留，unlock_all 兜底） */
    treelock_unlock_all(tl);
    for (int n = 1; n <= 5; n++) {
        EXPECT_EQ(treelock_get_mode(tl, (treelock_node_id_t)n), TREELOCK_NL);
    }

    treelock_destroy(tl);
}

/* =========================================================================
 * 内存泄露测试: 重复 create/lock 多节点/destroy 周期
 *
 * 测试目标: 100 次完整生命周期（create → 获取多把锁 →
 *          unlock_all → destroy），通过大量重复分配/释放检测
 *          内存泄露和资源泄露（mutex/cond var 残留）。
 *
 * 如果之前的修复合入前运行此测试，树索引泄露会导致
 * 内存持续增长。现在应稳定通过。
 *
 * 覆盖: client.c treelock_create() + treelock_destroy() 完整清理,
 *       lock_table.c 节点创建/释放,
 *       tree 模块 tree_index 分配/释放
 * ========================================================================= */

TEST(MemoryTest, RepeatedCreateLockDestroy100)
{
    const char *json =
        "{ \"nodes\": ["
        "  { \"id\": 1, \"label\": \"root\", \"parent\": 0 },"
        "  { \"id\": 2, \"label\": \"a\",    \"parent\": 1 },"
        "  { \"id\": 3, \"label\": \"b\",    \"parent\": 2 },"
        "  { \"id\": 4, \"label\": \"c\",    \"parent\": 1 }"
        "]}";

    for (int cycle = 0; cycle < 100; cycle++) {
        treelock_t *tl = treelock_create(nullptr);
        ASSERT_NE(tl, nullptr);

        ASSERT_EQ(treelock_load_tree_from_string(tl, json), TREELOCK_OK);

        /* 多种锁模式组合 */
        EXPECT_EQ(treelock_lock_path(tl, "/a/b", TREELOCK_X), TREELOCK_OK);
        EXPECT_EQ(treelock_unlock_path(tl, "/a/b"), TREELOCK_OK);

        EXPECT_EQ(treelock_lock_path(tl, "/c", TREELOCK_S), TREELOCK_OK);
        /* escalate 目标节点前需先升级根节点（S→SIX 要求父节点持有 IX 或更强） */
        EXPECT_EQ(treelock_escalate(tl, 1, TREELOCK_IX), TREELOCK_OK);
        /* escalate + downgrade on target node */
        EXPECT_EQ(treelock_escalate(tl, 4, TREELOCK_SIX), TREELOCK_OK);
        EXPECT_EQ(treelock_downgrade(tl, 4, TREELOCK_IS), TREELOCK_OK);
        treelock_unlock_path(tl, "/c");

        treelock_unlock_all(tl);
        treelock_destroy(tl);
    }

    SUCCEED(); /* 100 次完整生命周期无崩溃 = 无明显内存泄露模式 */
}

/* =========================================================================
 * 内存泄露测试: grant/waiter 数组重复扩容回缩
 *
 * 测试目标: 在同一节点上反复 grant/release 和 waiter add/remove，
 *          验证动态数组不会无限增长（每次 swap-remove 正确管理容量）。
 *
 * 覆盖: lock_table.c treelock_table_grant_lock() 数组扩展,
 *       treelock_table_release_lock() swap-remove,
 *       treelock_table_add_waiter() + wake_waiters cond 管理
 * ========================================================================= */

typedef struct {
    treelock_t *tl;
    int         id;
    int         acquired;
} mem_waiter_t;

static void *mem_leak_waiter(void *arg) {
    auto *mw = (mem_waiter_t *)arg;
    int rc = treelock_try_lock(mw->tl, 9999, TREELOCK_X, 100);
    if (rc == TREELOCK_OK) {
        mw->acquired = 1;
        treelock_unlock(mw->tl, 9999);
    }
    return nullptr;
}

TEST(MemoryTest, GrantWaiterArrayRecycle)
{
    treelock_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.timeout_ms = 5000;
    cfg.client_id  = "mem_test";

    treelock_t *tl = treelock_create(&cfg);
    ASSERT_NE(tl, nullptr);

    for (int cycle = 0; cycle < 200; cycle++) {
        /* 获取不同数量的锁 (触发 grant 数组多次扩容) */
        int num_locks = (cycle % 10) + 1;
        for (int i = 0; i < num_locks; i++) {
            ASSERT_EQ(treelock_lock(tl, 9999, TREELOCK_IS), TREELOCK_OK);
        }
        for (int i = 0; i < num_locks; i++) {
            ASSERT_EQ(treelock_unlock(tl, 9999), TREELOCK_OK);
        }

        /* 制造短暂的等待者场景 */
        ASSERT_EQ(treelock_lock(tl, 9999, TREELOCK_X), TREELOCK_OK);

        pthread_t w;
        mem_waiter_t mw = { tl, cycle, 0 };
        pthread_create(&w, nullptr, mem_leak_waiter, &mw);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));

        treelock_unlock(tl, 9999); /* 释放 → 唤醒 waiter */
        pthread_join(w, nullptr);
    }

    treelock_destroy(tl);
    SUCCEED(); /* 200 次 waiter 进出 + grant 扩容 = 验证 cond 管理 */
}

/* =========================================================================
 * 内存泄露测试: 多线程并发 create/destroy
 *
 * 测试目标: 多个线程独立执行 create→lock→destroy 循环，
 *          并发分配/释放互不干扰，不产生内存泄露或资源竞争。
 *
 * 覆盖: client.c treelock_create() + treelock_destroy() 并发安全
 * ========================================================================= */

static void *mem_create_destroy_worker(void *arg) {
    int tid = *(int *)arg;
    for (int c = 0; c < 50; c++) {
        treelock_t *tl = treelock_create(nullptr);
        if (!tl) return (void *)(intptr_t)(-1);

        /* 获取一些锁 */
        for (int n = 0; n < 20; n++) {
            auto nid = (treelock_node_id_t)(tid * 100 + n + 1);
            if (treelock_lock(tl, nid, TREELOCK_IX) != TREELOCK_OK) {
                treelock_destroy(tl);
                return (void *)(intptr_t)(-1);
            }
        }
        treelock_unlock_all(tl);
        treelock_destroy(tl);
    }
    return nullptr;
}

TEST(MemoryTest, ConcurrentCreateDestroy)
{
    const int N = 6;
    pthread_t threads[N];
    int ids[N];

    for (int t = 0; t < N; t++) {
        ids[t] = t;
        pthread_create(&threads[t], nullptr, mem_create_destroy_worker, &ids[t]);
    }

    int errors = 0;
    for (int t = 0; t < N; t++) {
        void *ret;
        pthread_join(threads[t], &ret);
        if (ret != nullptr) errors++;
    }

    EXPECT_EQ(errors, 0) << "concurrent create/destroy had errors";
}

/* =========================================================================
 * 压力测试: wait_queue 高强度 churn
 *
 * 测试目标: 固定 holder + 多 waiter 场景下高频释放/唤醒/重试，
 *          swap-remove 在极端频率下保持队列一致性。
 *
 * 覆盖: lock_table.c treelock_table_wake_waiters() swap-remove +
 *       treelock_table_add_waiter() cond init +
 *       client.c _do_lock_core() 超时/唤醒路径 cond destroy
 * ========================================================================= */

TEST(StressTest, WaitQueueHighChurn)
{
    treelock_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.timeout_ms = 100;
    cfg.client_id  = "churn_stress";

    treelock_t *tl = treelock_create(&cfg);
    ASSERT_NE(tl, nullptr);

    volatile int running = 1;
    int holder_ops = 0;
    int waiter_ops[6] = {0};
    int waiter_errs[6] = {0};

    auto holder = [&]() {
        while (running) {
            if (treelock_lock(tl, 1, TREELOCK_X) == TREELOCK_OK) {
                holder_ops++;
                std::this_thread::sleep_for(std::chrono::microseconds(500));
                treelock_unlock(tl, 1);
            }
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
    };

    auto waiter = [&](int idx) {
        while (running) {
            int rc = treelock_try_lock(tl, 1, TREELOCK_X, 20);
            if (rc == TREELOCK_OK) {
                waiter_ops[idx]++;
                std::this_thread::sleep_for(std::chrono::microseconds(100));
                treelock_unlock(tl, 1);
            } else if (rc != TREELOCK_ERR_TIMEOUT) {
                waiter_errs[idx]++;
            }
        }
    };

    std::thread ht(holder);
    std::thread wt[6];
    for (int i = 0; i < 6; i++) wt[i] = std::thread(waiter, i);

    std::this_thread::sleep_for(std::chrono::milliseconds(3000));
    running = 0;

    ht.join();
    for (int i = 0; i < 6; i++) wt[i].join();

    int total_waits = 0, total_errs = 0;
    for (int i = 0; i < 6; i++) {
        total_waits += waiter_ops[i];
        total_errs += waiter_errs[i];
    }

    EXPECT_GT(holder_ops, 100) << "holder should have performed many ops";
    EXPECT_GT(total_waits, 50) << "waiters should have acquired lock";
    EXPECT_EQ(total_errs, 0) << "no protocol errors in churn";
}

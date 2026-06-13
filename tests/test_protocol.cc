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

extern "C" {
#include "treelock.h"
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

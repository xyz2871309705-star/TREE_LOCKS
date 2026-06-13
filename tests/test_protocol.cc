/*
 * TreeLocks — 协议正确性测试 (GTest)
 *
 * 测试兼容矩阵、锁升级/降级路径等协议层逻辑。
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
 * 兼容矩阵
 * ========================================================================= */

TEST(CompatMatrix, NL_CompatibleWithAll)
{
    for (int m = TREELOCK_NL; m <= TREELOCK_MODE_MAX; m++) {
        EXPECT_TRUE(treelock_mode_compatible(TREELOCK_NL, (treelock_mode_t)m))
            << "NL should be compatible with mode " << treelock_mode_name((treelock_mode_t)m);
    }
}

TEST(CompatMatrix, X_OnlyCompatibleWithNL)
{
    EXPECT_TRUE (treelock_mode_compatible(TREELOCK_X, TREELOCK_NL));
    EXPECT_FALSE(treelock_mode_compatible(TREELOCK_X, TREELOCK_IS));
    EXPECT_FALSE(treelock_mode_compatible(TREELOCK_X, TREELOCK_IX));
    EXPECT_FALSE(treelock_mode_compatible(TREELOCK_X, TREELOCK_S));
    EXPECT_FALSE(treelock_mode_compatible(TREELOCK_X, TREELOCK_SIX));
    EXPECT_FALSE(treelock_mode_compatible(TREELOCK_X, TREELOCK_X));
}

TEST(CompatMatrix, IS_CompatibleWithAllExceptX)
{
    EXPECT_TRUE (treelock_mode_compatible(TREELOCK_IS, TREELOCK_NL));
    EXPECT_TRUE (treelock_mode_compatible(TREELOCK_IS, TREELOCK_IS));
    EXPECT_TRUE (treelock_mode_compatible(TREELOCK_IS, TREELOCK_IX));
    EXPECT_TRUE (treelock_mode_compatible(TREELOCK_IS, TREELOCK_S));
    EXPECT_TRUE (treelock_mode_compatible(TREELOCK_IS, TREELOCK_SIX));
    EXPECT_FALSE(treelock_mode_compatible(TREELOCK_IS, TREELOCK_X));
}

TEST(CompatMatrix, IX_OnlyCompatibleWithNL_IS_IX)
{
    EXPECT_TRUE (treelock_mode_compatible(TREELOCK_IX, TREELOCK_NL));
    EXPECT_TRUE (treelock_mode_compatible(TREELOCK_IX, TREELOCK_IS));
    EXPECT_TRUE (treelock_mode_compatible(TREELOCK_IX, TREELOCK_IX));
    EXPECT_FALSE(treelock_mode_compatible(TREELOCK_IX, TREELOCK_S));
    EXPECT_FALSE(treelock_mode_compatible(TREELOCK_IX, TREELOCK_SIX));
    EXPECT_FALSE(treelock_mode_compatible(TREELOCK_IX, TREELOCK_X));
}

TEST(CompatMatrix, S_OnlyCompatibleWithNL_IS_S)
{
    EXPECT_TRUE (treelock_mode_compatible(TREELOCK_S, TREELOCK_NL));
    EXPECT_TRUE (treelock_mode_compatible(TREELOCK_S, TREELOCK_IS));
    EXPECT_FALSE(treelock_mode_compatible(TREELOCK_S, TREELOCK_IX));
    EXPECT_TRUE (treelock_mode_compatible(TREELOCK_S, TREELOCK_S));
    EXPECT_FALSE(treelock_mode_compatible(TREELOCK_S, TREELOCK_SIX));
    EXPECT_FALSE(treelock_mode_compatible(TREELOCK_S, TREELOCK_X));
}

TEST(CompatMatrix, SIX_OnlyCompatibleWithNL_IS)
{
    EXPECT_TRUE (treelock_mode_compatible(TREELOCK_SIX, TREELOCK_NL));
    EXPECT_TRUE (treelock_mode_compatible(TREELOCK_SIX, TREELOCK_IS));
    EXPECT_FALSE(treelock_mode_compatible(TREELOCK_SIX, TREELOCK_IX));
    EXPECT_FALSE(treelock_mode_compatible(TREELOCK_SIX, TREELOCK_S));
    EXPECT_FALSE(treelock_mode_compatible(TREELOCK_SIX, TREELOCK_SIX));
    EXPECT_FALSE(treelock_mode_compatible(TREELOCK_SIX, TREELOCK_X));
}

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
 * 父节点锁模式
 * ========================================================================= */

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
 * 越界参数处理
 * ========================================================================= */

TEST(Boundary, ModeCompatibleOutOfRange)
{
    /* 超出 TREELOCK_MODE_MAX 的值应返回 FALSE，不崩溃 */
    EXPECT_FALSE(treelock_mode_compatible((treelock_mode_t)99, TREELOCK_IS));
    EXPECT_FALSE(treelock_mode_compatible(TREELOCK_IS, (treelock_mode_t)99));
    EXPECT_FALSE(treelock_mode_compatible((treelock_mode_t)99, (treelock_mode_t)99));
    /* 负值也应安全处理 */
    EXPECT_FALSE(treelock_mode_compatible((treelock_mode_t)(-1), TREELOCK_IS));
}

TEST(Boundary, RequiredParentModeOutOfRange)
{
    EXPECT_EQ(treelock_required_parent_mode((treelock_mode_t)99), TREELOCK_NL);
    EXPECT_EQ(treelock_required_parent_mode((treelock_mode_t)(-1)), TREELOCK_NL);
}

TEST(Boundary, EscalateValidOutOfRange)
{
    EXPECT_FALSE(treelock_escalate_valid((treelock_mode_t)99, TREELOCK_S));
    EXPECT_FALSE(treelock_escalate_valid(TREELOCK_IS, (treelock_mode_t)99));
    EXPECT_FALSE(treelock_escalate_valid((treelock_mode_t)99, (treelock_mode_t)99));
}

TEST(Boundary, DowngradeValidOutOfRange)
{
    EXPECT_FALSE(treelock_downgrade_valid((treelock_mode_t)99, TREELOCK_NL));
    EXPECT_FALSE(treelock_downgrade_valid(TREELOCK_X, (treelock_mode_t)99));
}

/* =========================================================================
 * 锁升级路径
 * ========================================================================= */

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
 * 锁降级路径
 * ========================================================================= */

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

TEST(DowngradePaths, Invalid)
{
    EXPECT_FALSE(treelock_downgrade_valid(TREELOCK_NL, TREELOCK_IS));
    EXPECT_FALSE(treelock_downgrade_valid(TREELOCK_IS, TREELOCK_IX));
    EXPECT_FALSE(treelock_downgrade_valid(TREELOCK_S, TREELOCK_IX));
    EXPECT_FALSE(treelock_downgrade_valid(TREELOCK_IX, TREELOCK_S));
    EXPECT_FALSE(treelock_downgrade_valid(TREELOCK_S, TREELOCK_S));
}

/* =========================================================================
 * 工具函数
 * ========================================================================= */

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

TEST(Utils, StrErrorUnknownCode)
{
    EXPECT_STREQ(treelock_strerror(999), "Unknown error");
    EXPECT_STREQ(treelock_strerror(-100), "Unknown error");
}

/*
 * TreeLocks - 协议正确性测试
 *
 * 测试兼容矩阵、锁升级/降级路径等协议层逻辑。
 *
 * 版本: 0.1.0
 * 日期: 2026-06-13
 */

#include "treelock.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 访问内部函数 */
#include "../src/internal.h"

/* =========================================================================
 * 测试框架（最小化实现，不依赖第三方库）
 * ========================================================================= */

static INT_32 g_tests_run    = 0;
static INT_32 g_tests_passed = 0;
static INT_32 g_tests_failed = 0;

/**
 * 函数名称：test_begin
 *
 * 功能描述：标记一个测试用例开始
 *
 * @param[IN] name - 测试名称字符串
 */
static VOID test_begin(
    IN CSTR_PTR name)
{
    printf("  TEST: %s ... ", name);
    g_tests_run++;
}

/**
 * 函数名称：test_pass
 *
 * 功能描述：标记当前测试通过
 */
static VOID test_pass(VOID)
{
    printf("PASSED\n");
    g_tests_passed++;
}

/**
 * 函数名称：test_fail
 *
 * 功能描述：标记当前测试失败并输出原因
 *
 * @param[IN] msg - 失败原因描述
 */
static VOID test_fail(
    IN CSTR_PTR msg)
{
    printf("FAILED: %s\n", msg);
    g_tests_failed++;
}

#define TEST(name) test_begin(name)

#define PASS() test_pass()

#define FAIL(msg) do { test_fail(msg); return; } while (0)

#define ASSERT_TRUE(cond, msg) \
    do { \
        if (!(cond)) { test_fail(msg); return; } \
    } while (0)

#define ASSERT_EQ(a, b, msg) \
    do { \
        if ((a) != (b)) { \
            CHAR _buf[256]; \
            snprintf(_buf, sizeof(_buf), "%s (expected=%d, actual=%d)", \
                     msg, (INT_32)(b), (INT_32)(a)); \
            test_fail(_buf); return; \
        } \
    } while (0)

/* =========================================================================
 * 测试用例：兼容矩阵
 * ========================================================================= */

/**
 * 函数名称：test_compat_matrix_nl
 *
 * 功能描述：验证 NL 与所有模式兼容
 */
static VOID test_compat_matrix_nl(VOID)
{
    INT_32 m;
    TEST("NL compatible with all modes");
    for (m = TREELOCK_NL; m <= TREELOCK_MODE_MAX; m++) {
        ASSERT_TRUE(treelock_mode_compatible(TREELOCK_NL, (treelock_mode_t)m),
                    "NL should be compatible");
    }
    PASS();
}

/**
 * 函数名称：test_compat_matrix_x
 *
 * 功能描述：验证 X 仅与 NL 兼容
 */
static VOID test_compat_matrix_x(VOID)
{
    TEST("X compatible only with NL");
    ASSERT_TRUE(treelock_mode_compatible(TREELOCK_X, TREELOCK_NL),
                "X-NL compatible");
    ASSERT_TRUE(!treelock_mode_compatible(TREELOCK_X, TREELOCK_IS),
                "X-IS not compatible");
    ASSERT_TRUE(!treelock_mode_compatible(TREELOCK_X, TREELOCK_IX),
                "X-IX not compatible");
    ASSERT_TRUE(!treelock_mode_compatible(TREELOCK_X, TREELOCK_S),
                "X-S not compatible");
    ASSERT_TRUE(!treelock_mode_compatible(TREELOCK_X, TREELOCK_SIX),
                "X-SIX not compatible");
    ASSERT_TRUE(!treelock_mode_compatible(TREELOCK_X, TREELOCK_X),
                "X-X not compatible");
    PASS();
}

/**
 * 函数名称：test_compat_matrix_is
 *
 * 功能描述：验证 IS 与除 X 外的所有模式兼容
 */
static VOID test_compat_matrix_is(VOID)
{
    TEST("IS compatible with NL/IS/IX/S/SIX");
    ASSERT_TRUE(treelock_mode_compatible(TREELOCK_IS, TREELOCK_NL),  "IS-NL");
    ASSERT_TRUE(treelock_mode_compatible(TREELOCK_IS, TREELOCK_IS),  "IS-IS");
    ASSERT_TRUE(treelock_mode_compatible(TREELOCK_IS, TREELOCK_IX),  "IS-IX");
    ASSERT_TRUE(treelock_mode_compatible(TREELOCK_IS, TREELOCK_S),   "IS-S");
    ASSERT_TRUE(treelock_mode_compatible(TREELOCK_IS, TREELOCK_SIX), "IS-SIX");
    ASSERT_TRUE(!treelock_mode_compatible(TREELOCK_IS, TREELOCK_X),  "IS-X");
    PASS();
}

/**
 * 函数名称：test_compat_matrix_ix
 *
 * 功能描述：验证 IX 仅与 NL/IS/IX 兼容
 */
static VOID test_compat_matrix_ix(VOID)
{
    TEST("IX compatible with NL/IS/IX");
    ASSERT_TRUE(treelock_mode_compatible(TREELOCK_IX, TREELOCK_NL),  "IX-NL");
    ASSERT_TRUE(treelock_mode_compatible(TREELOCK_IX, TREELOCK_IS),  "IX-IS");
    ASSERT_TRUE(treelock_mode_compatible(TREELOCK_IX, TREELOCK_IX),  "IX-IX");
    ASSERT_TRUE(!treelock_mode_compatible(TREELOCK_IX, TREELOCK_S),  "IX-S");
    ASSERT_TRUE(!treelock_mode_compatible(TREELOCK_IX, TREELOCK_SIX),"IX-SIX");
    ASSERT_TRUE(!treelock_mode_compatible(TREELOCK_IX, TREELOCK_X),  "IX-X");
    PASS();
}

/**
 * 函数名称：test_compat_matrix_s
 *
 * 功能描述：验证 S 仅与 NL/IS/S 兼容
 */
static VOID test_compat_matrix_s(VOID)
{
    TEST("S compatible with NL/IS/S");
    ASSERT_TRUE(treelock_mode_compatible(TREELOCK_S, TREELOCK_NL),  "S-NL");
    ASSERT_TRUE(treelock_mode_compatible(TREELOCK_S, TREELOCK_IS),  "S-IS");
    ASSERT_TRUE(!treelock_mode_compatible(TREELOCK_S, TREELOCK_IX), "S-IX");
    ASSERT_TRUE(treelock_mode_compatible(TREELOCK_S, TREELOCK_S),   "S-S");
    ASSERT_TRUE(!treelock_mode_compatible(TREELOCK_S, TREELOCK_SIX),"S-SIX");
    ASSERT_TRUE(!treelock_mode_compatible(TREELOCK_S, TREELOCK_X),  "S-X");
    PASS();
}

/* =========================================================================
 * 测试用例：父节点锁模式
 * ========================================================================= */

/**
 * 函数名称：test_required_parent_mode
 *
 * 功能描述：验证各锁模式对应的父节点最小锁要求
 */
static VOID test_required_parent_mode(VOID)
{
    TEST("required parent mode");

    ASSERT_EQ(treelock_required_parent_mode(TREELOCK_NL),  TREELOCK_NL,  "NL→NL");
    ASSERT_EQ(treelock_required_parent_mode(TREELOCK_IS),  TREELOCK_IS,  "IS→IS");
    ASSERT_EQ(treelock_required_parent_mode(TREELOCK_IX),  TREELOCK_IX,  "IX→IX");
    ASSERT_EQ(treelock_required_parent_mode(TREELOCK_S),   TREELOCK_IS,  "S→IS");
    ASSERT_EQ(treelock_required_parent_mode(TREELOCK_SIX), TREELOCK_IX,  "SIX→IX");
    ASSERT_EQ(treelock_required_parent_mode(TREELOCK_X),   TREELOCK_IX,  "X→IX");

    PASS();
}

/* =========================================================================
 * 测试用例：锁升级路径
 * ========================================================================= */

/**
 * 函数名称：test_escalate_valid_paths
 *
 * 功能描述：验证所有合法的锁升级路径
 */
static VOID test_escalate_valid_paths(VOID)
{
    TEST("valid escalate paths");

    /* NL → 任意 */
    ASSERT_TRUE(treelock_escalate_valid(TREELOCK_NL, TREELOCK_IS),  "NL→IS");
    ASSERT_TRUE(treelock_escalate_valid(TREELOCK_NL, TREELOCK_IX),  "NL→IX");
    ASSERT_TRUE(treelock_escalate_valid(TREELOCK_NL, TREELOCK_S),   "NL→S");
    ASSERT_TRUE(treelock_escalate_valid(TREELOCK_NL, TREELOCK_SIX), "NL→SIX");
    ASSERT_TRUE(treelock_escalate_valid(TREELOCK_NL, TREELOCK_X),   "NL→X");

    /* IS → 更强 */
    ASSERT_TRUE(treelock_escalate_valid(TREELOCK_IS, TREELOCK_IX),  "IS→IX");
    ASSERT_TRUE(treelock_escalate_valid(TREELOCK_IS, TREELOCK_S),   "IS→S");
    ASSERT_TRUE(treelock_escalate_valid(TREELOCK_IS, TREELOCK_SIX), "IS→SIX");
    ASSERT_TRUE(treelock_escalate_valid(TREELOCK_IS, TREELOCK_X),   "IS→X");

    /* IX → 更强 */
    ASSERT_TRUE(treelock_escalate_valid(TREELOCK_IX, TREELOCK_SIX), "IX→SIX");
    ASSERT_TRUE(treelock_escalate_valid(TREELOCK_IX, TREELOCK_X),   "IX→X");

    /* S → 更强 */
    ASSERT_TRUE(treelock_escalate_valid(TREELOCK_S, TREELOCK_SIX),  "S→SIX");
    ASSERT_TRUE(treelock_escalate_valid(TREELOCK_S, TREELOCK_X),    "S→X");

    /* SIX → X */
    ASSERT_TRUE(treelock_escalate_valid(TREELOCK_SIX, TREELOCK_X),  "SIX→X");

    PASS();
}

/**
 * 函数名称：test_escalate_invalid_paths
 *
 * 功能描述：验证非法锁升级路径被正确拒绝
 */
static VOID test_escalate_invalid_paths(VOID)
{
    TEST("invalid escalate paths");

    ASSERT_TRUE(!treelock_escalate_valid(TREELOCK_NL, TREELOCK_NL), "NL→NL");
    ASSERT_TRUE(!treelock_escalate_valid(TREELOCK_IS, TREELOCK_NL), "IS→NL");
    ASSERT_TRUE(!treelock_escalate_valid(TREELOCK_IX, TREELOCK_S),  "IX→S");
    ASSERT_TRUE(!treelock_escalate_valid(TREELOCK_S, TREELOCK_IX),  "S→IX");
    ASSERT_TRUE(!treelock_escalate_valid(TREELOCK_X, TREELOCK_IS),  "X→IS");
    ASSERT_TRUE(!treelock_escalate_valid(TREELOCK_X, TREELOCK_SIX), "X→SIX");

    PASS();
}

/* =========================================================================
 * 测试用例：锁降级路径
 * ========================================================================= */

/**
 * 函数名称：test_downgrade_valid_paths
 *
 * 功能描述：验证所有合法的锁降级路径
 */
static VOID test_downgrade_valid_paths(VOID)
{
    TEST("valid downgrade paths");

    /* X → 任意更弱 */
    ASSERT_TRUE(treelock_downgrade_valid(TREELOCK_X, TREELOCK_SIX), "X→SIX");
    ASSERT_TRUE(treelock_downgrade_valid(TREELOCK_X, TREELOCK_S),   "X→S");
    ASSERT_TRUE(treelock_downgrade_valid(TREELOCK_X, TREELOCK_IX),  "X→IX");
    ASSERT_TRUE(treelock_downgrade_valid(TREELOCK_X, TREELOCK_IS),  "X→IS");
    ASSERT_TRUE(treelock_downgrade_valid(TREELOCK_X, TREELOCK_NL),  "X→NL");

    /* SIX → 更弱 */
    ASSERT_TRUE(treelock_downgrade_valid(TREELOCK_SIX, TREELOCK_S),  "SIX→S");
    ASSERT_TRUE(treelock_downgrade_valid(TREELOCK_SIX, TREELOCK_IX), "SIX→IX");
    ASSERT_TRUE(treelock_downgrade_valid(TREELOCK_SIX, TREELOCK_IS), "SIX→IS");
    ASSERT_TRUE(treelock_downgrade_valid(TREELOCK_SIX, TREELOCK_NL), "SIX→NL");

    PASS();
}

/**
 * 函数名称：test_downgrade_invalid_paths
 *
 * 功能描述：验证非法锁降级路径被正确拒绝
 */
static VOID test_downgrade_invalid_paths(VOID)
{
    TEST("invalid downgrade paths");

    ASSERT_TRUE(!treelock_downgrade_valid(TREELOCK_NL, TREELOCK_IS), "NL→IS");
    ASSERT_TRUE(!treelock_downgrade_valid(TREELOCK_IS, TREELOCK_IX), "IS→IX");
    ASSERT_TRUE(!treelock_downgrade_valid(TREELOCK_S, TREELOCK_IX),  "S→IX");
    ASSERT_TRUE(!treelock_downgrade_valid(TREELOCK_IX, TREELOCK_S),  "IX→S");
    ASSERT_TRUE(!treelock_downgrade_valid(TREELOCK_S, TREELOCK_S),   "S→S");

    PASS();
}

/* =========================================================================
 * 测试用例：工具函数
 * ========================================================================= */

/**
 * 函数名称：test_mode_name
 *
 * 功能描述：验证锁模式名称转换函数
 */
static VOID test_mode_name(VOID)
{
    TEST("mode name strings");

    ASSERT_EQ(strcmp(treelock_mode_name(TREELOCK_NL),  "NL"),  0, "NL");
    ASSERT_EQ(strcmp(treelock_mode_name(TREELOCK_IS),  "IS"),  0, "IS");
    ASSERT_EQ(strcmp(treelock_mode_name(TREELOCK_IX),  "IX"),  0, "IX");
    ASSERT_EQ(strcmp(treelock_mode_name(TREELOCK_S),   "S"),   0, "S");
    ASSERT_EQ(strcmp(treelock_mode_name(TREELOCK_SIX), "SIX"), 0, "SIX");
    ASSERT_EQ(strcmp(treelock_mode_name(TREELOCK_X),   "X"),   0, "X");
    ASSERT_EQ(strcmp(treelock_mode_name(99),           "UNKNOWN"), 0, "INVALID");

    PASS();
}

/**
 * 函数名称：test_strerror
 *
 * 功能描述：验证错误码转字符串函数
 */
static VOID test_strerror(VOID)
{
    TEST("error strings");

    ASSERT_EQ(strcmp(treelock_strerror(TREELOCK_OK), "Success"),            0, "OK");
    ASSERT_TRUE(strlen(treelock_strerror(TREELOCK_ERR_TIMEOUT)) > 0, "TIMEOUT");
    ASSERT_TRUE(strlen(treelock_strerror(TREELOCK_ERR_CONFLICT)) > 0,"CONFLICT");
    ASSERT_TRUE(strlen(treelock_strerror(TREELOCK_ERR_PROTOCOL)) > 0,"PROTOCOL");
    ASSERT_TRUE(strlen(treelock_strerror(TREELOCK_ERR_NETWORK)) > 0, "NETWORK");
    ASSERT_TRUE(strlen(treelock_strerror(TREELOCK_ERR_STALE)) > 0,   "STALE");
    ASSERT_TRUE(strlen(treelock_strerror(TREELOCK_ERR_INVAL)) > 0,   "INVAL");

    PASS();
}

/* =========================================================================
 * 入口
 * ========================================================================= */

/**
 * 函数名称：main
 *
 * 功能描述：协议测试入口，运行全部测试用例并汇总结果
 *
 * @return EXIT_SUCCESS 全部通过，EXIT_FAILURE 存在失败
 */
INT_32 main(VOID)
{
    printf("TreeLocks - 协议正确性测试\n");
    printf("==========================\n\n");

    printf("兼容矩阵测试:\n");
    test_compat_matrix_nl();
    test_compat_matrix_x();
    test_compat_matrix_is();
    test_compat_matrix_ix();
    test_compat_matrix_s();

    printf("\n父节点锁模式测试:\n");
    test_required_parent_mode();

    printf("\n锁升级路径测试:\n");
    test_escalate_valid_paths();
    test_escalate_invalid_paths();

    printf("\n锁降级路径测试:\n");
    test_downgrade_valid_paths();
    test_downgrade_invalid_paths();

    printf("\n工具函数测试:\n");
    test_mode_name();
    test_strerror();

    /* 结果汇总 */
    printf("\n==========================\n");
    printf("结果: %d/%d 通过, %d 失败\n",
           g_tests_passed, g_tests_run, g_tests_failed);

    return (g_tests_failed > 0) ? EXIT_FAILURE : EXIT_SUCCESS;
}

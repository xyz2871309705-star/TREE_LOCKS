/*
 * TreeLocks - 日志模块测试
 *
 * 测试日志文件输出功能，包括：
 *   - 文件输出基本功能
 *   - 无 ANSI 颜色码验证
 *   - 各级别日志输出
 *   - 文件切换与关闭
 *   - 边界条件
 *   - 与自定义回调共存
 *
 * 版本: 0.1.0
 * 日期: 2026-06-13
 */

#include "treelock_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

#define ASSERT_STR_CONTAINS(haystack, needle, msg) \
    do { \
        if (strstr((haystack), (needle)) == NULL) { \
            CHAR _buf[512]; \
            snprintf(_buf, sizeof(_buf), \
                     "%s (string '%s' not found in '%s')", \
                     msg, (needle), (haystack)); \
            test_fail(_buf); return; \
        } \
    } while (0)

#define ASSERT_STR_NOT_CONTAINS(haystack, needle, msg) \
    do { \
        if (strstr((haystack), (needle)) != NULL) { \
            CHAR _buf[512]; \
            snprintf(_buf, sizeof(_buf), \
                     "%s (string '%s' unexpectedly found in '%s')", \
                     msg, (needle), (haystack)); \
            test_fail(_buf); return; \
        } \
    } while (0)

/* =========================================================================
 * 测试辅助函数
 * ========================================================================= */

/** 测试用的日志文件名 */
#define TEST_LOG_FILE "test_log_output.tmp"

/**
 * 函数名称：_read_file_content
 *
 * 功能描述：读取文件全部内容到静态缓冲区
 *
 * @param[IN]  filename - 文件路径
 * @param[OUT] out_size - 输出文件大小（可为 NULL）
 *
 * @return 文件内容字符串，失败返回 NULL
 */
static CHAR *_read_file_content(
    IN  CSTR_PTR  filename,
    OUT UINT_32  *out_size)
{
    static CHAR buf[8192];
    FILE *fp;
    size_t n;

    if (out_size != NULL) {
        *out_size = 0;
    }

    fp = fopen(filename, "r");
    if (fp == NULL) {
        return NULL;
    }

    n = fread(buf, 1, sizeof(buf) - 1, fp);
    buf[n] = '\0';

    if (out_size != NULL) {
        *out_size = (UINT_32)n;
    }

    fclose(fp);
    return buf;
}

/**
 * 函数名称：_delete_file
 *
 * 功能描述：删除临时测试文件
 *
 * @param[IN] filename - 文件路径
 */
static VOID _delete_file(
    IN CSTR_PTR filename)
{
    remove(filename);
}

/* =========================================================================
 * 测试用例：基本文件输出
 * ========================================================================= */

/**
 * 函数名称：test_file_output_basic
 *
 * 功能描述：验证日志写入文件的基本功能
 *
 *          设置文件 → 写日志 → 关闭文件 → 读取验证内容
 */
static VOID test_file_output_basic(VOID)
{
    BOOL  ok;
    CHAR *content;

    TEST("basic file output");

    /* 清理旧文件 */
    _delete_file(TEST_LOG_FILE);

    /* 设置日志文件 */
    ok = treelock_log_set_file(TEST_LOG_FILE);
    ASSERT_TRUE(ok, "set_file should succeed");

    /* 临时降低等级以确保 INFO 日志可以输出 */
    treelock_log_set_level(TREELOCK_LOG_TRACE);

    /* 写入一条 INFO 日志 */
    TREELOCK_LOG_INFO("TEST", "hello from test_file_output_basic");

    /* 恢复等级 */
    treelock_log_set_level(TREELOCK_LOG_WARN);

    /* 检查文件路径 */
    ASSERT_TRUE(treelock_log_get_file() != NULL, "get_file should return path");

    /* 关闭文件（确保刷新） */
    treelock_log_close_file();

    /* 读取并验证文件内容 */
    content = _read_file_content(TEST_LOG_FILE, NULL);
    ASSERT_TRUE(content != NULL, "log file should exist");

    ASSERT_STR_CONTAINS(content, "[INFO ]",     "should contain INFO level");
    ASSERT_STR_CONTAINS(content, "[TEST    ]",  "should contain TEST tag");
    ASSERT_STR_CONTAINS(content, "hello from test_file_output_basic",
                        "should contain log message");

    /* 清理 */
    _delete_file(TEST_LOG_FILE);
    PASS();
}

/**
 * 函数名称：test_file_output_no_ansi
 *
 * 功能描述：验证日志文件中不含 ANSI 颜色转义码
 */
static VOID test_file_output_no_ansi(VOID)
{
    BOOL  ok;
    CHAR *content;

    TEST("file output has no ANSI codes");

    _delete_file(TEST_LOG_FILE);

    ok = treelock_log_set_file(TEST_LOG_FILE);
    ASSERT_TRUE(ok, "set_file should succeed");

    /* 写入不同级别的日志（每个级别都产生 ANSI 颜色码到 stderr） */
    TREELOCK_LOG_ERROR("TEST", "error message with color");
    TREELOCK_LOG_WARN("TEST",  "warning message");
    TREELOCK_LOG_INFO("TEST",  "info message");

    treelock_log_close_file();

    content = _read_file_content(TEST_LOG_FILE, NULL);
    ASSERT_TRUE(content != NULL, "log file should exist");

    /* 验证没有 ANSI 转义码 (\033 即 ESC 字符 0x1B) */
    ASSERT_STR_NOT_CONTAINS(content, "\033", "should not contain ANSI escape");
    ASSERT_STR_NOT_CONTAINS(content, "[1;31", "should not contain color code");
    ASSERT_STR_NOT_CONTAINS(content, "[0;3",  "should not contain color code");

    _delete_file(TEST_LOG_FILE);
    PASS();
}

/**
 * 函数名称：test_file_output_levels
 *
 * 功能描述：验证所有等级的日志都能正确写入文件
 */
static VOID test_file_output_levels(VOID)
{
    BOOL  ok;
    CHAR *content;

    TEST("all log levels to file");

    _delete_file(TEST_LOG_FILE);

    ok = treelock_log_set_file(TEST_LOG_FILE);
    ASSERT_TRUE(ok, "set_file should succeed");

    /* 确保等级设为 TRACE 以输出所有日志 */
    treelock_log_set_level(TREELOCK_LOG_TRACE);

    /* 每个等级写一条 */
    TREELOCK_LOG_FATAL("LVL", "fatal test");
    TREELOCK_LOG_ERROR("LVL", "error test");
    TREELOCK_LOG_WARN("LVL",  "warn test");
    TREELOCK_LOG_INFO("LVL",  "info test");
    TREELOCK_LOG_DEBUG("LVL", "debug test");
    TREELOCK_LOG_TRACE("LVL", "trace test");

    treelock_log_close_file();

    content = _read_file_content(TEST_LOG_FILE, NULL);
    ASSERT_TRUE(content != NULL, "log file should exist");

    ASSERT_STR_CONTAINS(content, "[FATAL]", "should contain FATAL");
    ASSERT_STR_CONTAINS(content, "[ERROR]", "should contain ERROR");
    ASSERT_STR_CONTAINS(content, "[WARN ]", "should contain WARN");
    ASSERT_STR_CONTAINS(content, "[INFO ]", "should contain INFO");
    ASSERT_STR_CONTAINS(content, "[DEBUG]", "should contain DEBUG");
    ASSERT_STR_CONTAINS(content, "[TRACE]", "should contain TRACE");

    _delete_file(TEST_LOG_FILE);
    PASS();
}

/**
 * 函数名称：test_file_output_respects_level
 *
 * 功能描述：验证文件输出遵守运行期日志等级过滤
 */
static VOID test_file_output_respects_level(VOID)
{
    BOOL  ok;
    CHAR *content;

    TEST("file output respects log level");

    _delete_file(TEST_LOG_FILE);

    /* 只输出 WARN 及以上 */
    treelock_log_set_level(TREELOCK_LOG_WARN);
    ok = treelock_log_set_file(TEST_LOG_FILE);
    ASSERT_TRUE(ok, "set_file should succeed");

    TREELOCK_LOG_ERROR("LVL", "this error should appear");
    TREELOCK_LOG_WARN("LVL",  "this warning should appear");
    TREELOCK_LOG_INFO("LVL",  "this info should NOT appear");
    TREELOCK_LOG_DEBUG("LVL", "this debug should NOT appear");

    treelock_log_close_file();

    content = _read_file_content(TEST_LOG_FILE, NULL);
    ASSERT_TRUE(content != NULL, "log file should exist");

    ASSERT_STR_CONTAINS(content,    "this error should appear",
                        "error should be in file");
    ASSERT_STR_CONTAINS(content,    "this warning should appear",
                        "warning should be in file");
    ASSERT_STR_NOT_CONTAINS(content, "this info should NOT appear",
                            "info should not be in file");
    ASSERT_STR_NOT_CONTAINS(content, "this debug should NOT appear",
                            "debug should not be in file");

    /* 恢复等级 */
    treelock_log_set_level(TREELOCK_LOG_TRACE);
    _delete_file(TEST_LOG_FILE);
    PASS();
}

/* =========================================================================
 * 测试用例：文件切换与关闭
 * ========================================================================= */

/**
 * 函数名称：test_file_switch
 *
 * 功能描述：验证切换日志文件后旧文件关闭、新文件正常写入
 */
static VOID test_file_switch(VOID)
{
    BOOL  ok;
    CHAR *content1;
    CHAR *content2;
    UINT_32 size1_before, size1_after;

    TEST("switch log file");

    _delete_file("test_log_A.tmp");
    _delete_file("test_log_B.tmp");

    /* 写文件 A */
    ok = treelock_log_set_file("test_log_A.tmp");
    ASSERT_TRUE(ok, "set_file A should succeed");
    TREELOCK_LOG_INFO("SW", "message for file A");

    /* 记录文件 A 当前大小 */
    content1 = _read_file_content("test_log_A.tmp", &size1_before);
    ASSERT_TRUE(content1 != NULL, "file A should exist");

    /* 切换到文件 B */
    ok = treelock_log_set_file("test_log_B.tmp");
    ASSERT_TRUE(ok, "set_file B should succeed");
    TREELOCK_LOG_INFO("SW", "message for file B");

    /* 再写一条到 A（确认 A 已关闭，不应有新内容） */
    treelock_log_close_file();

    /* 验证文件 A 大小未变（切换到 B 后不再写入 A） */
    content1 = _read_file_content("test_log_A.tmp", &size1_after);
    ASSERT_TRUE(content1 != NULL, "file A should still exist");
    ASSERT_EQ(size1_after, size1_before,
              "file A should not grow after switching to B");

    /* 验证文件 B 有内容 */
    content2 = _read_file_content("test_log_B.tmp", NULL);
    ASSERT_TRUE(content2 != NULL, "file B should exist");
    ASSERT_STR_CONTAINS(content2, "message for file B",
                        "file B should contain its message");

    /* 清理 */
    _delete_file("test_log_A.tmp");
    _delete_file("test_log_B.tmp");
    PASS();
}

/**
 * 函数名称：test_file_close
 *
 * 功能描述：验证关闭文件后不再写入
 */
static VOID test_file_close(VOID)
{
    BOOL  ok;
    CHAR *content;
    UINT_32 size_before, size_after;

    TEST("close log file stops output");

    _delete_file(TEST_LOG_FILE);

    /* 打开并写入 */
    ok = treelock_log_set_file(TEST_LOG_FILE);
    ASSERT_TRUE(ok, "set_file should succeed");
    TREELOCK_LOG_INFO("CLOSE", "before close");

    /* 关闭 */
    treelock_log_close_file();
    ASSERT_TRUE(treelock_log_get_file() == NULL,
                "get_file should return NULL after close");

    /* 记录当前大小 */
    content = _read_file_content(TEST_LOG_FILE, &size_before);
    ASSERT_TRUE(content != NULL, "file should exist");
    ASSERT_STR_CONTAINS(content, "before close",
                        "should contain message before close");

    /* 再写一条（不应进入文件） */
    TREELOCK_LOG_INFO("CLOSE", "after close - should not appear");

    content = _read_file_content(TEST_LOG_FILE, &size_after);
    ASSERT_TRUE(content != NULL, "file should still exist");
    ASSERT_EQ(size_after, size_before,
              "file size should not change after close");

    _delete_file(TEST_LOG_FILE);
    PASS();
}

/* =========================================================================
 * 测试用例：边界条件
 * ========================================================================= */

/**
 * 函数名称：test_file_null_safety
 *
 * 功能描述：验证 NULL 和空字符串参数的安全性
 */
static VOID test_file_null_safety(VOID)
{
    BOOL ok;

    TEST("null and empty filename safety");

    /* NULL 应该成功（关闭文件） */
    ok = treelock_log_set_file(NULL);
    ASSERT_TRUE(ok, "set_file(NULL) should succeed");

    ASSERT_TRUE(treelock_log_get_file() == NULL,
                "get_file should return NULL after set_file(NULL)");

    /* 空字符串应该成功（关闭文件） */
    ok = treelock_log_set_file("");
    ASSERT_TRUE(ok, "set_file(\"\") should succeed");

    ASSERT_TRUE(treelock_log_get_file() == NULL,
                "get_file should return NULL after set_file(\"\")");

    /* 重复 close 不应崩溃 */
    treelock_log_close_file();
    treelock_log_close_file();

    ASSERT_TRUE(treelock_log_get_file() == NULL,
                "get_file should still be NULL");

    PASS();
}

/**
 * 函数名称：test_file_invalid_path
 *
 * 功能描述：验证无效路径时返回失败
 */
static VOID test_file_invalid_path(VOID)
{
    BOOL ok;

    TEST("invalid file path returns failure");

#ifdef TREELOCK_PLATFORM_WINDOWS
    /* Windows 上的无效路径 */
    ok = treelock_log_set_file("Z:\\nonexistent\\path\\that\\should\\fail\\log.txt");
#else
    /* Linux 上的无效路径 */
    ok = treelock_log_set_file("/nonexistent/path/that/should/fail/log.txt");
#endif
    ASSERT_TRUE(!ok, "set_file with invalid path should fail");

    /* 之前如果有有效文件，应该保持不变 */
    /* (此处不做强断言，因为之前未设置任何文件) */

    PASS();
}

/**
 * 函数名称：test_file_get_path
 *
 * 功能描述：验证 treelock_log_get_file 返回正确的路径
 */
static VOID test_file_get_path(VOID)
{
    BOOL    ok;
    CSTR_PTR path;

    TEST("get file path");

    _delete_file(TEST_LOG_FILE);

    /* 未设置时返回 NULL */
    path = treelock_log_get_file();
    ASSERT_TRUE(path == NULL, "should return NULL before setting");

    /* 设置文件后返回路径 */
    ok = treelock_log_set_file(TEST_LOG_FILE);
    ASSERT_TRUE(ok, "set_file should succeed");

    path = treelock_log_get_file();
    ASSERT_TRUE(path != NULL, "should return path after setting");
    ASSERT_STR_CONTAINS(path, "test_log_output.tmp",
                        "path should contain filename");

    /* 关闭后返回 NULL */
    treelock_log_close_file();
    path = treelock_log_get_file();
    ASSERT_TRUE(path == NULL, "should return NULL after close");

    _delete_file(TEST_LOG_FILE);
    PASS();
}

/**
 * 函数名称：test_file_concurrent_write
 *
 * 功能描述：验证同一日志消息正确写入文件（线程安全基础测试）
 *
 *          此测试不创建多线程，但验证在加锁机制下
 *          每条日志的完整性（不会出现交错）。
 */
static VOID test_file_concurrent_write(VOID)
{
    BOOL  ok;
    CHAR *content;
    INT_32 i;

    TEST("concurrent write integrity");

    _delete_file(TEST_LOG_FILE);

    ok = treelock_log_set_file(TEST_LOG_FILE);
    ASSERT_TRUE(ok, "set_file should succeed");

    /* 快速连续写多条日志 */
    for (i = 0; i < 50; i++) {
        TREELOCK_LOG_INFO("CW", "message number %d", i);
    }

    treelock_log_close_file();

    content = _read_file_content(TEST_LOG_FILE, NULL);
    ASSERT_TRUE(content != NULL, "log file should exist");

    /* 验证每条日志都完整（检查第一条和最后一条） */
    ASSERT_STR_CONTAINS(content, "message number 0",  "should contain first");
    ASSERT_STR_CONTAINS(content, "message number 49", "should contain last");

    _delete_file(TEST_LOG_FILE);
    PASS();
}

/* =========================================================================
 * 测试用例：与自定义回调共存
 * ========================================================================= */

/** 自定义回调测试用的全局变量 */
static INT_32 g_callback_call_count = 0;

/**
 * 函数名称：_test_callback
 *
 * 功能描述：测试用自定义回调，统计被调用次数
 *
 * @param[IN] level     - 日志等级
 * @param[IN] tag       - 模块标签
 * @param[IN] file      - 源文件名
 * @param[IN] line      - 行号
 * @param[IN] func_name - 函数名
 * @param[IN] message   - 格式化后的消息
 * @param[IN] user_data - 用户数据
 */
static VOID _test_callback(
    IN treelock_log_level_t  level,
    IN CSTR_PTR              tag,
    IN CSTR_PTR              file,
    IN INT_32                line,
    IN CSTR_PTR              func_name,
    IN CSTR_PTR              message,
    IN PTR_VOID              user_data)
{
    UNUSED_PARAM(level);
    UNUSED_PARAM(tag);
    UNUSED_PARAM(file);
    UNUSED_PARAM(line);
    UNUSED_PARAM(func_name);
    UNUSED_PARAM(message);
    UNUSED_PARAM(user_data);

    g_callback_call_count++;
}

/**
 * 函数名称：test_file_with_callback
 *
 * 功能描述：验证文件输出与自定义回调可以共存
 *
 *          注册自定义回调后，日志应同时：
 *          1. 调用用户回调
 *          2. 写入文件
 */
static VOID test_file_with_callback(VOID)
{
    BOOL  ok;
    CHAR *content;

    TEST("file output with custom callback");

    _delete_file(TEST_LOG_FILE);

    /* 开启文件输出 */
    ok = treelock_log_set_file(TEST_LOG_FILE);
    ASSERT_TRUE(ok, "set_file should succeed");

    /* 注册自定义回调 */
    g_callback_call_count = 0;
    treelock_log_set_callback(_test_callback, NULL);

    /* 写日志 */
    TREELOCK_LOG_INFO("CB", "callback and file test");

    /* 验证回调被调用 */
    ASSERT_EQ(g_callback_call_count, 1,
              "callback should be called once");

    /* 恢复默认回调 */
    treelock_log_set_callback(NULL, NULL);

    treelock_log_close_file();

    /* 验证文件也有内容 */
    content = _read_file_content(TEST_LOG_FILE, NULL);
    ASSERT_TRUE(content != NULL, "log file should exist");
    ASSERT_STR_CONTAINS(content, "callback and file test",
                        "file should contain message");

    _delete_file(TEST_LOG_FILE);
    PASS();
}

/**
 * 函数名称：test_file_timestamp_format
 *
 * 功能描述：验证文件中的时间戳格式正确
 *
 *          格式应为: YYYY-MM-DD HH:MM:SS.mmm
 */
static VOID test_file_timestamp_format(VOID)
{
    BOOL  ok;
    CHAR *content;
    CHAR *ts_start;
    CHAR *ts_end;
    INT_32 dash_count;
    INT_32 colon_count;
    INT_32 dot_count;
    CHAR *p;

    TEST("timestamp format in file");

    _delete_file(TEST_LOG_FILE);

    ok = treelock_log_set_file(TEST_LOG_FILE);
    ASSERT_TRUE(ok, "set_file should succeed");

    TREELOCK_LOG_INFO("TS", "timestamp test");

    treelock_log_close_file();

    content = _read_file_content(TEST_LOG_FILE, NULL);
    ASSERT_TRUE(content != NULL, "log file should exist");

    /* 提取时间戳部分：[YYYY-MM-DD HH:MM:SS.mmm] */
    ts_start = strchr(content, '[');
    ASSERT_TRUE(ts_start != NULL, "should have opening bracket");
    ts_start++; /* 跳过 '[' */

    ts_end = strchr(ts_start, ']');
    ASSERT_TRUE(ts_end != NULL, "should have closing bracket");

    /* 验证格式 YYYY-MM-DD HH:MM:SS.mmm */
    /* 计算分隔符 */
    dash_count = 0;
    colon_count = 0;
    dot_count = 0;
    for (p = ts_start; p < ts_end; p++) {
        if (*p == '-') {
            dash_count++;
        } else if (*p == ':') {
            colon_count++;
        } else if (*p == '.') {
            dot_count++;
        }
    }
    ASSERT_EQ(dash_count, 2, "should have 2 dashes (YYYY-MM-DD)");
    ASSERT_EQ(colon_count, 2, "should have 2 colons (HH:MM:SS)");
    ASSERT_EQ(dot_count, 1, "should have 1 dot (SS.mmm)");

    _delete_file(TEST_LOG_FILE);
    PASS();
}

/* =========================================================================
 * 入口
 * ========================================================================= */

/**
 * 函数名称：main
 *
 * 功能描述：日志模块测试入口，运行全部测试用例并汇总结果
 *
 * @return EXIT_SUCCESS 全部通过，EXIT_FAILURE 存在失败
 */
INT_32 main(VOID)
{
    /* 测试期间抑制默认 stderr 输出噪音 */
    treelock_log_set_level(TREELOCK_LOG_WARN);

    printf("TreeLocks - 日志模块测试\n");
    printf("==========================\n\n");

    printf("基本文件输出测试:\n");
    test_file_output_basic();
    test_file_output_no_ansi();
    test_file_output_levels();
    test_file_output_respects_level();

    printf("\n文件切换与关闭测试:\n");
    test_file_switch();
    test_file_close();

    printf("\n边界条件测试:\n");
    test_file_null_safety();
    test_file_invalid_path();
    test_file_get_path();
    test_file_concurrent_write();

    printf("\n与自定义回调共存测试:\n");
    test_file_with_callback();

    printf("\n时间戳格式测试:\n");
    test_file_timestamp_format();

    /* 清理残留的临时文件 */
    _delete_file(TEST_LOG_FILE);
    _delete_file("test_log_A.tmp");
    _delete_file("test_log_B.tmp");

    /* 结果汇总 */
    printf("\n==========================\n");
    printf("结果: %d/%d 通过, %d 失败\n",
           g_tests_passed, g_tests_run, g_tests_failed);

    return (g_tests_failed > 0) ? EXIT_FAILURE : EXIT_SUCCESS;
}

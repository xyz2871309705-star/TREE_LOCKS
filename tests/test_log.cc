/*
 * TreeLocks — 日志模块测试 (GTest)
 *
 * 测试文件输出、日志等级过滤、文件切换、回调共存等。
 *
 * 版本: 0.2.0
 * 日期: 2026-06-13
 */

#include <gtest/gtest.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>

extern "C" {
#include "treelock_log.h"
}

#define TEST_LOG_FILE "test_log_output.tmp"

/* =========================================================================
 * 辅助函数
 * ========================================================================= */

static char *read_file_content(const char *filename, unsigned int *out_size = nullptr) {
    static char buf[8192];
    if (out_size) *out_size = 0;

    FILE *fp = fopen(filename, "r");
    if (!fp) return nullptr;

    size_t n = fread(buf, 1, sizeof(buf) - 1, fp);
    buf[n] = '\0';
    if (out_size) *out_size = (unsigned int)n;
    fclose(fp);
    return buf;
}

static void delete_file(const char *filename) {
    remove(filename);
}

class LogFileTest : public ::testing::Test {
protected:
    void SetUp() override {
        delete_file(TEST_LOG_FILE);
        delete_file("test_log_A.tmp");
        delete_file("test_log_B.tmp");
    }
    void TearDown() override {
        delete_file(TEST_LOG_FILE);
        delete_file("test_log_A.tmp");
        delete_file("test_log_B.tmp");
    }
};

/* =========================================================================
 * 基本文件输出
 * ========================================================================= */

TEST_F(LogFileTest, BasicFileOutput)
{
    ASSERT_TRUE(treelock_log_set_file(TEST_LOG_FILE));
    treelock_log_set_level(TREELOCK_LOG_TRACE);

    TREELOCK_LOG_INFO("TEST", "hello from test_file_output_basic");

    treelock_log_set_level(TREELOCK_LOG_WARN);
    EXPECT_NE(treelock_log_get_file(), nullptr);
    treelock_log_close_file();

    char *content = read_file_content(TEST_LOG_FILE);
    ASSERT_NE(content, nullptr);
    EXPECT_NE(strstr(content, "[INFO ]"), nullptr);
    EXPECT_NE(strstr(content, "[TEST    ]"), nullptr);
    EXPECT_NE(strstr(content, "hello from test_file_output_basic"), nullptr);
}

TEST_F(LogFileTest, NoAnsiCodes)
{
    ASSERT_TRUE(treelock_log_set_file(TEST_LOG_FILE));

    TREELOCK_LOG_ERROR("TEST", "error message with color");
    TREELOCK_LOG_WARN("TEST",  "warning message");
    TREELOCK_LOG_INFO("TEST",  "info message");
    treelock_log_close_file();

    char *content = read_file_content(TEST_LOG_FILE);
    ASSERT_NE(content, nullptr);
    EXPECT_EQ(strstr(content, "\033"), nullptr);
    EXPECT_EQ(strstr(content, "[1;31"), nullptr);
    EXPECT_EQ(strstr(content, "[0;3"),  nullptr);
}

TEST_F(LogFileTest, AllLevels)
{
    ASSERT_TRUE(treelock_log_set_file(TEST_LOG_FILE));
    treelock_log_set_level(TREELOCK_LOG_TRACE);

    TREELOCK_LOG_FATAL("LVL", "fatal test");
    TREELOCK_LOG_ERROR("LVL", "error test");
    TREELOCK_LOG_WARN("LVL",  "warn test");
    TREELOCK_LOG_INFO("LVL",  "info test");
    TREELOCK_LOG_DEBUG("LVL", "debug test");
    TREELOCK_LOG_TRACE("LVL", "trace test");
    treelock_log_close_file();

    char *content = read_file_content(TEST_LOG_FILE);
    ASSERT_NE(content, nullptr);
    EXPECT_NE(strstr(content, "[FATAL]"), nullptr);
    EXPECT_NE(strstr(content, "[ERROR]"), nullptr);
    EXPECT_NE(strstr(content, "[WARN ]"), nullptr);
    EXPECT_NE(strstr(content, "[INFO ]"), nullptr);
    EXPECT_NE(strstr(content, "[DEBUG]"), nullptr);
    EXPECT_NE(strstr(content, "[TRACE]"), nullptr);
}

TEST_F(LogFileTest, RespectsLogLevel)
{
    treelock_log_set_level(TREELOCK_LOG_WARN);
    ASSERT_TRUE(treelock_log_set_file(TEST_LOG_FILE));

    TREELOCK_LOG_ERROR("LVL", "this error should appear");
    TREELOCK_LOG_WARN("LVL",  "this warning should appear");
    TREELOCK_LOG_INFO("LVL",  "this info should NOT appear");
    TREELOCK_LOG_DEBUG("LVL", "this debug should NOT appear");
    treelock_log_close_file();

    char *content = read_file_content(TEST_LOG_FILE);
    ASSERT_NE(content, nullptr);
    EXPECT_NE(strstr(content, "this error should appear"), nullptr);
    EXPECT_NE(strstr(content, "this warning should appear"), nullptr);
    EXPECT_EQ(strstr(content, "this info should NOT appear"), nullptr);
    EXPECT_EQ(strstr(content, "this debug should NOT appear"), nullptr);

    treelock_log_set_level(TREELOCK_LOG_TRACE);
}

/* =========================================================================
 * 文件切换与关闭
 * ========================================================================= */

TEST_F(LogFileTest, SwitchFile)
{
    ASSERT_TRUE(treelock_log_set_file("test_log_A.tmp"));
    TREELOCK_LOG_INFO("SW", "message for file A");

    unsigned int size_before;
    char *content1 = read_file_content("test_log_A.tmp", &size_before);
    ASSERT_NE(content1, nullptr);

    ASSERT_TRUE(treelock_log_set_file("test_log_B.tmp"));
    TREELOCK_LOG_INFO("SW", "message for file B");
    treelock_log_close_file();

    unsigned int size_after;
    content1 = read_file_content("test_log_A.tmp", &size_after);
    ASSERT_NE(content1, nullptr);
    EXPECT_EQ(size_after, size_before) << "file A should not grow after switch";

    char *content2 = read_file_content("test_log_B.tmp");
    ASSERT_NE(content2, nullptr);
    EXPECT_NE(strstr(content2, "message for file B"), nullptr);
}

TEST_F(LogFileTest, CloseStopsOutput)
{
    ASSERT_TRUE(treelock_log_set_file(TEST_LOG_FILE));
    TREELOCK_LOG_INFO("CLOSE", "before close");
    treelock_log_close_file();
    EXPECT_EQ(treelock_log_get_file(), nullptr);

    unsigned int size_before;
    char *content = read_file_content(TEST_LOG_FILE, &size_before);
    ASSERT_NE(content, nullptr);
    EXPECT_NE(strstr(content, "before close"), nullptr);

    TREELOCK_LOG_INFO("CLOSE", "after close - should not appear");

    unsigned int size_after;
    content = read_file_content(TEST_LOG_FILE, &size_after);
    ASSERT_NE(content, nullptr);
    EXPECT_EQ(size_after, size_before);
}

/* =========================================================================
 * 边界条件
 * ========================================================================= */

TEST_F(LogFileTest, NullAndEmptySafety)
{
    EXPECT_TRUE(treelock_log_set_file(nullptr));
    EXPECT_EQ(treelock_log_get_file(), nullptr);

    EXPECT_TRUE(treelock_log_set_file(""));
    EXPECT_EQ(treelock_log_get_file(), nullptr);

    /* double close should not crash */
    treelock_log_close_file();
    treelock_log_close_file();
    EXPECT_EQ(treelock_log_get_file(), nullptr);
}

TEST_F(LogFileTest, InvalidPath)
{
#ifdef TREELOCK_PLATFORM_WINDOWS
    EXPECT_FALSE(treelock_log_set_file("Z:\\nonexistent\\path\\that\\should\\fail\\log.txt"));
#else
    EXPECT_FALSE(treelock_log_set_file("/nonexistent/path/that/should/fail/log.txt"));
#endif
}

TEST_F(LogFileTest, GetFilePath)
{
    EXPECT_EQ(treelock_log_get_file(), nullptr);

    ASSERT_TRUE(treelock_log_set_file(TEST_LOG_FILE));
    const char *path = treelock_log_get_file();
    ASSERT_NE(path, nullptr);
    EXPECT_NE(strstr(path, "test_log_output.tmp"), nullptr);

    treelock_log_close_file();
    EXPECT_EQ(treelock_log_get_file(), nullptr);
}

TEST_F(LogFileTest, WriteIntegrity)
{
    ASSERT_TRUE(treelock_log_set_file(TEST_LOG_FILE));

    for (int i = 0; i < 50; i++) {
        TREELOCK_LOG_INFO("CW", "message number %d", i);
    }
    treelock_log_close_file();

    char *content = read_file_content(TEST_LOG_FILE);
    ASSERT_NE(content, nullptr);
    EXPECT_NE(strstr(content, "message number 0"),  nullptr);
    EXPECT_NE(strstr(content, "message number 49"), nullptr);
}

/* =========================================================================
 * 回调共存
 * ========================================================================= */

static int g_callback_call_count = 0;

static void test_callback(
    treelock_log_level_t  level,
    const char           *tag,
    const char           *file,
    int                   line,
    const char           *func_name,
    const char           *message,
    void                 *user_data)
{
    (void)level; (void)tag; (void)file; (void)line;
    (void)func_name; (void)message; (void)user_data;
    g_callback_call_count++;
}

TEST_F(LogFileTest, FileWithCallback)
{
    ASSERT_TRUE(treelock_log_set_file(TEST_LOG_FILE));
    g_callback_call_count = 0;
    treelock_log_set_callback(test_callback, nullptr);

    TREELOCK_LOG_INFO("CB", "callback and file test");

    EXPECT_EQ(g_callback_call_count, 1);
    treelock_log_set_callback(nullptr, nullptr);
    treelock_log_close_file();

    char *content = read_file_content(TEST_LOG_FILE);
    ASSERT_NE(content, nullptr);
    EXPECT_NE(strstr(content, "callback and file test"), nullptr);
}

/* =========================================================================
 * 时间戳格式
 * ========================================================================= */

TEST_F(LogFileTest, TimestampFormat)
{
    ASSERT_TRUE(treelock_log_set_file(TEST_LOG_FILE));
    TREELOCK_LOG_INFO("TS", "timestamp test");
    treelock_log_close_file();

    char *content = read_file_content(TEST_LOG_FILE);
    ASSERT_NE(content, nullptr);

    char *ts_start = strchr(content, '[');
    ASSERT_NE(ts_start, nullptr);
    ts_start++;

    char *ts_end = strchr(ts_start, ']');
    ASSERT_NE(ts_end, nullptr);

    int dashes = 0, colons = 0, dots = 0;
    for (char *p = ts_start; p < ts_end; p++) {
        if (*p == '-') dashes++;
        else if (*p == ':') colons++;
        else if (*p == '.') dots++;
    }
    EXPECT_EQ(dashes, 2);
    EXPECT_EQ(colons, 2);
    EXPECT_EQ(dots,   1);
}

/* =========================================================================
 * 等级名称
 * ========================================================================= */

TEST_F(LogFileTest, LevelName)
{
    EXPECT_STREQ(treelock_log_level_name(TREELOCK_LOG_OFF),   "OFF");
    EXPECT_STREQ(treelock_log_level_name(TREELOCK_LOG_FATAL), "FATAL");
    EXPECT_STREQ(treelock_log_level_name(TREELOCK_LOG_ERROR), "ERROR");
    EXPECT_STREQ(treelock_log_level_name(TREELOCK_LOG_WARN),  "WARN");
    EXPECT_STREQ(treelock_log_level_name(TREELOCK_LOG_INFO),  "INFO");
    EXPECT_STREQ(treelock_log_level_name(TREELOCK_LOG_DEBUG), "DEBUG");
    EXPECT_STREQ(treelock_log_level_name(TREELOCK_LOG_TRACE), "TRACE");
    EXPECT_STREQ(treelock_log_level_name((treelock_log_level_t)99), "UNKNOWN");
}

/* =========================================================================
 * Get/Set Level 往返
 * ========================================================================= */

TEST_F(LogFileTest, GetSetLevelRoundtrip)
{
    treelock_log_set_level(TREELOCK_LOG_WARN);
    EXPECT_EQ(treelock_log_get_level(), TREELOCK_LOG_WARN);

    treelock_log_set_level(TREELOCK_LOG_TRACE);
    EXPECT_EQ(treelock_log_get_level(), TREELOCK_LOG_TRACE);

    treelock_log_set_level(TREELOCK_LOG_OFF);
    EXPECT_EQ(treelock_log_get_level(), TREELOCK_LOG_OFF);

    /* 恢复默认以便后续测试 */
    treelock_log_set_level(TREELOCK_LOG_TRACE);
}

/* =========================================================================
 * Get/Set Callback 查询
 * ========================================================================= */

TEST_F(LogFileTest, GetSetCallback)
{
    /* 默认回调应为非 NULL（内置 stderr 回调或 NULL） */
    treelock_log_callback_t orig = treelock_log_get_callback();

    treelock_log_set_callback(test_callback, nullptr);
    EXPECT_EQ(treelock_log_get_callback(), test_callback);

    /* 恢复 */
    treelock_log_set_callback(orig, nullptr);
    EXPECT_EQ(treelock_log_get_callback(), orig);
}

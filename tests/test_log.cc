/*
 * TreeLocks — 日志模块测试 (GTest)
 *
 * 测试文件输出、日志等级过滤、文件切换、回调共存等。
 * 被测源文件: modules/treelock_log/src/log_core.c
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
 * 基本文件输出测试
 *
 * 验证日志模块能正确将消息写入文件，包含正确的等级标签和时间戳。
 *
 * 公共路径:
 *   TREELOCK_LOG_xxx(tag, fmt, ...)
 *     → 宏展开 → if (level <= TREELOCK_LOG_MAX_LEVEL)
 *       → treelock_log_write(level, tag, __FILE__, __LINE__, __func__, fmt, ...)
 *         → log_core.c: 检查运行期阈值 (g_log_ctx.level)
 *         → va_list 格式化消息
 *         → 调用回调 (默认: 写 stderr) + 写文件 (如果已设置)
 * ========================================================================= */

/**
 * 测试目标: 基本文件输出 + 内容验证
 *
 * 运行路径:
 *   treelock_log_set_file("test_log_output.tmp") → fopen + 写入路径
 *   TREELOCK_LOG_INFO("TEST", "hello...")
 *     → 宏检查 INFO <= MAX_LEVEL (Debug=TRACE → OK)
 *     → treelock_log_write() → 检查运行期阈值 → 格式化 → 写文件
 *   treelock_log_close_file() → fclose
 *   验证: 文件存在且包含 "[INFO ]", "[TEST    ]", 消息文本
 */
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

/**
 * 测试目标: 文件输出不含 ANSI 颜色码
 *
 * 运行路径:
 *   TREELOCK_LOG_ERROR/WARN/INFO 各写一条
 *     → treelock_log_write() → 写文件时跳过 ANSI 前缀
 *   验证: 文件中无 "\033", "[1;31", "[0;3" 等颜色序列
 */
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

/**
 * 测试目标: 全部 6 种日志等级正确输出
 *
 * 运行路径:
 *   treelock_log_set_level(TRACE) → 所有等级都通过
 *   TREELOCK_LOG_FATAL/ERROR/WARN/INFO/DEBUG/TRACE 各写一条
 *   验证: 文件包含 [FATAL]/[ERROR]/[WARN]/[INFO]/[DEBUG]/[TRACE] 标签
 */
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

/**
 * 测试目标: 运行期等级过滤正确生效
 *
 * 运行路径:
 *   treelock_log_set_level(WARN) → g_log_ctx.level = WARN
 *   TREELOCK_LOG_ERROR → level(2) >= WARN(3)? NO — 等等:
 *     FATAL=1, ERROR=2, WARN=3, INFO=4, DEBUG=5, TRACE=6
 *     所以 ERROR(2) < WARN(3) → 检查: level >= g_log_ctx.level?
 *     → treelock_log_write() 中检查 EC >= g_log_ctx.level? 2 >= 3 → FALSE → 不输出?!
 *
 *   不对，让我重新看逻辑。宏中检查: if (level <= TREELOCK_LOG_MAX_LEVEL)
 *   那实际上是编译期裁剪。运行期在 treelock_log_write() 中。
 *
 *   log_core.c 的 treelock_log_write() 会检查: level <= g_log_ctx.level
 *   FATAL(1) <= WARN(3) → YES → 输出
 *   ERROR(2) <= WARN(3) → YES → 输出
 *   INFO(4) <= WARN(3)  → NO  → 不输出
 *   DEBUG(5) <= WARN(3) → NO  → 不输出
 *
 *   验证: ERROR/WARN 出现在文件, INFO/DEBUG 不出现在文件
 */
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
 *
 * 验证 treelock_log_set_file() 切换文件时旧文件正确关闭，
 * 新文件接管后续输出；treelock_log_close_file() 彻底停止文件输出。
 * ========================================================================= */

/**
 * 测试目标: 切换文件后旧文件不再增长，新文件接收输出
 *
 * 运行路径:
 *   set_file(A) → 写 A → set_file(B) → g_log_ctx 关闭 A，打开 B
 *   验证: A 大小不变，B 包含新消息
 */
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

/**
 * 测试目标: close 后日志不再写入文件
 *
 * 运行路径:
 *   set_file → 写 "before close" → close_file → 写 "after close"
 *   close_file: g_log_ctx.file = NULL (fclose 后置空)
 *   treelock_log_write() 中 file!=NULL 检查 → 跳过文件写入
 *   验证: 文件大小不变
 */
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
 * 边界条件测试
 *
 * 验证日志模块在 NULL/空字符串/无效路径/双重关闭等异常场景不崩溃。
 * ========================================================================= */

/**
 * 测试目标: NULL/空字符串参数安全处理
 *
 * 运行路径:
 *   set_file(nullptr) → filename==NULL → 等同 close_file() → return TRUE
 *   set_file("")       → filename[0]=='\0' → 等同 close_file()
 *   close_file() × 2   → g_log_ctx.file == NULL → fclose(NULL) 被跳过
 */
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

/**
 * 测试目标: 非法路径返回 FALSE
 *
 * 运行路径:
 *   set_file("Z:\\nonexistent\\...") → fopen(.., "a") → NULL
 *     → return FALSE
 */
TEST_F(LogFileTest, InvalidPath)
{
#ifdef TREELOCK_PLATFORM_WINDOWS
    EXPECT_FALSE(treelock_log_set_file("Z:\\nonexistent\\path\\that\\should\\fail\\log.txt"));
#else
    EXPECT_FALSE(treelock_log_set_file("/nonexistent/path/that/should/fail/log.txt"));
#endif
}

/**
 * 测试目标: get_file 返回正确路径 + close 后返回 NULL
 *
 * 运行路径:
 *   get_file() 无文件 → NULL
 *   set_file("xxx") → g_log_ctx.file_path 存储路径 → get_file() → 路径指针
 *   close_file() → file_path 归零 → get_file() → NULL
 */
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

/**
 * 测试目标: 连续 50 条日志完整性（无截断/丢失）
 *
 * 运行路径:
 *   for 0..49: TREELOCK_LOG_INFO("CW", "message number %d", i)
 *     → 每次调用 treelock_log_write() → 格式化 → 写文件
 *   验证: 第一条和最后一条都在文件中
 */
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
 * 回调共存测试
 *
 * 验证自定义回调 (treelock_log_set_callback) 与文件输出可同时工作。
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

/**
 * 测试目标: 文件 + 自定义回调共存，两者都被触发
 *
 * 运行路径:
 *   set_file("xxx") → 开启文件输出
 *   set_callback(test_callback, nullptr) → g_log_ctx.cb = test_callback
 *   TREELOCK_LOG_INFO → treelock_log_write()
 *     → 调用 cb (自定义回调被调用 1 次)
 *     → 写文件 (文件中有消息)
 *   验证: cb 被调用 1 次，文件中有消息
 */
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
 * 时间戳格式验证
 *
 * 验证日志时间戳格式为 "YYYY-MM-DD HH:MM:SS.mmm"。
 * ========================================================================= */

/**
 * 测试目标: 时间戳包含 2 个 '-' (日期) + 2 个 ':' (时间) + 1 个 '.' (毫秒)
 *
 * 运行路径:
 *   TREELOCK_LOG_INFO → treelock_log_write()
 *     → treelock_platform_local_time(buf, 24) → 格式化为时间戳
 *     → 写文件: "[timestamp] [level] [tag] message"
 *   从文件提取第一个 [...] 块 → 验证格式结构
 */
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
 * 日志等级名称测试
 *
 * 验证 treelock_log_level_name() 返回正确的等级字符串。
 *
 * 运行路径:
 *   treelock_log_level_name(level)
 *     → log_core.c: 查 names[] 表返回字面量
 *     → 越界返回 "UNKNOWN"
 * ========================================================================= */

/**
 * 测试目标: 7 种已知等级 + 未知等级的名称
 *
 * 运行路径: 参见上方块注释
 * 覆盖: log_core.c treelock_log_level_name() 全部 case
 */
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
 * Get/Set Level 往返测试
 *
 * 验证 treelock_log_set_level() → treelock_log_get_level() 正确往返。
 *
 * 运行路径:
 *   set_level(level) → g_log_ctx.level = level
 *   get_level() → return g_log_ctx.level
 * ========================================================================= */

/**
 * 测试目标: set_level 后 get_level 返回相同值
 *
 * 运行路径: 参见上方块注释
 * 覆盖: log_core.c set_level + get_level
 */
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
 * Get/Set Callback 往返测试
 *
 * 验证 treelock_log_get_callback() / treelock_log_set_callback() 正确往返。
 * ========================================================================= */

/**
 * 测试目标: set_callback 后 get_callback 返回相同函数指针
 *
 * 运行路径:
 *   get_callback() → g_log_ctx.cb
 *   set_callback(fn, data) → g_log_ctx.cb = fn
 *   get_callback() → fn ✓
 *   恢复原始回调
 */
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

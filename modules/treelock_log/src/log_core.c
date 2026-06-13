/*
 * TreeLocks - 日志模块核心实现
 *
 * 版本: 0.1.0
 * 日期: 2026-06-13
 */

#include "log_internal.h"

#include <stdio.h>   /* vfprintf, stderr, FILE */
#include <string.h>  /* strlen, strncpy */
#include <time.h>    /* time, localtime */
#include <sys/time.h>/* gettimeofday (POSIX) */
#include <stdlib.h>  /* NULL */

/* =========================================================================
 * 全局日志上下文（单例）
 * ========================================================================= */

treelock_log_context_t g_log_ctx = {
    TREELOCK_LOG_TRACE,  /* runtime_level  — 默认输出所有等级 */
    NULL,                /* callback       — NULL 表示使用默认回调 */
    NULL,                /* callback_data  */
    PTHREAD_MUTEX_INITIALIZER,
    TRUE,                /* initialized    */
    FALSE                /* recursion_guard */
};

/* =========================================================================
 * 等级名称表
 * ========================================================================= */

static const CHAR *g_level_names[TREELOCK_LOG_LEVEL_COUNT] = {
    "OFF",
    "FATAL",
    "ERROR",
    "WARN",
    "INFO",
    "DEBUG",
    "TRACE"
};

/* ANSI 颜色码（仅用于默认终端输出） */
static const CHAR *g_level_colors[TREELOCK_LOG_LEVEL_COUNT] = {
    "",           /* OFF   */
    "\033[1;31m", /* FATAL — 粗体红 */
    "\033[0;31m", /* ERROR — 红色   */
    "\033[0;33m", /* WARN  — 黄色   */
    "\033[0;36m", /* INFO  — 青色   */
    "\033[0;37m", /* DEBUG — 灰色   */
    "\033[0;90m", /* TRACE — 暗灰色 */
};

static const CHAR *g_color_reset = "\033[0m";

/* =========================================================================
 * 内部函数实现
 * ========================================================================= */

/**
 * 函数名称：_log_short_file
 *
 * 功能描述：截取文件路径尾部
 *
 *          如果完整路径超过限制长度，截断头部保留尾部，
 *          方便日志阅读。例如 "/home/user/project/src/client.c" → "src/client.c"
 *
 * @param[IN] file - 完整文件路径
 *
 * @return 指向原始字符串中截取位置的指针
 */
CSTR_PTR _log_short_file(
    IN CSTR_PTR file)
{
    UINT_32 len;

    if (file == NULL) {
        return "???";
    }

    len = (UINT_32)strlen(file);
    if (len <= TREELOCK_LOG_FILE_TAIL) {
        return file;
    }

    /* 从尾部向前扫描，找到合适的截断点 */
    {
        UINT_32 skip = len - TREELOCK_LOG_FILE_TAIL;
        /* 跳过开头的目录部分，但保留最后的文件名 */
        if (skip < len) {
            return file + skip;
        }
    }

    return file;
}

/**
 * 函数名称：_log_format_timestamp
 *
 * 功能描述：格式化当前时间戳
 *
 *          格式: "2026-06-13 14:30:00.123"
 *          使用 gettimeofday 获取毫秒精度。
 *
 * @param[OUT] buf      - 输出缓冲区
 * @param[IN]  buf_size - 缓冲区大小（应 >= TREELOCK_LOG_TIME_BUF）
 */
VOID _log_format_timestamp(
    OUT CHAR   *buf,
    IN  UINT_32 buf_size)
{
    struct timeval  tv;
    struct tm       tm_info;
    INT_32          written;

    if (buf == NULL || buf_size == 0) {
        return;
    }

    gettimeofday(&tv, NULL);
#ifdef _WIN32
    /* Windows: localtime_s */
    localtime_s(&tm_info, &tv.tv_sec);
#else
    /* POSIX: localtime_r */
    localtime_r(&tv.tv_sec, &tm_info);
#endif

    written = (INT_32)strftime(buf, (size_t)buf_size, "%Y-%m-%d %H:%M:%S", &tm_info);
    if (written > 0 && (UINT_32)written + 5 < buf_size) {
        snprintf(buf + written, (size_t)(buf_size - (UINT_32)written),
                 ".%03ld", (long)(tv.tv_usec / 1000));
    }
}

/**
 * 函数名称：_log_should_output
 *
 * 功能描述：判断指定等级的日志是否应该输出
 *
 *          受编译期 TREELOCK_LOG_MAX_LEVEL 和运行期 runtime_level 双重约束。
 *
 * @param[IN] level - 日志等级
 *
 * @return TRUE 应输出，FALSE 应丢弃
 */
static BOOL _log_should_output(
    IN treelock_log_level_t level)
{
    if (level == TREELOCK_LOG_OFF) {
        return FALSE;
    }
    /* 运行期阈值：只输出 >= runtime_level 的日志 */
    if (level > g_log_ctx.runtime_level) {
        return FALSE;
    }
    return TRUE;
}

/**
 * 函数名称：_log_default_callback
 *
 * 功能描述：默认日志回调
 *
 *          格式: [时间戳] [等级] [标签] 文件:行 函数名() 消息
 *          输出到 stderr，ERROR/FATAL 附带 ANSI 颜色。
 *
 * @param[IN] level     - 日志等级
 * @param[IN] tag       - 模块标签
 * @param[IN] file      - 源文件名
 * @param[IN] line      - 行号
 * @param[IN] func_name - 函数名
 * @param[IN] message   - 格式化后的消息
 * @param[IN] user_data - 用户数据（未使用）
 */
VOID _log_default_callback(
    IN treelock_log_level_t  level,
    IN CSTR_PTR              tag,
    IN CSTR_PTR              file,
    IN INT_32                line,
    IN CSTR_PTR              func_name,
    IN CSTR_PTR              message,
    IN PTR_VOID              user_data)
{
    CHAR    time_buf[TREELOCK_LOG_TIME_BUF];
    CSTR_PTR short_file;
    CSTR_PTR color;
    CSTR_PTR reset;

    UNUSED_PARAM(user_data);

    _log_format_timestamp(time_buf, sizeof(time_buf));
    short_file = _log_short_file(file);

    color = g_level_colors[level];
    reset = g_color_reset;

    fprintf(stderr, "%s[%s] [%-5s] [%-8s] %s:%-4d %s()%s %s\n",
            color,
            time_buf,
            g_level_names[level],
            (tag != NULL) ? tag : "-",
            (short_file != NULL) ? short_file : "???",
            line,
            (func_name != NULL) ? func_name : "???",
            reset,
            (message != NULL) ? message : "");
}

/* =========================================================================
 * 公共 API 实现
 * ========================================================================= */

/**
 * 函数名称：treelock_log_set_level
 *
 * 功能描述：设置运行期日志等级阈值
 *
 *          可动态调整，例如启动时设 DEBUG，稳定后降为 ERROR。
 *
 * @param[IN] level - 最低输出等级，TREELOCK_LOG_OFF 关闭全部
 */
VOID treelock_log_set_level(
    IN treelock_log_level_t level)
{
    if (level > TREELOCK_LOG_TRACE) {
        level = TREELOCK_LOG_TRACE;
    }
    g_log_ctx.runtime_level = level;
}

/**
 * 函数名称：treelock_log_get_level
 *
 * 功能描述：获取当前运行期日志等级
 *
 * @return 当前等级
 */
treelock_log_level_t treelock_log_get_level(VOID)
{
    return g_log_ctx.runtime_level;
}

/**
 * 函数名称：treelock_log_set_callback
 *
 * 功能描述：注册自定义日志回调
 *
 *          注册后替代默认 stderr 输出，客户可将日志
 *          接入自有系统（syslog、文件、远程收集等）。
 *
 * @param[IN] cb        - 回调函数（NULL = 恢复默认）
 * @param[IN] user_data - 用户数据指针（回调时透传）
 */
VOID treelock_log_set_callback(
    IN treelock_log_callback_t  cb,
    IN PTR_VOID                 user_data)
{
    g_log_ctx.callback      = cb;
    g_log_ctx.callback_data = user_data;
}

/**
 * 函数名称：treelock_log_get_callback
 *
 * 功能描述：获取当前注册的日志回调
 *
 * @return 回调函数指针
 */
treelock_log_callback_t treelock_log_get_callback(VOID)
{
    return g_log_ctx.callback;
}

/**
 * 函数名称：treelock_log_level_name
 *
 * 功能描述：获取日志等级对应的字符串名称
 *
 * @param[IN] level - 日志等级
 *
 * @return 等级名称字符串
 */
CSTR_PTR treelock_log_level_name(
    IN treelock_log_level_t level)
{
    if (level >= TREELOCK_LOG_LEVEL_COUNT) {
        return "UNKNOWN";
    }
    return g_level_names[level];
}

/**
 * 函数名称：treelock_log_write
 *
 * 功能描述：格式化并输出一条日志
 *
 *          流程：
 *          1. 运行期等级检查
 *          2. 格式化消息到栈缓冲区（4096 字节）
 *          3. 加锁（递归保护）
 *          4. 调用回调（用户自定义 或 默认 stderr）
 *          5. 解锁
 *
 * @param[IN] level     - 日志等级
 * @param[IN] tag       - 模块标签
 * @param[IN] file      - 源文件名
 * @param[IN] line      - 行号
 * @param[IN] func_name - 函数名
 * @param[IN] fmt       - printf 风格格式串
 * @param[IN] ...       - 可变参数
 */
VOID treelock_log_write(
    IN treelock_log_level_t  level,
    IN CSTR_PTR              tag,
    IN CSTR_PTR              file,
    IN INT_32                line,
    IN CSTR_PTR              func_name,
    IN CSTR_PTR              fmt,
    ...)
{
    va_list args;
    va_start(args, fmt);
    treelock_log_write_va(level, tag, file, line, func_name, fmt, args);
    va_end(args);
}

/**
 * 函数名称：treelock_log_write_va
 *
 * 功能描述：日志输出的 va_list 版本
 *
 * @param[IN] level     - 日志等级
 * @param[IN] tag       - 模块标签
 * @param[IN] file      - 源文件名
 * @param[IN] line      - 行号
 * @param[IN] func_name - 函数名
 * @param[IN] fmt       - 格式串
 * @param[IN] args      - va_list
 */
VOID treelock_log_write_va(
    IN treelock_log_level_t  level,
    IN CSTR_PTR              tag,
    IN CSTR_PTR              file,
    IN INT_32                line,
    IN CSTR_PTR              func_name,
    IN CSTR_PTR              fmt,
    IN va_list               args)
{
    CHAR                  msg_buf[TREELOCK_LOG_MSG_MAX];
    treelock_log_callback_t cb;
    PTR_VOID              cb_data;

    /* ── 运行期等级过滤 ── */
    if (!_log_should_output(level)) {
        return;
    }

    /* ── 格式化消息 ── */
    vsnprintf(msg_buf, sizeof(msg_buf),
              (fmt != NULL) ? fmt : "", args);
    msg_buf[sizeof(msg_buf) - 1] = '\0';

    /* ── 递归保护（防止回调中再次写日志导致死锁） ── */
    if (g_log_ctx.recursion_guard) {
        /* 递归调用：直接写到 stderr 避免死锁 */
        fprintf(stderr, "[RECURSIVE LOG] [%s] %s\n",
                g_level_names[level], msg_buf);
        return;
    }

    /* ── 加锁 ── */
    pthread_mutex_lock(&g_log_ctx.mutex);
    g_log_ctx.recursion_guard = TRUE;

    /* ── 调用回调 ── */
    cb      = g_log_ctx.callback;
    cb_data = g_log_ctx.callback_data;

    if (cb != NULL) {
        cb(level, tag, file, line, func_name, msg_buf, cb_data);
    } else {
        _log_default_callback(level, tag, file, line, func_name,
                              msg_buf, NULL);
    }

    /* ── 解锁 ── */
    g_log_ctx.recursion_guard = FALSE;
    pthread_mutex_unlock(&g_log_ctx.mutex);
}

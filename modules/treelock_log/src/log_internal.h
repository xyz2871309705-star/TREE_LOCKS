/*
 * TreeLocks - 日志模块内部结构
 *
 * 此头文件不对外暴露，仅供日志模块内部使用。
 */

#ifndef TREELOCK_LOG_INTERNAL_H
#define TREELOCK_LOG_INTERNAL_H

#include "treelock_log.h"
#include <pthread.h>
#include <stdio.h>  /* FILE*, vfprintf (仅默认回调使用) */

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * 内部常量
 * ========================================================================= */

/** 单条日志消息最大长度 */
#define TREELOCK_LOG_MSG_MAX  (4096)

/** 日志 Tag 最大长度 */
#define TREELOCK_LOG_TAG_MAX  (32)

/** 默认时间戳缓冲区大小 */
#define TREELOCK_LOG_TIME_BUF (32)

/** 文件名截取长度（只保留最后 N 个字符） */
#define TREELOCK_LOG_FILE_TAIL (24)

/* =========================================================================
 * 日志上下文（全局单例）
 * ========================================================================= */

typedef struct {
    treelock_log_level_t     runtime_level;  /**< 运行期等级阈值           */
    treelock_log_callback_t  callback;       /**< 注册的回调函数           */
    PTR_VOID                 callback_data;  /**< 回调用户数据             */
    pthread_mutex_t          mutex;          /**< 线程安全锁               */
    INT_32                   initialized;    /**< 初始化标志               */
    INT_32                   recursion_guard;/**< 递归保护（防止回调中死锁） */
} treelock_log_context_t;

/* =========================================================================
 * 全局日志上下文（定义在 log_core.c）
 * ========================================================================= */

extern treelock_log_context_t g_log_ctx;

/* =========================================================================
 * 内部函数声明
 * ========================================================================= */

/**
 * 函数名称：_log_default_callback
 *
 * 功能描述：默认日志回调实现
 *
 *          将日志输出到 stderr，格式：
 *            [2026-06-13 14:30:00.123] [ERROR] [CORE] file.c:42 func() message
 *
 *          ERROR/FATAL 级别使用红色前缀标记（ANSI 终端颜色）。
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
    IN PTR_VOID              user_data
);

/**
 * 函数名称：_log_format_timestamp
 *
 * 功能描述：格式化当前时间戳到缓冲区
 *
 *          格式: "2026-06-13 14:30:00.123"
 *
 * @param[OUT] buf     - 输出缓冲区（至少 TREELOCK_LOG_TIME_BUF 字节）
 * @param[IN]  buf_size - 缓冲区大小
 */
VOID _log_format_timestamp(
    OUT CHAR   *buf,
    IN  UINT_32 buf_size
);

/**
 * 函数名称：_log_short_file
 *
 * 功能描述：截取文件路径的尾部（只保留最后 N 个字符）
 *
 * @param[IN] file - 完整文件路径
 *
 * @return 指向原始字符串中截取位置的指针
 */
CSTR_PTR _log_short_file(
    IN CSTR_PTR file
);

#ifdef __cplusplus
}
#endif

#endif /* TREELOCK_LOG_INTERNAL_H */

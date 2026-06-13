/*
 * TreeLocks - 日志模块
 *
 * 提供统一的日志记录能力，支持：
 *   - 6 级日志等级（FATAL/ERROR/WARN/INFO/DEBUG/TRACE）
 *   - 编译期 + 运行期双重输出控制
 *   - 外部回调注册（客户可接入自有日志系统）
 *   - 线程安全
 *   - 零外部依赖
 *
 * 用法：
 *   TREELOCK_LOG_INFO ("LOCK", "acquired lock on node %llu", node_id);
 *   TREELOCK_LOG_ERROR("NET", "connection failed: %s",    err_msg);
 *
 * 版本: 0.1.0
 * 日期: 2026-06-13
 */

#ifndef TREELOCK_LOG_H
#define TREELOCK_LOG_H

#include "treelock_types.h"
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * 日志等级
 * ========================================================================= */

typedef enum {
    TREELOCK_LOG_OFF   = 0,  /**< 关闭所有日志输出                     */
    TREELOCK_LOG_FATAL = 1,  /**< 致命错误（程序即将中止）              */
    TREELOCK_LOG_ERROR = 2,  /**< 错误（操作失败，需关注）              */
    TREELOCK_LOG_WARN  = 3,  /**< 警告（潜在问题，不影响运行）          */
    TREELOCK_LOG_INFO  = 4,  /**< 信息（关键流程节点）                  */
    TREELOCK_LOG_DEBUG = 5,  /**< 调试（开发诊断信息）                  */
    TREELOCK_LOG_TRACE = 6,  /**< 追踪（最细粒度，含函数进出等）        */

    TREELOCK_LOG_LEVEL_COUNT = 7  /**< 等级总数                        */
} treelock_log_level_t;

/* =========================================================================
 * 编译期日志等级开关
 *
 * 通过 CMake 定义 TREELOCK_LOG_MAX_LEVEL 可在编译期裁剪日志代码：
 *   - 低于此等级的日志语句不会生成任何机器码
 *   - Debug  构建默认 TREELOCK_LOG_TRACE (全部开启)
 *   - Release 构建默认 TREELOCK_LOG_INFO  (裁剪 DEBUG/TRACE)
 * ========================================================================= */

#ifndef TREELOCK_LOG_MAX_LEVEL
#  ifdef NDEBUG
#    define TREELOCK_LOG_MAX_LEVEL  TREELOCK_LOG_INFO
#  else
#    define TREELOCK_LOG_MAX_LEVEL  TREELOCK_LOG_TRACE
#  endif
#endif

/* =========================================================================
 * 日志回调
 * ========================================================================= */

/**
 * 日志回调函数类型
 *
 * 用户可注册自定义回调将日志输出到自有系统。
 * 所有 TREELOCK_LOG_* 宏最终都会调用已注册的回调。
 *
 * @param[IN] level     - 日志等级
 * @param[IN] tag       - 模块标签（如 "CORE"/"NET"/"LOCK"）
 * @param[IN] file      - 源文件名（__FILE__）
 * @param[IN] line      - 行号（__LINE__）
 * @param[IN] func_name - 函数名（__func__）
 * @param[IN] message   - 格式化后的日志消息
 * @param[IN] user_data - 注册时传入的用户数据指针
 */
typedef VOID (*treelock_log_callback_t)(
    IN treelock_log_level_t  level,
    IN CSTR_PTR              tag,
    IN CSTR_PTR              file,
    IN INT_32                line,
    IN CSTR_PTR              func_name,
    IN CSTR_PTR              message,
    IN PTR_VOID              user_data
);

/* =========================================================================
 * 公共 API
 * ========================================================================= */

/**
 * 函数名称：treelock_log_set_level
 *
 * 功能描述：设置运行期日志等级阈值
 *
 *          只有 >= 此等级的日志才会输出。
 *          运行期阈值受编译期 TREELOCK_LOG_MAX_LEVEL 约束：
 *          若编译期裁剪了 DEBUG，运行期设 DEBUG 也不会输出。
 *
 * @param[IN] level - 最低输出等级（TREELOCK_LOG_OFF = 全部关闭）
 */
VOID treelock_log_set_level(
    IN treelock_log_level_t level
);

/**
 * 函数名称：treelock_log_get_level
 *
 * 功能描述：获取当前运行期日志等级阈值
 *
 * @return 当前日志等级
 */
treelock_log_level_t treelock_log_get_level(VOID);

/**
 * 函数名称：treelock_log_set_callback
 *
 * 功能描述：注册自定义日志输出回调
 *
 *          默认回调将日志输出到 stderr（含时间戳和颜色标记）。
 *          注册新回调后替代默认行为，便于客户接入自有日志系统。
 *          传入 NULL 恢复默认回调。
 *
 * @param[IN] cb        - 日志回调函数指针（NULL = 恢复默认）
 * @param[IN] user_data - 用户数据（回调时透传）
 */
VOID treelock_log_set_callback(
    IN treelock_log_callback_t  cb,
    IN PTR_VOID                 user_data
);

/**
 * 函数名称：treelock_log_get_callback
 *
 * 功能描述：获取当前注册的日志回调函数
 *
 * @return 当前回调函数指针
 */
treelock_log_callback_t treelock_log_get_callback(VOID);

/**
 * 函数名称：treelock_log_level_name
 *
 * 功能描述：获取日志等级对应的字符串名称
 *
 * @param[IN] level - 日志等级
 *
 * @return 等级字符串（如 "ERROR"、"INFO" 等）
 */
CSTR_PTR treelock_log_level_name(
    IN treelock_log_level_t level
);

/* =========================================================================
 * 核心日志输出函数（供宏调用，一般不直接使用）
 * ========================================================================= */

/**
 * 函数名称：treelock_log_write
 *
 * 功能描述：执行实际的日志输出
 *
 *          根据当前运行期等级决定是否输出，然后调用注册的回调。
 *          此函数由 TREELOCK_LOG_* 宏自动调用，应用代码不应直接调用。
 *
 * @param[IN] level     - 日志等级
 * @param[IN] tag       - 模块标签
 * @param[IN] file      - 文件名
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
    ...
);

/**
 * 函数名称：treelock_log_write_va
 *
 * 功能描述：日志输出的 va_list 版本
 *
 * @param[IN] level     - 日志等级
 * @param[IN] tag       - 模块标签
 * @param[IN] file      - 文件名
 * @param[IN] line      - 行号
 * @param[IN] func_name - 函数名
 * @param[IN] fmt       - printf 风格格式串
 * @param[IN] args      - va_list 可变参数
 */
VOID treelock_log_write_va(
    IN treelock_log_level_t  level,
    IN CSTR_PTR              tag,
    IN CSTR_PTR              file,
    IN INT_32                line,
    IN CSTR_PTR              func_name,
    IN CSTR_PTR              fmt,
    IN va_list               args
);

/* =========================================================================
 * 文件输出 API
 * ========================================================================= */

/**
 * 函数名称：treelock_log_set_file
 *
 * 功能描述：设置日志输出文件
 *
 *          打开指定文件用于日志输出。日志会同时写入此文件和 stderr
 *          （或用户注册的回调）。文件中不含 ANSI 颜色码。
 *
 *          传入 NULL 或空字符串等效于 treelock_log_close_file()。
 *          若之前已打开文件，会先关闭旧文件再打开新文件。
 *
 * @param[IN] filename - 日志文件路径（NULL 或空串 = 关闭文件输出）
 *
 * @return TRUE 成功打开文件，FALSE 打开失败
 */
BOOL treelock_log_set_file(
    IN CSTR_PTR filename
);

/**
 * 函数名称：treelock_log_close_file
 *
 * 功能描述：关闭当前日志输出文件
 *
 *          关闭后日志不再写入文件，但仍会输出到 stderr（或用户回调）。
 *          若未开启文件输出，调用此函数无副作用。
 */
VOID treelock_log_close_file(VOID);

/**
 * 函数名称：treelock_log_get_file
 *
 * 功能描述：获取当前日志文件路径
 *
 * @return 当前日志文件路径字符串；未设置文件时返回 NULL
 */
CSTR_PTR treelock_log_get_file(VOID);

/* =========================================================================
 * 日志宏（应用代码统一使用这些宏）
 * ========================================================================= */

/**
 * TREELOCK_LOG_FATAL — 致命错误级别日志
 */
#define TREELOCK_LOG_FATAL(tag, fmt, ...) \
    do { \
        if (TREELOCK_LOG_FATAL <= TREELOCK_LOG_MAX_LEVEL) { \
            treelock_log_write(TREELOCK_LOG_FATAL, (tag), \
                               __FILE__, __LINE__, __func__, \
                               (fmt), ##__VA_ARGS__); \
        } \
    } while (0)

/**
 * TREELOCK_LOG_ERROR — 错误级别日志
 */
#define TREELOCK_LOG_ERROR(tag, fmt, ...) \
    do { \
        if (TREELOCK_LOG_ERROR <= TREELOCK_LOG_MAX_LEVEL) { \
            treelock_log_write(TREELOCK_LOG_ERROR, (tag), \
                               __FILE__, __LINE__, __func__, \
                               (fmt), ##__VA_ARGS__); \
        } \
    } while (0)

/**
 * TREELOCK_LOG_WARN — 警告级别日志
 */
#define TREELOCK_LOG_WARN(tag, fmt, ...) \
    do { \
        if (TREELOCK_LOG_WARN <= TREELOCK_LOG_MAX_LEVEL) { \
            treelock_log_write(TREELOCK_LOG_WARN, (tag), \
                               __FILE__, __LINE__, __func__, \
                               (fmt), ##__VA_ARGS__); \
        } \
    } while (0)

/**
 * TREELOCK_LOG_INFO — 信息级别日志
 */
#define TREELOCK_LOG_INFO(tag, fmt, ...) \
    do { \
        if (TREELOCK_LOG_INFO <= TREELOCK_LOG_MAX_LEVEL) { \
            treelock_log_write(TREELOCK_LOG_INFO, (tag), \
                               __FILE__, __LINE__, __func__, \
                               (fmt), ##__VA_ARGS__); \
        } \
    } while (0)

/**
 * TREELOCK_LOG_DEBUG — 调试级别日志
 */
#define TREELOCK_LOG_DEBUG(tag, fmt, ...) \
    do { \
        if (TREELOCK_LOG_DEBUG <= TREELOCK_LOG_MAX_LEVEL) { \
            treelock_log_write(TREELOCK_LOG_DEBUG, (tag), \
                               __FILE__, __LINE__, __func__, \
                               (fmt), ##__VA_ARGS__); \
        } \
    } while (0)

/**
 * TREELOCK_LOG_TRACE — 追踪级别日志（最细粒度）
 */
#define TREELOCK_LOG_TRACE(tag, fmt, ...) \
    do { \
        if (TREELOCK_LOG_TRACE <= TREELOCK_LOG_MAX_LEVEL) { \
            treelock_log_write(TREELOCK_LOG_TRACE, (tag), \
                               __FILE__, __LINE__, __func__, \
                               (fmt), ##__VA_ARGS__); \
        } \
    } while (0)

/* =========================================================================
 * 便捷宏：函数进入/退出追踪
 * ========================================================================= */

/** 函数进入追踪 */
#define TREELOCK_LOG_FUNC_ENTER(tag) \
    TREELOCK_LOG_TRACE((tag), "--> %s() enter", __func__)

/** 函数退出追踪 */
#define TREELOCK_LOG_FUNC_EXIT(tag) \
    TREELOCK_LOG_TRACE((tag), "<-- %s() exit",  __func__)

/** 函数退出并携带返回值 */
#define TREELOCK_LOG_FUNC_EXIT_RC(tag, rc) \
    TREELOCK_LOG_TRACE((tag), "<-- %s() exit (rc=%d)", __func__, (rc))

#ifdef __cplusplus
}
#endif

#endif /* TREELOCK_LOG_H */

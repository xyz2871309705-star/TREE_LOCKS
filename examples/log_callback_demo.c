/*
 * TreeLocks - 日志回调注册示例
 *
 * 演示外部调用者如何注册自定义日志回调，将日志接入自有系统。
 *
 * 版本: 0.1.0
 * 日期: 2026-06-13
 */

#include "treelock_log.h"
#include "treelock.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* =========================================================================
 * 自定义日志上下文
 * ========================================================================= */

/** 用户自定义日志上下文 */
typedef struct {
    FILE   *file_handle;     /**< 日志文件句柄（示例：写文件而非 stderr） */
    INT_32  message_count;  /**< 已记录的消息数                          */
    BOOL    use_json;       /**< 是否以 JSON 格式输出                    */
} my_log_context_t;

/* =========================================================================
 * 示例 1：自定义日志回调 — 写入文件
 * ========================================================================= */

/**
 * 函数名称：_my_file_log_callback
 *
 * 功能描述：自定义日志回调 — 将日志写入指定文件
 *
 *          演示如何替代默认的 stderr 输出，
 *          使用用户自己的文件句柄。
 *
 * @param[IN] level     - 日志等级
 * @param[IN] tag       - 模块标签
 * @param[IN] file      - 源文件名
 * @param[IN] line      - 行号
 * @param[IN] func_name - 函数名
 * @param[IN] message   - 日志消息
 * @param[IN] user_data - my_log_context_t 指针
 */
static VOID _my_file_log_callback(
    IN treelock_log_level_t  level,
    IN CSTR_PTR              tag,
    IN CSTR_PTR              file,
    IN INT_32                line,
    IN CSTR_PTR              func_name,
    IN CSTR_PTR              message,
    IN PTR_VOID              user_data)
{
    my_log_context_t *ctx = (my_log_context_t *)user_data;
    CHAR              time_buf[32];
    time_t            now;

    UNUSED_PARAM(func_name);

    if (ctx == NULL || ctx->file_handle == NULL) {
        return;
    }

    now = time(NULL);
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S",
             localtime(&now));

    fprintf(ctx->file_handle,
            "[%s] [%-5s] [%-8s] (%s:%d) %s\n",
            time_buf,
            treelock_log_level_name(level),
            (tag != NULL) ? tag : "-",
            (file != NULL) ? file : "???",
            line,
            (message != NULL) ? message : "");

    fflush(ctx->file_handle);
    ctx->message_count++;
}

/* =========================================================================
 * 示例 2：自定义日志回调 — JSON 格式
 * ========================================================================= */

/**
 * 函数名称：_my_json_log_callback
 *
 * 功能描述：自定义日志回调 — JSON 格式输出到 stdout
 *
 *          演示结构化日志输出，便于接入日志收集系统（ELK/Loki 等）。
 *
 * @param[IN] level     - 日志等级
 * @param[IN] tag       - 模块标签
 * @param[IN] file      - 源文件名
 * @param[IN] line      - 行号
 * @param[IN] func_name - 函数名
 * @param[IN] message   - 日志消息
 * @param[IN] user_data - my_log_context_t 指针
 */
static VOID _my_json_log_callback(
    IN treelock_log_level_t  level,
    IN CSTR_PTR              tag,
    IN CSTR_PTR              file,
    IN INT_32                line,
    IN CSTR_PTR              func_name,
    IN CSTR_PTR              message,
    IN PTR_VOID              user_data)
{
    my_log_context_t *ctx = (my_log_context_t *)user_data;

    if (ctx == NULL) {
        return;
    }

    /*
     * 输出 JSON 行（便于日志管道消费）：
     * {"ts":1234567890,"level":"INFO","tag":"CORE","file":"client.c","line":372,"func":"treelock_lock","msg":"..."}
     */
    printf("{\"ts\":%lld,\"level\":\"%s\",\"tag\":\"%s\",\"file\":\"%s\","
           "\"line\":%d,\"func\":\"%s\",\"msg\":\"%s\"}\n",
           (long long)time(NULL),
           treelock_log_level_name(level),
           (tag != NULL) ? tag : "-",
           (file != NULL) ? file : "???",
           line,
           (func_name != NULL) ? func_name : "???",
           (message != NULL) ? message : "");

    ctx->message_count++;
}

/* =========================================================================
 * 示例 3：日志等级过滤 + 恢复默认回调
 * ========================================================================= */

/**
 * 函数名称：_example_level_filter_and_restore
 *
 * 功能描述：演示运行期动态调整日志等级与恢复默认回调
 */
static VOID _example_level_filter_and_restore(VOID)
{
    printf("\n--- 示例 3：等级过滤 + 恢复默认 ---\n");

    /* 记录当前回调 */
    treelock_log_callback_t saved_cb = treelock_log_get_callback();

    /* 仅输出 ERROR 及以上 */
    treelock_log_set_level(TREELOCK_LOG_ERROR);

    TREELOCK_LOG_INFO ("DEMO", "这条 INFO 日志不会输出");
    TREELOCK_LOG_ERROR("DEMO", "这条 ERROR 日志会输出");

    /* 恢复全量输出 */
    treelock_log_set_level(TREELOCK_LOG_TRACE);
    TREELOCK_LOG_INFO ("DEMO", "恢复后这条 INFO 日志又可以输出了");

    /* 恢复之前的回调（如果有） */
    treelock_log_set_callback(saved_cb, NULL);
}

/* =========================================================================
 * main
 * ========================================================================= */

/**
 * 函数名称：main
 *
 * 功能描述：依次演示三种日志回调使用方式
 *
 * @return EXIT_SUCCESS
 */
INT_32 main(VOID)
{
    my_log_context_t file_ctx;
    my_log_context_t json_ctx;

    printf("TreeLocks 日志回调注册示例\n");
    printf("==========================\n");

    /* ── 示例 1：文件日志回调 ── */
    printf("\n--- 示例 1：自定义文件日志回调 ---\n");

    {
        treelock_t *tl;
        treelock_config_t cfg;

        memset(&file_ctx, 0, sizeof(file_ctx));
        file_ctx.file_handle = fopen("_treelock_demo.log", "w");
        if (file_ctx.file_handle == NULL) {
            fprintf(stderr, "无法创建日志文件\n");
            return EXIT_FAILURE;
        }

        /* 注册自定义回调 */
        treelock_log_set_callback(_my_file_log_callback, &file_ctx);

        TREELOCK_LOG_INFO ("DEMO", "日志已切换到文件输出");

        /* 使用 treelock 会产生日志（通过回调写入文件） */
        memset(&cfg, 0, sizeof(cfg));
        cfg.client_id  = "demo_file";
        cfg.timeout_ms = 5000;

        tl = treelock_create(&cfg);
        if (tl != NULL) {
            treelock_lock(tl, 100, TREELOCK_X);
            treelock_unlock(tl, 100);
            treelock_destroy(tl);
        }

        fclose(file_ctx.file_handle);
        printf("  → 文件日志已写入 _treelock_demo.log (%d 条消息)\n",
               file_ctx.message_count);

        /* 恢复默认回调 */
        treelock_log_set_callback(NULL, NULL);
    }

    /* ── 示例 2：JSON 格式日志回调 ── */
    printf("\n--- 示例 2：JSON 格式日志回调 ---\n");

    {
        treelock_t *tl2;
        treelock_config_t cfg2;

        memset(&json_ctx, 0, sizeof(json_ctx));
        json_ctx.use_json = TRUE;

        treelock_log_set_callback(_my_json_log_callback, &json_ctx);

        memset(&cfg2, 0, sizeof(cfg2));
        cfg2.client_id  = "demo_json";
        cfg2.timeout_ms = 5000;

        tl2 = treelock_create(&cfg2);
        if (tl2 != NULL) {
            treelock_lock(tl2, 200, TREELOCK_S);
            treelock_unlock(tl2, 200);
            treelock_destroy(tl2);
        }

        printf("  → JSON 日志已输出 %d 条\n", json_ctx.message_count);

        /* 恢复默认回调 */
        treelock_log_set_callback(NULL, NULL);
    }

    /* ── 示例 3：等级过滤 ── */
    _example_level_filter_and_restore();

    /* ── 清理 ── */
    remove("_treelock_demo.log");

    printf("\n所有示例完成。\n");
    return EXIT_SUCCESS;
}

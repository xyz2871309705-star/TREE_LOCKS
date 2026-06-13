/*
 * TreeLocks - 并发压力测试
 *
 * 测试多线程环境下的锁操作正确性，验证互斥和死锁防护。
 *
 * 版本: 0.1.0
 * 日期: 2026-06-13
 */

#include "treelock.h"
#include "treelock_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

/* =========================================================================
 * 测试常量
 * ========================================================================= */

#define NUM_THREADS       (8)
#define NUM_OPS           (1000)
#define NUM_NODES         (10)
#define ESCALATE_THREADS  (4)
#define ESCALATE_OPS      (100)
#define ESCALATE_NODES    (5)
#define CLIENT_ID_MAX     (32)

/* =========================================================================
 * 测试框架
 * ========================================================================= */

static INT_32 g_tests_run    = 0;
static INT_32 g_tests_passed = 0;
static INT_32 g_tests_failed = 0;

/**
 * 函数名称：test_begin
 *
 * 功能描述：标记测试开始
 *
 * @param[IN] name - 测试名称
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
 * 功能描述：标记测试通过并输出额外信息
 *
 * @param[IN] extra - 额外输出信息（可为 NULL）
 */
static VOID test_pass(
    IN CSTR_PTR extra)
{
    if (extra != NULL) {
        printf("PASSED (%s)\n", extra);
    } else {
        printf("PASSED\n");
    }
    g_tests_passed++;
}

/**
 * 函数名称：test_fail
 *
 * 功能描述：标记测试失败
 *
 * @param[IN] msg - 失败原因
 */
static VOID test_fail(
    IN CSTR_PTR msg)
{
    printf("FAILED: %s\n", msg);
    g_tests_failed++;
}

/* =========================================================================
 * 测试用例数据类型
 * ========================================================================= */

/** 基础并发测试线程参数：每个线程独立 client_id */
typedef struct {
    CHAR         client_id[CLIENT_ID_MAX]; /**< 线程专属客户端标识 */
    INT_32       thread_id;                /**< 线程编号           */
    INT_32       nodes;                    /**< 节点数             */
    INT_32       ops;                      /**< 操作次数           */
    INT_32       ops_done;                 /**< 成功操作数         */
    INT_32       timeouts;                 /**< 超时次数           */
    INT_32       errors;                   /**< 错误计数           */
} basic_thread_t;

/* =========================================================================
 * 测试用例 1：多客户端并发 lock/unlock
 * ========================================================================= */

/**
 * 函数名称：_basic_worker
 *
 * 功能描述：基础并发测试的线程工作函数
 *
 *          每个线程拥有独立的 treelock 实例和 client_id，
 *          保证锁冲突检测正确工作。
 *
 * @param[IN] arg - basic_thread_t 指针
 *
 * @return NULL
 */
static VOID *_basic_worker(
    IN PTR_VOID arg)
{
    basic_thread_t     *data = (basic_thread_t *)arg;
    treelock_config_t   cfg;
    treelock_t         *tl;
    INT_32              i;

    memset(&cfg, 0, sizeof(cfg));
    cfg.timeout_ms = 100;
    cfg.client_id  = data->client_id;

    tl = treelock_create(&cfg);
    if (tl == NULL) {
        data->errors = -999;
        return NULL;
    }

    for (i = 0; i < data->ops; i++) {
        treelock_node_id_t node_id;
        treelock_mode_t    mode;

        node_id = (treelock_node_id_t)((i + data->thread_id) % data->nodes + 1);

        /* 仅使用 IS / IX / X，避免模式升级冲突 */
        switch ((i * 7 + data->thread_id * 13) % 3) {
            case 0: mode = TREELOCK_IS; break;
            case 1: mode = TREELOCK_IX; break;
            default:mode = TREELOCK_X;  break;
        }

        RET_CODE rc = treelock_try_lock(tl, node_id, mode, 100);
        if (rc == TREELOCK_OK) {
            data->ops_done++;
            treelock_unlock(tl, node_id);
        } else if (rc == TREELOCK_ERR_TIMEOUT) {
            data->timeouts++; /* 锁冲突 → 正常 */
        } else {
            data->errors++;
        }
    }

    treelock_destroy(tl);
    return NULL;
}

/**
 * 函数名称：test_concurrent_basic
 *
 * 功能描述：多客户端并发 lock/unlock，验证无崩溃无异常错误
 */
static VOID test_concurrent_basic(VOID)
{
    pthread_t      threads[NUM_THREADS];
    basic_thread_t thread_data[NUM_THREADS];
    INT_32         t;
    INT_32         total_ops    = 0;
    INT_32         total_errors = 0;

    test_begin("concurrent multi-client lock/unlock");

    for (t = 0; t < NUM_THREADS; t++) {
        memset(&thread_data[t], 0, sizeof(thread_data[t]));
        snprintf(thread_data[t].client_id, CLIENT_ID_MAX,
                 "basic_t%d", t);
        thread_data[t].thread_id = t;
        thread_data[t].nodes     = NUM_NODES;
        thread_data[t].ops       = NUM_OPS;
        pthread_create(&threads[t], NULL, _basic_worker, &thread_data[t]);
    }

    for (t = 0; t < NUM_THREADS; t++) {
        pthread_join(threads[t], NULL);
        total_ops    += thread_data[t].ops_done;
        total_errors += thread_data[t].errors;
    }

    if (total_errors > 0) {
        CHAR buf[128];
        snprintf(buf, sizeof(buf),
                 "%d errors in %d ops", total_errors, total_ops);
        test_fail(buf);
    } else {
        CHAR buf[64];
        snprintf(buf, sizeof(buf),
                 "%d ops across %d threads", total_ops, NUM_THREADS);
        test_pass(buf);
    }
}

/* =========================================================================
 * 测试用例 2：锁表一致性
 *
 * 多线程共享同一 treelock 实例，验证锁表内部状态无竞态损坏。
 *
 * 注意：阶段一为单机单客户端锁表，跨客户端互斥需阶段三实现。
 *       此测试验证同一客户端下的并发安全性。
 * ========================================================================= */

/** 一致性测试共享状态 */
static volatile INT_32 g_lock_count = 0;  /**< 成功加锁次数   */
static volatile INT_32 g_unlock_error = 0;/**< 解锁失败次数   */

/**
 * 函数名称：_consistency_worker
 *
 * 功能描述：一致性测试线程
 *
 *          每个线程操作专属的节点组（与其他线程无冲突），
 *          验证 lock/unlock 配对不出错。
 *
 * @param[IN] arg - thread index 的指针 (INT_32*)
 *
 * @return NULL（错误通过 g_unlock_error 上报）
 */
typedef struct {
    treelock_t *tl;       /**< 共享的锁句柄     */
    INT_32      thread_idx; /**< 线程编号        */
} consistency_arg_t;

static VOID *_consistency_worker(
    IN PTR_VOID arg)
{
    consistency_arg_t *ca  = (consistency_arg_t *)arg;
    treelock_t        *tl  = ca->tl;
    INT_32             tid = ca->thread_idx;
    INT_32             i;

    for (i = 0; i < NUM_OPS; i++) {
        /* 每个线程使用专属节点范围：tid*1000 .. tid*1000+999 */
        treelock_node_id_t node_id =
            (treelock_node_id_t)(tid * 1000 + (i % 1000) + 1);

        RET_CODE rc = treelock_try_lock(tl, node_id, TREELOCK_IX, 200);
        if (rc == TREELOCK_OK) {
            g_lock_count++;
            rc = treelock_unlock(tl, node_id);
            if (rc != TREELOCK_OK) {
                g_unlock_error++;
            }
        }
    }
    return NULL;
}

/**
 * 函数名称：test_concurrent_consistency
 *
 * 功能描述：共享实例并发一致性测试
 */
static VOID test_concurrent_consistency(VOID)
{
    treelock_config_t cfg;
    treelock_t       *tl;
    pthread_t         threads[NUM_THREADS];
    INT_32            t;

    test_begin("concurrent consistency (shared instance)");

    memset(&cfg, 0, sizeof(cfg));
    cfg.timeout_ms = 200;
    cfg.client_id  = "consistency_test";

    tl = treelock_create(&cfg);
    if (tl == NULL) {
        test_fail("create failed");
        return;
    }

    g_lock_count   = 0;
    g_unlock_error = 0;

    consistency_arg_t args[NUM_THREADS];
    for (t = 0; t < NUM_THREADS; t++) {
        args[t].tl         = tl;
        args[t].thread_idx = t;
        pthread_create(&threads[t], NULL, _consistency_worker, &args[t]);
    }

    for (t = 0; t < NUM_THREADS; t++) {
        pthread_join(threads[t], NULL);
    }

    treelock_destroy(tl);

    if (g_unlock_error > 0) {
        CHAR buf[96];
        snprintf(buf, sizeof(buf),
                 "%d unlock errors in %d locks",
                 g_unlock_error, g_lock_count);
        test_fail(buf);
    } else {
        CHAR buf[64];
        snprintf(buf, sizeof(buf),
                 "%d lock/unlock pairs, 0 errors", g_lock_count);
        test_pass(buf);
    }
}

/* =========================================================================
 * 测试用例 3：并发锁升级
 * ========================================================================= */

/**
 * 函数名称：_escalate_worker
 *
 * 功能描述：锁升级测试线程 — IS → S 升级
 *
 * @param[IN] arg - client_id 字符串指针
 *
 * @return 成功返回 0，失败返回 -1
 */
static VOID *_escalate_worker(
    IN PTR_VOID arg)
{
    CSTR_PTR            cid = (CSTR_PTR)arg;
    treelock_config_t   cfg;
    treelock_t         *tl;
    INT_32              i;

    memset(&cfg, 0, sizeof(cfg));
    cfg.timeout_ms = 5000;
    cfg.client_id  = cid;

    tl = treelock_create(&cfg);
    if (tl == NULL) {
        return (VOID *)(intptr_t)(-1);
    }

    for (i = 0; i < ESCALATE_OPS; i++) {
        treelock_node_id_t node_id;
        RET_CODE           rc;

        node_id = (treelock_node_id_t)((i % ESCALATE_NODES) + 1);

        rc = treelock_lock(tl, node_id, TREELOCK_IS);
        if (rc != TREELOCK_OK) {
            treelock_destroy(tl);
            return (VOID *)(intptr_t)(-1);
        }

        rc = treelock_escalate(tl, node_id, TREELOCK_S);
        if (rc != TREELOCK_OK) {
            treelock_unlock(tl, node_id);
            treelock_destroy(tl);
            return (VOID *)(intptr_t)(-1);
        }

        treelock_unlock(tl, node_id);
    }

    treelock_destroy(tl);
    return (VOID *)0;
}

/**
 * 函数名称：test_concurrent_escalate
 *
 * 功能描述：多客户端并发锁升级测试
 */
static VOID test_concurrent_escalate(VOID)
{
    pthread_t     threads[ESCALATE_THREADS];
    CHAR          client_ids[ESCALATE_THREADS][CLIENT_ID_MAX];
    INT_32        t;
    INT_32        errors = 0;

    test_begin("concurrent lock escalate");

    for (t = 0; t < ESCALATE_THREADS; t++) {
        snprintf(client_ids[t], CLIENT_ID_MAX, "esc_t%d", t);
        pthread_create(&threads[t], NULL, _escalate_worker, client_ids[t]);
    }

    for (t = 0; t < ESCALATE_THREADS; t++) {
        VOID *ret;
        pthread_join(threads[t], &ret);
        if (ret != (VOID *)0) errors++;
    }

    if (errors > 0) {
        CHAR buf[64];
        snprintf(buf, sizeof(buf), "%d threads had errors", errors);
        test_fail(buf);
    } else {
        test_pass(NULL);
    }
}

/* =========================================================================
 * 入口
 * ========================================================================= */

/**
 * 函数名称：main
 *
 * 功能描述：并发测试入口
 *
 * @return EXIT_SUCCESS 或 EXIT_FAILURE
 */
INT_32 main(VOID)
{
    /* 测试期间抑制库 DEBUG/TRACE 日志噪音 */
    treelock_log_set_level(TREELOCK_LOG_WARN);

    printf("TreeLocks - 并发压力测试\n");
    printf("========================\n\n");

    test_concurrent_basic();
    test_concurrent_consistency();
    test_concurrent_escalate();

    printf("\n========================\n");
    printf("结果: %d/%d 通过, %d 失败\n",
           g_tests_passed, g_tests_run, g_tests_failed);

    return (g_tests_failed > 0) ? EXIT_FAILURE : EXIT_SUCCESS;
}

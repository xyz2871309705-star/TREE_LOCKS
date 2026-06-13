/*
 * TreeLocks - 并发压力测试
 *
 * 测试多线程环境下的锁操作正确性，验证互斥和死锁防护。
 *
 * 版本: 0.1.0
 * 日期: 2026-06-13
 */

#include "treelock.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

/* =========================================================================
 * 测试常量
 * ========================================================================= */

#define NUM_THREADS    (8)
#define NUM_OPS        (1000)
#define NUM_NODES      (10)
#define ESCALATE_THREADS (4)
#define ESCALATE_OPS     (100)
#define ESCALATE_NODES   (5)

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

/** 基础并发测试线程参数 */
typedef struct {
    treelock_t  *tl;        /**< 锁句柄     */
    INT_32       thread_id; /**< 线程编号   */
    INT_32       ops_done;  /**< 成功操作数 */
    INT_32       errors;    /**< 错误计数   */
} thread_data_t;

/** 互斥测试线程参数 */
typedef struct {
    treelock_t  *tl;        /**< 锁句柄     */
    INT_32       thread_id; /**< 线程编号   */
} counter_data_t;

/* 互斥测试共享变量 */
static volatile INT_32 g_shared_counter = 0;
static volatile INT_32 g_max_observed   = 0;

/* =========================================================================
 * 测试用例 1：基础并发 lock/unlock
 * ========================================================================= */

/**
 * 函数名称：_basic_worker
 *
 * 功能描述：基础并发测试的线程工作函数
 *
 *          每个线程执行 NUM_OPS 次操作：
 *          - 随机选择锁模式和节点
 *          - 尝试获取锁（100ms 超时）
 *          - 成功后短暂持有再释放
 *
 * @param[IN] arg - thread_data_t 指针
 *
 * @return NULL
 */
static VOID *_basic_worker(
    IN PTR_VOID arg)
{
    thread_data_t *data = (thread_data_t *)arg;
    INT_32 i;

    for (i = 0; i < NUM_OPS; i++) {
        treelock_node_id_t node_id;
        treelock_mode_t    mode;
        INT_32             r;
        RET_CODE           rc;

        node_id = (treelock_node_id_t)((i + data->thread_id) % NUM_NODES + 1);

        r = (i * 7 + data->thread_id * 13) % 6;
        switch (r) {
            case 0: mode = TREELOCK_IS;  break;
            case 1: mode = TREELOCK_IX;  break;
            case 2: mode = TREELOCK_S;   break;
            case 3: mode = TREELOCK_SIX; break;
            case 4: mode = TREELOCK_X;   break;
            default:mode = TREELOCK_IS;  break;
        }

        rc = treelock_try_lock(data->tl, node_id, mode, 100);
        if (rc == TREELOCK_OK) {
            data->ops_done++;
            treelock_unlock(data->tl, node_id);
        } else if (rc == TREELOCK_ERR_TIMEOUT) {
            /* 超时是正常的（锁冲突） */
        } else {
            data->errors++;
        }
    }
    return NULL;
}

/**
 * 函数名称：test_concurrent_basic
 *
 * 功能描述：验证多线程并发 lock/unlock 不会导致崩溃和错误
 */
static VOID test_concurrent_basic(VOID)
{
    treelock_config_t config;
    treelock_t       *tl;
    pthread_t         threads[NUM_THREADS];
    thread_data_t     thread_data[NUM_THREADS];
    INT_32            t;
    INT_32            total_ops    = 0;
    INT_32            total_errors = 0;

    TEST("concurrent basic lock/unlock");

    memset(&config, 0, sizeof(config));
    config.timeout_ms = 100;
    config.client_id  = "thread_test";

    tl = treelock_create(&config);
    if (tl == NULL) {
        test_fail("create failed");
        return;
    }

    for (t = 0; t < NUM_THREADS; t++) {
        thread_data[t].tl        = tl;
        thread_data[t].thread_id = t;
        thread_data[t].ops_done  = 0;
        thread_data[t].errors    = 0;
        pthread_create(&threads[t], NULL, _basic_worker, &thread_data[t]);
    }

    for (t = 0; t < NUM_THREADS; t++) {
        pthread_join(threads[t], NULL);
        total_ops    += thread_data[t].ops_done;
        total_errors += thread_data[t].errors;
    }

    treelock_destroy(tl);

    if (total_errors > 0) {
        CHAR buf[128];
        snprintf(buf, sizeof(buf),
                 "%d errors in %d operations", total_errors, total_ops);
        test_fail(buf);
    } else {
        CHAR buf[64];
        snprintf(buf, sizeof(buf),
                 "%d ops across %d threads", total_ops, NUM_THREADS);
        test_pass(buf);
    }
}

/* =========================================================================
 * 测试用例 2：互斥正确性
 * ========================================================================= */

/**
 * 函数名称：_counter_worker
 *
 * 功能描述：互斥测试的线程工作函数
 *
 *          每个线程在 X 锁保护下递增 shared_counter，
 *          验证临界区内的计数器最大值为 1（真正互斥）。
 *
 * @param[IN] arg - counter_data_t 指针
 *
 * @return 成功返回 0，失败返回 -1
 */
static VOID *_counter_worker(
    IN PTR_VOID arg)
{
    counter_data_t *data = (counter_data_t *)arg;
    INT_32 i;

    for (i = 0; i < NUM_OPS; i++) {
        RET_CODE rc = treelock_lock(data->tl, 1, TREELOCK_X);
        if (rc != TREELOCK_OK) {
            return (VOID *)(intptr_t)(-1);
        }

        /* ── 临界区开始 ── */
        g_shared_counter++;
        if (g_shared_counter > g_max_observed) {
            g_max_observed = g_shared_counter;
        }
        /* 模拟工作负载（短暂忙等待） */
        {
            volatile INT_32 j;
            for (j = 0; j < 100; j++) { /* empty */ }
        }
        g_shared_counter--;
        /* ── 临界区结束 ── */

        treelock_unlock(data->tl, 1);
    }
    return (VOID *)0;
}

/**
 * 函数名称：test_concurrent_mutual_exclusion
 *
 * 功能描述：验证 X 锁能够实现真正的互斥（临界区同时最多 1 个线程）
 */
static VOID test_concurrent_mutual_exclusion(VOID)
{
    treelock_config_t config;
    treelock_t       *tl;
    pthread_t         threads[NUM_THREADS];
    counter_data_t    thread_data[NUM_THREADS];
    INT_32            t;

    TEST("concurrent mutual exclusion (X lock)");

    memset(&config, 0, sizeof(config));
    config.timeout_ms = 0;
    config.client_id  = "mutex_test";

    tl = treelock_create(&config);
    if (tl == NULL) {
        test_fail("create failed");
        return;
    }

    g_shared_counter = 0;
    g_max_observed   = 0;

    for (t = 0; t < NUM_THREADS; t++) {
        thread_data[t].tl        = tl;
        thread_data[t].thread_id = t;
        pthread_create(&threads[t], NULL, _counter_worker, &thread_data[t]);
    }

    for (t = 0; t < NUM_THREADS; t++) {
        VOID *ret;
        pthread_join(threads[t], &ret);
        if (ret != (VOID *)0) {
            test_fail("thread returned error");
            treelock_destroy(tl);
            return;
        }
    }

    treelock_destroy(tl);

    /* X 锁保证互斥：临界区最多 1 个线程 */
    if (g_max_observed <= 1) {
        test_pass(NULL);
    } else {
        CHAR buf[64];
        snprintf(buf, sizeof(buf),
                 "max observed=%d (expected 1)", g_max_observed);
        test_fail(buf);
    }
}

/* =========================================================================
 * 测试用例 3：并发锁升级
 * ========================================================================= */

/**
 * 函数名称：_escalate_worker
 *
 * 功能描述：锁升级测试的线程工作函数
 *
 *          循环执行 IS → S 升级操作，验证升级路径无竞态。
 *
 * @param[IN] arg - treelock_t 指针
 *
 * @return 成功返回 0，失败返回 -1
 */
static VOID *_escalate_worker(
    IN PTR_VOID arg)
{
    treelock_t *tl = (treelock_t *)arg;
    INT_32 i;

    for (i = 0; i < ESCALATE_OPS; i++) {
        treelock_node_id_t node_id;
        RET_CODE           rc;

        node_id = (treelock_node_id_t)((i % ESCALATE_NODES) + 1);

        /* IS → S 升级 */
        rc = treelock_lock(tl, node_id, TREELOCK_IS);
        if (rc != TREELOCK_OK) return (VOID *)(intptr_t)(-1);

        rc = treelock_escalate(tl, node_id, TREELOCK_S);
        if (rc != TREELOCK_OK) {
            treelock_unlock(tl, node_id);
            return (VOID *)(intptr_t)(-1);
        }

        treelock_unlock(tl, node_id);
    }
    return (VOID *)0;
}

/**
 * 函数名称：test_concurrent_escalate
 *
 * 功能描述：验证多线程并发锁升级不会导致协议错误
 */
static VOID test_concurrent_escalate(VOID)
{
    treelock_config_t config;
    treelock_t       *tl;
    pthread_t         threads[ESCALATE_THREADS];
    INT_32            t;
    INT_32            errors = 0;

    TEST("concurrent lock escalate");

    memset(&config, 0, sizeof(config));
    config.timeout_ms = 5000;
    config.client_id  = "escalate_test";

    tl = treelock_create(&config);
    if (tl == NULL) {
        test_fail("create failed");
        return;
    }

    for (t = 0; t < ESCALATE_THREADS; t++) {
        pthread_create(&threads[t], NULL, _escalate_worker, tl);
    }

    for (t = 0; t < ESCALATE_THREADS; t++) {
        VOID *ret;
        pthread_join(threads[t], &ret);
        if (ret != (VOID *)0) errors++;
    }

    treelock_destroy(tl);

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
 * 功能描述：并发测试入口，运行全部测试用例并汇总结果
 *
 * @return EXIT_SUCCESS 或 EXIT_FAILURE
 */
INT_32 main(VOID)
{
    printf("TreeLocks - 并发压力测试\n");
    printf("========================\n\n");

    test_concurrent_basic();
    test_concurrent_mutual_exclusion();
    test_concurrent_escalate();

    printf("\n========================\n");
    printf("结果: %d/%d 通过, %d 失败\n",
           g_tests_passed, g_tests_run, g_tests_failed);

    return (g_tests_failed > 0) ? EXIT_FAILURE : EXIT_SUCCESS;
}

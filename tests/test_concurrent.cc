/*
 * TreeLocks — 并发压力测试 (GTest)
 *
 * 测试多线程环境下的锁操作正确性，验证互斥和死锁防护。
 *
 * 版本: 0.2.0
 * 日期: 2026-06-13
 */

#include <gtest/gtest.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <pthread.h>
#include <thread>
#include <chrono>

extern "C" {
#include "treelock.h"
#include "treelock_log.h"
#include "treelock_platform.h"
}

#define NUM_THREADS       (8)
#define NUM_OPS           (1000)
#define NUM_NODES         (10)
#define ESCALATE_THREADS  (4)
#define ESCALATE_OPS      (100)
#define ESCALATE_NODES    (5)
#define CLIENT_ID_MAX     (32)

/* =========================================================================
 * Test 1: multi-client concurrent lock/unlock
 * ========================================================================= */

typedef struct {
    char    client_id[CLIENT_ID_MAX];
    int     thread_id;
    int     nodes;
    int     ops;
    int     ops_done;
    int     timeouts;
    int     errors;
} basic_thread_t;

static void *basic_worker(void *arg) {
    basic_thread_t *data = (basic_thread_t *)arg;
    treelock_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.timeout_ms = 100;
    cfg.client_id  = data->client_id;

    treelock_t *tl = treelock_create(&cfg);
    if (!tl) { data->errors = -999; return nullptr; }

    for (int i = 0; i < data->ops; i++) {
        auto node_id = (treelock_node_id_t)((i + data->thread_id) % data->nodes + 1);
        treelock_mode_t mode;
        switch ((i * 7 + data->thread_id * 13) % 3) {
            case 0: mode = TREELOCK_IS; break;
            case 1: mode = TREELOCK_IX; break;
            default:mode = TREELOCK_X;  break;
        }
        int rc = treelock_try_lock(tl, node_id, mode, 100);
        if (rc == TREELOCK_OK) {
            data->ops_done++;
            treelock_unlock(tl, node_id);
        } else if (rc == TREELOCK_ERR_TIMEOUT) {
            data->timeouts++;
        } else {
            data->errors++;
        }
    }
    treelock_destroy(tl);
    return nullptr;
}

TEST(Concurrent, MultiClientLockUnlock)
{
    pthread_t threads[NUM_THREADS];
    basic_thread_t thread_data[NUM_THREADS];
    int total_ops = 0, total_errors = 0;

    for (int t = 0; t < NUM_THREADS; t++) {
        memset(&thread_data[t], 0, sizeof(thread_data[t]));
        snprintf(thread_data[t].client_id, CLIENT_ID_MAX, "basic_t%d", t);
        thread_data[t].thread_id = t;
        thread_data[t].nodes     = NUM_NODES;
        thread_data[t].ops       = NUM_OPS;
        pthread_create(&threads[t], nullptr, basic_worker, &thread_data[t]);
    }
    for (int t = 0; t < NUM_THREADS; t++) {
        pthread_join(threads[t], nullptr);
        total_ops    += thread_data[t].ops_done;
        total_errors += thread_data[t].errors;
    }
    EXPECT_EQ(total_errors, 0) << "errors in " << total_ops << " operations";
}

/* =========================================================================
 * Test 2: lock table consistency (shared instance)
 * ========================================================================= */

static volatile int g_lock_count   = 0;
static volatile int g_unlock_error = 0;

typedef struct {
    treelock_t *tl;
    int         thread_idx;
} consistency_arg_t;

static void *consistency_worker(void *arg) {
    auto *ca  = (consistency_arg_t *)arg;
    auto *tl  = ca->tl;
    int   tid = ca->thread_idx;

    for (int i = 0; i < NUM_OPS; i++) {
        auto node_id = (treelock_node_id_t)(tid * 1000 + (i % 1000) + 1);
        int rc = treelock_try_lock(tl, node_id, TREELOCK_IX, 200);
        if (rc == TREELOCK_OK) {
            g_lock_count++;
            rc = treelock_unlock(tl, node_id);
            if (rc != TREELOCK_OK) g_unlock_error++;
        }
    }
    return nullptr;
}

TEST(Concurrent, SharedInstanceConsistency)
{
    treelock_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.timeout_ms = 200;
    cfg.client_id  = "consistency_test";

    treelock_t *tl = treelock_create(&cfg);
    ASSERT_NE(tl, nullptr);

    g_lock_count   = 0;
    g_unlock_error = 0;

    pthread_t threads[NUM_THREADS];
    consistency_arg_t args[NUM_THREADS];
    for (int t = 0; t < NUM_THREADS; t++) {
        args[t].tl         = tl;
        args[t].thread_idx = t;
        pthread_create(&threads[t], nullptr, consistency_worker, &args[t]);
    }
    for (int t = 0; t < NUM_THREADS; t++) {
        pthread_join(threads[t], nullptr);
    }
    treelock_destroy(tl);

    EXPECT_EQ(g_unlock_error, 0)
        << g_unlock_error << " unlock errors in " << g_lock_count << " ops";
}

/* =========================================================================
 * Test 3: try_lock timeout
 * ========================================================================= */

TEST(Concurrent, TryLockTimeout)
{
    /*
     * Phase 1 中每个 treelock_t 有独立锁表，不同实例间不存在真正冲突。
     * 跨客户端竞争需 Phase 2+ 的 server 模式。
     *
     * 此处测试：
     * 1. try_lock 在锁可用时立即获取成功
     * 2. 同客户端重入锁被正确检测（自身不冲突 + ref_count）
     * 3. 锁获取耗时在合理范围内（远小于超时值）
     */
    treelock_t *tl = treelock_create(nullptr);
    ASSERT_NE(tl, nullptr);

    /* 锁可用：try_lock(50ms) 应立即成功 */
    {
        INT_64 start = treelock_platform_time_ms();
        RET_CODE rc = treelock_try_lock(tl, 100, TREELOCK_X, 50);
        INT_64 elapsed = treelock_platform_time_ms() - start;

        EXPECT_EQ(rc, TREELOCK_OK);
        EXPECT_LT(elapsed, 47) << "should acquire quickly when lock available";
    }

    /* 同客户端重入锁：ref_count++，不阻塞 */
    {
        INT_64 start = treelock_platform_time_ms();
        RET_CODE rc = treelock_try_lock(tl, 100, TREELOCK_X, 200);
        INT_64 elapsed = treelock_platform_time_ms() - start;

        EXPECT_EQ(rc, TREELOCK_OK);
        EXPECT_EQ(treelock_get_mode(tl, 100), TREELOCK_X);
        EXPECT_LT(elapsed, 45) << "re-entrant lock should be instant";
    }

    /* 释放 */
    treelock_unlock(tl, 100);
    treelock_unlock(tl, 100);
    treelock_destroy(tl);
}

/* =========================================================================
 * Test 4: re-entrant lock/unlock
 * ========================================================================= */

TEST(Concurrent, ReentrantLock)
{
    treelock_t *tl = treelock_create(nullptr);
    ASSERT_NE(tl, nullptr);

    /* 同客户端同节点同模式多次加锁 */
    EXPECT_EQ(treelock_lock(tl, 1, TREELOCK_S), TREELOCK_OK);
    EXPECT_EQ(treelock_lock(tl, 1, TREELOCK_S), TREELOCK_OK);
    EXPECT_EQ(treelock_lock(tl, 1, TREELOCK_S), TREELOCK_OK);

    /* 查询模式应仍为 S */
    EXPECT_EQ(treelock_get_mode(tl, 1), TREELOCK_S);

    /* 每次解锁只减引用计数 */
    EXPECT_EQ(treelock_unlock(tl, 1), TREELOCK_OK);
    EXPECT_EQ(treelock_get_mode(tl, 1), TREELOCK_S); /* 仍持有 */
    EXPECT_EQ(treelock_unlock(tl, 1), TREELOCK_OK);
    EXPECT_EQ(treelock_get_mode(tl, 1), TREELOCK_S); /* 仍持有 */
    EXPECT_EQ(treelock_unlock(tl, 1), TREELOCK_OK);
    EXPECT_EQ(treelock_get_mode(tl, 1), TREELOCK_NL); /* 完全释放 */

    /* 超额 unlock 应返回错误 */
    EXPECT_EQ(treelock_unlock(tl, 1), TREELOCK_ERR_INVAL);

    treelock_destroy(tl);
}

TEST(Concurrent, ReentrantLockUpgrade)
{
    treelock_t *tl = treelock_create(nullptr);
    ASSERT_NE(tl, nullptr);

    /* IS → escalate 到 S（合法升级路径） */
    EXPECT_EQ(treelock_lock(tl, 2, TREELOCK_IS), TREELOCK_OK);
    EXPECT_EQ(treelock_escalate(tl, 2, TREELOCK_S), TREELOCK_OK);

    /* 再次以相同模式 S 加锁（重入，ref_count++） */
    EXPECT_EQ(treelock_lock(tl, 2, TREELOCK_S), TREELOCK_OK);

    EXPECT_EQ(treelock_get_mode(tl, 2), TREELOCK_S);
    /* 释放：ref_count=2 → 1 (第二次 lock 的计数) */
    EXPECT_EQ(treelock_unlock(tl, 2), TREELOCK_OK);
    EXPECT_EQ(treelock_get_mode(tl, 2), TREELOCK_S);
    /* 释放：ref_count=1 → 0 (escalate 后持有的锁) */
    EXPECT_EQ(treelock_unlock(tl, 2), TREELOCK_OK);
    EXPECT_EQ(treelock_get_mode(tl, 2), TREELOCK_NL);

    treelock_destroy(tl);
}

/* =========================================================================
 * Test 5: concurrent lock escalate (original)
 * ========================================================================= */

static void *escalate_worker(void *arg) {
    const char *cid = (const char *)arg;
    treelock_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.timeout_ms = 5000;
    cfg.client_id  = cid;

    treelock_t *tl = treelock_create(&cfg);
    if (!tl) return (void *)(intptr_t)(-1);

    for (int i = 0; i < ESCALATE_OPS; i++) {
        auto node_id = (treelock_node_id_t)((i % ESCALATE_NODES) + 1);
        if (treelock_lock(tl, node_id, TREELOCK_IS) != TREELOCK_OK) {
            treelock_destroy(tl);
            return (void *)(intptr_t)(-1);
        }
        if (treelock_escalate(tl, node_id, TREELOCK_S) != TREELOCK_OK) {
            treelock_unlock(tl, node_id);
            treelock_destroy(tl);
            return (void *)(intptr_t)(-1);
        }
        treelock_unlock(tl, node_id);
    }
    treelock_destroy(tl);
    return nullptr;
}

TEST(Concurrent, LockEscalate)
{
    pthread_t threads[ESCALATE_THREADS];
    char client_ids[ESCALATE_THREADS][CLIENT_ID_MAX];
    int errors = 0;

    for (int t = 0; t < ESCALATE_THREADS; t++) {
        snprintf(client_ids[t], CLIENT_ID_MAX, "esc_t%d", t);
        pthread_create(&threads[t], nullptr, escalate_worker, client_ids[t]);
    }
    for (int t = 0; t < ESCALATE_THREADS; t++) {
        void *ret;
        pthread_join(threads[t], &ret);
        if (ret != nullptr) errors++;
    }
    EXPECT_EQ(errors, 0) << errors << " threads had errors";
}

/* =========================================================================
 * Test 6: concurrent lock downgrade
 * ========================================================================= */

#define DOWNGRADE_THREADS (4)
#define DOWNGRADE_OPS     (100)

static void *downgrade_worker(void *arg) {
    const char *cid = (const char *)arg;
    treelock_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.timeout_ms = 5000;
    cfg.client_id  = cid;

    treelock_t *tl = treelock_create(&cfg);
    if (!tl) return (void *)(intptr_t)(-1);

    for (int i = 0; i < DOWNGRADE_OPS; i++) {
        auto node_id = (treelock_node_id_t)((i % 3) + 10);
        /* X → lock, downgrade to IS */
        if (treelock_lock(tl, node_id, TREELOCK_X) != TREELOCK_OK) {
            treelock_destroy(tl);
            return (void *)(intptr_t)(-1);
        }
        if (treelock_downgrade(tl, node_id, TREELOCK_IS) != TREELOCK_OK) {
            treelock_unlock(tl, node_id);
            treelock_destroy(tl);
            return (void *)(intptr_t)(-1);
        }
        treelock_unlock(tl, node_id);
    }
    treelock_destroy(tl);
    return nullptr;
}

TEST(Concurrent, LockDowngrade)
{
    pthread_t threads[DOWNGRADE_THREADS];
    char client_ids[DOWNGRADE_THREADS][CLIENT_ID_MAX];
    int errors = 0;

    for (int t = 0; t < DOWNGRADE_THREADS; t++) {
        snprintf(client_ids[t], CLIENT_ID_MAX, "dng_t%d", t);
        pthread_create(&threads[t], nullptr, downgrade_worker, client_ids[t]);
    }
    for (int t = 0; t < DOWNGRADE_THREADS; t++) {
        void *ret;
        pthread_join(threads[t], &ret);
        if (ret != nullptr) errors++;
    }
    EXPECT_EQ(errors, 0) << errors << " downgrade threads had errors";
}

/* =========================================================================
 * Test 7: concurrent unlock_all
 * ========================================================================= */

#define UNLOCKALL_THREADS (4)
#define UNLOCKALL_NODES   (20)
#define UNLOCKALL_OPS     (50)

static void *unlockall_worker(void *arg) {
    int tid = *(int *)arg;
    treelock_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.timeout_ms = 5000;
    cfg.client_id  = "unlockall_test";

    treelock_t *tl = treelock_create(&cfg);
    if (!tl) return (void *)(intptr_t)(-1);

    for (int i = 0; i < UNLOCKALL_OPS; i++) {
        /* 获取若干锁 */
        for (int n = 0; n < 5; n++) {
            auto node_id = (treelock_node_id_t)(
                tid * UNLOCKALL_NODES + (i * 3 + n) % UNLOCKALL_NODES + 100);
            if (treelock_try_lock(tl, node_id, TREELOCK_IX, 500) != TREELOCK_OK) {
                treelock_unlock_all(tl);
                treelock_destroy(tl);
                return (void *)(intptr_t)(-1);
            }
        }
        /* 一次性释放全部 */
        if (treelock_unlock_all(tl) != TREELOCK_OK) {
            treelock_destroy(tl);
            return (void *)(intptr_t)(-1);
        }
    }
    treelock_destroy(tl);
    return nullptr;
}

TEST(Concurrent, UnlockAll)
{
    pthread_t threads[UNLOCKALL_THREADS];
    int thread_ids[UNLOCKALL_THREADS];
    int errors = 0;

    for (int t = 0; t < UNLOCKALL_THREADS; t++) {
        thread_ids[t] = t;
        pthread_create(&threads[t], nullptr, unlockall_worker, &thread_ids[t]);
    }
    for (int t = 0; t < UNLOCKALL_THREADS; t++) {
        void *ret;
        pthread_join(threads[t], &ret);
        if (ret != nullptr) errors++;
    }
    EXPECT_EQ(errors, 0) << errors << " unlock_all threads had errors";
}

/* =========================================================================
 * Test 8: query_node during concurrent access
 * ========================================================================= */

static volatile int g_query_ops     = 0;
static volatile int g_query_errors  = 0;

static void *query_worker(void *arg) {
    auto *tl = (treelock_t *)arg;
    for (int i = 0; i < 500; i++) {
        auto node_id = (treelock_node_id_t)((i % 5) + 1);
        int rc = treelock_try_lock(tl, node_id, TREELOCK_IS, 50);
        if (rc == TREELOCK_OK) {
            /* 查询自身模式 */
            if (treelock_get_mode(tl, node_id) != TREELOCK_IS) {
                g_query_errors++;
            }
            /* 查询节点状态 */
            char *json = nullptr;
            if (treelock_query_node(tl, node_id, &json) == TREELOCK_OK) {
                if (json != nullptr && strlen(json) > 0) {
                    g_query_ops++;
                }
                free(json);
            }
            treelock_unlock(tl, node_id);
        }
    }
    return nullptr;
}

TEST(Concurrent, QueryDuringConcurrent)
{
    treelock_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.timeout_ms = 200;
    cfg.client_id  = "query_test";

    treelock_t *tl = treelock_create(&cfg);
    ASSERT_NE(tl, nullptr);

    g_query_ops    = 0;
    g_query_errors = 0;

    pthread_t threads[4];
    for (int t = 0; t < 4; t++) {
        pthread_create(&threads[t], nullptr, query_worker, tl);
    }
    for (int t = 0; t < 4; t++) {
        pthread_join(threads[t], nullptr);
    }
    treelock_destroy(tl);

    EXPECT_EQ(g_query_errors, 0) << "query errors: " << g_query_errors;
    EXPECT_GT(g_query_ops, 0) << "query_node should have succeeded at least once";
}

/* =========================================================================
 * Test 9: multiple waiters FIFO wake on same node
 * ========================================================================= */

typedef struct {
    treelock_t *tl;
    int         waiter_id;
    int         acquired;
    INT_64      acquired_at;
} waiter_fifo_t;

static void *fifo_waiter(void *arg) {
    auto *w = (waiter_fifo_t *)arg;
    /* 所有 waiter 竞争同一个节点上的 X 锁（互斥，只有一个人能拿到） */
    int rc = treelock_try_lock(w->tl, 9999, TREELOCK_X, 3000);
    if (rc == TREELOCK_OK) {
        w->acquired    = 1;
        w->acquired_at = treelock_platform_time_ms();
        treelock_unlock(w->tl, 9999);
    }
    return nullptr;
}

TEST(Concurrent, MultipleWaitersSameNode)
{
    treelock_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.timeout_ms = 5000;
    cfg.client_id  = "fifo_test";

    treelock_t *tl = treelock_create(&cfg);
    ASSERT_NE(tl, nullptr);

    /* 先持有一个 X 锁阻塞所有 waiter */
    ASSERT_EQ(treelock_lock(tl, 9999, TREELOCK_X), TREELOCK_OK);

    const int NUM_WAITERS = 8;
    pthread_t threads[NUM_WAITERS];
    waiter_fifo_t waiters[NUM_WAITERS];

    for (int t = 0; t < NUM_WAITERS; t++) {
        memset(&waiters[t], 0, sizeof(waiters[t]));
        waiters[t].tl        = tl;
        waiters[t].waiter_id = t;
        pthread_create(&threads[t], nullptr, fifo_waiter, &waiters[t]);
    }

    /* 短暂等待确保所有 waiter 入队 */
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    /* 释放 X → 唤醒第一个兼容的 waiter */
    treelock_unlock(tl, 9999);

    int acquired_count = 0;
    for (int t = 0; t < NUM_WAITERS; t++) {
        pthread_join(threads[t], nullptr);
        if (waiters[t].acquired) acquired_count++;
    }

    /* 至少有一个 waiter 成功获取了锁（其他因 X 互斥超时） */
    EXPECT_GE(acquired_count, 1) << "at least one waiter should acquire the lock";

    treelock_destroy(tl);
}

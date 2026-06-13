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

extern "C" {
#include "treelock.h"
#include "treelock_log.h"
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
 * Test 3: concurrent lock escalate
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

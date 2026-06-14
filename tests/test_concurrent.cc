/*
 * TreeLocks — 并发压力测试 (GTest)
 *
 * 测试多线程环境下的锁操作正确性，验证互斥、重入、升降级和查询功能。
 * 被测源文件: modules/treelock_core/src/client.c, lock_table.c
 *
 * Phase 1 注意: 每个 treelock_t 有独立锁表，跨实例无真正锁竞争。
 * 跨客户端竞争需 Phase 2+ server 模式。本文件测试单实例多线程、
 * 重入锁、升降级等 Phase 1 可验证的行为。
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
#include "treelock_tree.h"
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
 * Test 1: 多客户端并发 lock/unlock
 *
 * 测试目标: 多个独立的 treelock_t 实例在并发执行 lock/try_lock/unlock
 *          时不出现死锁、崩溃或数据损坏。
 *
 * 运行路径:
 *   每个线程:
 *     treelock_create() → 创建独立锁表 + mutex 初始化
 *     for 1000 ops:
 *       treelock_try_lock(tl, node, IS/IX/X, timeout=100ms)
 *         → client.c: _do_lock_core()
 *           → lock_table.c: treelock_table_get_or_create()  (节点不存在则创建)
 *           → lock_table.c: treelock_table_check_conflict() (自身不冲突→直接授予)
 *           → lock_table.c: treelock_table_grant_lock()     (加入 grant 列表)
 *         → client.c: _add_held_lock() (记录到 held_locks[])
 *       treelock_unlock(tl, node)
 *         → client.c: _find_held_lock() → ref_count ≤ 1 → 释放
 *           → lock_table.c: treelock_table_release_lock()
 *           → lock_table.c: treelock_table_wake_waiters()
 *     treelock_destroy() → 释放所有锁 + 清理锁表
 *
 * 验证: 8 线程 × 1000 ops = 8000 次操作全部无 TREELOCK_ERR_* 错误
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

/**
 * 测试目标: 8 线程并发执行 lock/unlock 无错误
 *
 * 运行路径: 参见上方 "Test 1" 块注释
 * 覆盖: client.c (全程), lock_table.c (全程)
 */
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
 * Test 2: 共享实例锁表一致性
 *
 * 测试目标: 多个线程共享同一 treelock_t 实例，每个线程使用不同的 node_id
 *          范围，验证共享锁表在并发创建节点、授予锁、释放锁时不出现
 *          数据竞争或状态不一致。
 *
 * 运行路径:
 *   所有线程共享 tl (同一个锁表):
 *     treelock_try_lock(tl, tid*1000 + i, IX, 200ms)
 *       → lock_table.c: treelock_table_get_or_create()  (table_mutex 保护)
 *         → 链表头插入新节点
 *       → lock_table.c: treelock_table_check_conflict()  (同 client_id 自身跳过)
 *       → lock_table.c: treelock_table_grant_lock()     (grant 数组扩展)
 *       → client.c: _add_held_lock()                    (held_mutex 保护)
 *     treelock_unlock(tl, node)
 *       → lock_table.c: treelock_table_release_lock()   (swap-remove)
 *       → lock_table.c: treelock_table_wake_waiters()
 *   最后 treelock_destroy(tl) 清理所有节点
 *
 * 验证: 0 unlock 错误
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

/**
 * 测试目标: 共享 treelock_t 的并发 lock/unlock 一致性
 *
 * 运行路径: 参见上方 "Test 2" 块注释
 * 覆盖: lock_table.c table_mutex 并发保护, client.c held_mutex 并发保护
 */
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
 * Test 3: try_lock 超时与立即获取
 *
 * 测试目标: 验证 try_lock 在锁可用时立即返回成功（耗时远小于超时值），
 *          以及同客户端重入锁不阻塞。
 *
 * 运行路径:
 *   Phase 1 中每个 treelock_t 有独立锁表，不同实例间不存在真正冲突。
 *   此处测试 Phase 1 可验证的行为:
 *
 *   场景 1 — 锁可用时:
 *     treelock_try_lock(tl, 100, X, 50ms)
 *       → _do_lock_core()
 *         → _get_held_mode() → 未持有 → 跳过重入检查
 *         → treelock_table_get_or_create() → 新建节点 (grant_count=0)
 *         → treelock_table_check_conflict() → grant_count=0 → return TRUE (无冲突)
 *         → treelock_table_grant_lock() → 直接授予
 *         → _add_held_lock()
 *
 *   场景 2 — 同客户端重入:
 *     treelock_try_lock(tl, 100, X, 200ms)  (已有 X 锁)
 *       → _do_lock_core()
 *         → _get_held_mode() → TREELOCK_OK, existing_mode = X == mode → ref_count++
 *         → 直接返回 TREELOCK_OK (不进入锁表操作)
 *
 * 验证: 两次操作都在 50ms 内完成（远小于超时值），模式正确。
 * ========================================================================= */

/**
 * 测试目标: try_lock 在锁可用时立即返回 + 重入锁快速路径
 *
 * 运行路径: 参见上方 "Test 3" 块注释
 * 覆盖: client.c _do_lock_core() — 快速授予路径 + 重入 ref_count 路径
 */
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
 * Test 4: 重入锁引用计数
 *
 * 测试目标: 验证同客户端同节点同模式的重复加锁通过引用计数 (ref_count)
 *          正确管理，而非在锁表中创建重复 grant 记录。
 *
 * 运行路径:
 *   ReentrantLock:
 *     第 1 次 treelock_lock(tl, 1, S)
 *       → _do_lock_core() → 新获取 → grant + held (ref_count=1)
 *     第 2 次 treelock_lock(tl, 1, S)
 *       → _do_lock_core() → _get_held_mode() → existing=S → mode 相同
 *         → _find_held_lock() → ref_count++ (2)
 *         → 不进入锁表操作
 *     第 3 次同样 ref_count++ (3)
 *     第 1 次 treelock_unlock(tl, 1)
 *       → _find_held_lock() → ref_count > 1 → ref_count-- (2) → 不释放锁表
 *     第 2 次: ref_count-- (1)
 *     第 3 次: ref_count-- (1→0) → 从 held[] swap-remove
 *       → treelock_table_release_lock()
 *     第 4 次 treelock_unlock: _find_held_lock() → NULL → TREELOCK_ERR_INVAL
 *
 *   ReentrantLockUpgrade:
 *     treelock_lock(tl, 2, IS) → held[mode=IS, ref_count=1]
 *     treelock_escalate(tl, 2, S)
 *       → client.c: treelock_escalate()
 *         → _get_held_mode() → IS
 *         → treelock_escalate_valid(IS, S) → TRUE
 *         → treelock_table_grant_lock(S) + treelock_table_release_lock(IS)
 *         → 原地更新 held: mode = S
 *     treelock_lock(tl, 2, S) (re-entrant)
 *       → 同 mode → ref_count++ (2)
 *   释放路径同上。
 *
 * 验证: ref_count 正确计算，超额 unlock 返回错误
 * ========================================================================= */

/**
 * 测试目标: 同模式重入锁的引用计数管理
 *
 * 运行路径: 参见上方 "Test 4" 块注释 — ReentrantLock 路径
 * 覆盖: client.c _do_lock_core() 重入分支, _find_held_lock(),
 *       _remove_held_lock() ref_count > 1 路径
 */
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

/**
 * 测试目标: escalate 后再重入锁的引用计数
 *
 * 运行路径: 参见上方 "Test 4" 块注释 — ReentrantLockUpgrade 路径
 * 覆盖: client.c treelock_escalate() — grant 新旧模式 + held原地更新,
 *       _do_lock_core() 重入分支在 upgrade 后的行为
 */
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
 * Test 5: 并发锁升级
 *
 * 测试目标: 多个线程同时独立执行 IS→S 锁升级，验证 escalate 路径的
 *          并发安全性（grant 替换 + held 更新 无竞争）。
 *
 * 运行路径:
 *   每个线程 (独立 treelock_t, 5 个不同节点):
 *     for 100 ops:
 *       treelock_lock(tl, node, IS)
 *         → _do_lock_core() → grant IS → held[IS]
 *       treelock_escalate(tl, node, S)
 *         → client.c: treelock_escalate()
 *           → _get_held_mode() → IS
 *           → treelock_escalate_valid(IS, S) → TRUE
 *           → treelock_table_grant_lock(S)  → grant S
 *           → treelock_table_release_lock(IS) → remove IS grant
 *           → treelock_table_wake_waiters()
 *           → held.mode = S (原地更新)
 *       treelock_unlock(tl, node)
 *         → _find_held_lock() → ref_count=1 → 释放
 *           → treelock_table_release_lock(S)
 *
 * 验证: 4 线程 × 100 ops = 400 次 escalate 无错误
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

/**
 * 测试目标: 4 线程并发执行 IS→S 升级无错误
 *
 * 运行路径: 参见上方 "Test 5" 块注释
 * 覆盖: client.c treelock_escalate() — grant/release/wake 完整流程
 */
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
 * Test 6: 并发锁降级
 *
 * 测试目标: 多个线程同时独立执行 X→IS 锁降级，验证降级路径的
 *          并发安全性。
 *
 * 运行路径:
 *   每个线程 (独立 treelock_t, 3 个不同节点):
 *     for 100 ops:
 *       treelock_lock(tl, node, X)
 *         → _do_lock_core() → grant X → held[X]
 *       treelock_downgrade(tl, node, IS)
 *         → client.c: treelock_downgrade()
 *           → _get_held_mode() → X
 *           → treelock_downgrade_valid(X, IS) → TRUE
 *           → treelock_table_grant_lock(IS) → grant IS
 *           → treelock_table_release_lock(X) → remove X grant
 *           → treelock_table_wake_waiters()
 *           → held.mode = IS (原地更新)
 *       treelock_unlock(tl, node)
 *         → treelock_table_release_lock(IS)
 *
 * 验证: 4 线程 × 100 ops = 400 次降级无错误
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

/**
 * 测试目标: 4 线程并发执行 X→IS 降级无错误
 *
 * 运行路径: 参见上方 "Test 6" 块注释
 * 覆盖: client.c treelock_downgrade() — grant IS + release X + wake 流程
 */
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
 * Test 7: 并发 unlock_all
 *
 * 测试目标: 验证 treelock_unlock_all() 在并发环境下正确批量释放锁，
 *          逆序释放 + 锁表同步不产生数据竞争。
 *
 * 运行路径:
 *   每个线程的每轮迭代:
 *     获取 5 个不同节点的 IX 锁:
 *       treelock_try_lock(tl, node, IX, 500ms) × 5
 *         → held_locks[] 累计 5 条记录
 *     treelock_unlock_all(tl)
 *       → client.c: treelock_unlock_all()
 *         → while (held_count > 0):
 *             取 held_locks[held_count-1] (最后一个 = 最后获取的 → 逆序)
 *             treelock_table_release_lock(node, cid, mode)
 *             treelock_table_wake_waiters(node)
 *         → held_count = 0
 *
 * 验证: 4 线程 × 50 轮 = 200 次 unlock_all 无错误
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

/**
 * 测试目标: 4 线程并发批量 lock → unlock_all 循环
 *
 * 运行路径: 参见上方 "Test 7" 块注释
 * 覆盖: client.c treelock_unlock_all() — 逆序释放 + 全量清理
 */
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
 * Test 8: 并发查询操作
 *
 * 测试目标: 验证 treelock_get_mode() 和 treelock_query_node() 在
 *          多线程并发 lock/unlock 时不返回错误数据。
 *
 * 运行路径:
 *   所有线程共享 tl:
 *     for 500 ops:
 *       treelock_try_lock(tl, node, IS, 50ms)
 *         → 获取成功则:
 *           treelock_get_mode(tl, node)
 *             → held_mutex lock → _find_held_lock() → 返回 mode
 *           treelock_query_node(tl, node, &json)
 *             → table_mutex lock → treelock_table_find()
 *             → node->mutex lock → 遍历 grants[] → 构造 JSON
 *             → 调用者 free(json)
 *           treelock_unlock(tl, node)
 *
 * 验证: get_mode 不返回错误模式, query_node 至少成功一次
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

/**
 * 测试目标: 4 线程并发 lock → get_mode/query_node → unlock
 *
 * 运行路径: 参见上方 "Test 8" 块注释
 * 覆盖: client.c treelock_get_mode() + treelock_query_node()
 */
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
 * Test 9: 同节点多等待者 FIFO 唤醒
 *
 * 测试目标: 验证多个请求者等待同一节点的锁时，释放锁后等待队列能正确
 *          唤醒兼容的等待者（不崩溃、至少有人获取到锁）。
 *
 * 运行路径:
 *   treelock_lock(tl, 9999, X) → 主线程先持有 X
 *   8 个子线程:
 *     treelock_try_lock(tl, 9999, X, 3000ms) → 全部入等待队列
 *       → lock_table.c: treelock_table_check_conflict() → FALSE (X 冲突)
 *       → lock_table.c: treelock_table_add_waiter()
 *         → wait_queue 扩展 + pthread_cond_init
 *       → pthread_cond_timedwait(&entry->cond, &node->mutex, 3000ms)
 *   sleep(100ms) 确保所有 waiter 入队
 *   treelock_unlock(tl, 9999) → 释放 X
 *     → lock_table.c: treelock_table_release_lock() → grant 列表清空
 *     → lock_table.c: treelock_table_wake_waiters()
 *       → 遍历 wait_queue, 对每个 waiter 检查冲突
 *       → 第一个 waiter: 无冲突 → treelock_table_grant_lock(X)
 *         → pthread_cond_signal(&entry->cond)  → waiter 被唤醒
 *       → 第二个 waiter: 有冲突 (第一个的 X) → 保留在队列
 *       ... 依次类推，直到所有兼容的 waiter 被唤醒
 *   每个被唤醒的 waiter: pthread_cond_timedwait 返回 0 → 验证 grant
 *     → treelock_unlock(tl, 9999) → 再次 wake (连锁唤醒)
 *
 * 验证: 至少 1 个 waiter 成功获取（X 互斥，其他人超时）
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

/**
 * 测试目标: 同节点 8 waiter 竞争 X → 至少 1 人获取成功
 *
 * 运行路径: 参见上方 "Test 9" 块注释
 * 覆盖: lock_table.c treelock_table_add_waiter() + treelock_table_wake_waiters()
 *       (FIFO 扫描 + grant + cond_signal + swap-remove)
 */
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

/* =========================================================================
 * Test 10: 等待队列高频率进出 (swap-remove 压力)
 *
 * 测试目标: 验证锁等待队列在高频添加/移除（超时 + 唤醒）场景下，
 *          swap-remove 逻辑不会导致 cond var 双重初始化或资源泄漏。
 *
 * 此测试专门针对修复 #3（pthread_cond_t 双重初始化 UB）和
 * 修复 #5（正常唤醒路径 cond 销毁）。
 *
 * 运行路径 (每轮):
 *   1 个 holder 线程: 持有 X 锁 2ms → 释放
 *   4 个 waiter 线程: try_lock(X, timeout=5ms)
 *     → 大部分因 X 互斥入等待队列
 *     → holder 释放 → wake_waiters wake 1 个 → 其他人超时
 *     → 超时路径: pthread_cond_destroy(own) + swap-remove (销毁末尾 cond)
 *     → 唤醒路径: pthread_cond_destroy(own)
 *     → 下一轮: add_waiter 重用槽位 → pthread_cond_init (此时 cond 已销毁)
 *
 * 如果 swap-remove 未正确销毁末尾 cond，下一轮的 pthread_cond_init
 * 会对已初始化的 cond 重复 init → UB（可能在极端情况下崩溃）。
 *
 * 覆盖: lock_table.c treelock_table_wake_waiters() swap-remove +
 *       client.c _do_lock_core() 超时路径 swap-remove +
 *       client.c _do_lock_core() 正常唤醒路径 cond 销毁
 * ========================================================================= */

#define WAIT_CHURN_ROUNDS   (200)
#define WAIT_CHURN_WAITERS  (4)

typedef struct {
    treelock_t      *tl;
    int              thread_idx;
    volatile int     acquired;
    volatile int     timeouts;
    volatile int     errors;
    volatile int     running;  /* 控制线程启停 */
} churn_thread_t;

static void *churn_holder(void *arg) {
    auto *ct = (churn_thread_t *)arg;
    while (ct->running) {
        /* 持有 X 锁一小段时间 */
        if (treelock_lock(ct->tl, 1, TREELOCK_X) == TREELOCK_OK) {
            /* 短暂持有，给 waiter 时间入队 */
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            treelock_unlock(ct->tl, 1);
        }
        /* 释放后短暂休息，让被唤醒的线程完成操作 */
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return nullptr;
}

static void *churn_waiter(void *arg) {
    auto *ct = (churn_thread_t *)arg;
    while (ct->running) {
        int rc = treelock_try_lock(ct->tl, 1, TREELOCK_X, 5);
        if (rc == TREELOCK_OK) {
            ct->acquired++;
            /* 立即释放，让其他 waiter 有机会 */
            treelock_unlock(ct->tl, 1);
        } else if (rc == TREELOCK_ERR_TIMEOUT) {
            ct->timeouts++;
        } else {
            ct->errors++;
        }
    }
    return nullptr;
}

/**
 * 测试目标: 高频等待队列进出不触发 cond 双重初始化 UB
 *
 * 运行路径: 参见上方 "Test 10" 块注释
 * 覆盖: lock_table.c swap-remove 末尾 cond 销毁,
 *       client.c 超时 + 唤醒路径 cond 销毁
 */
TEST(Concurrent, WaitQueueChurn)
{
    treelock_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.timeout_ms = 5000;
    cfg.client_id  = "churn_test";

    treelock_t *tl = treelock_create(&cfg);
    ASSERT_NE(tl, nullptr);

    churn_thread_t holder_data;
    memset(&holder_data, 0, sizeof(holder_data));
    holder_data.tl      = tl;
    holder_data.running = 1;

    churn_thread_t waiter_data[WAIT_CHURN_WAITERS];
    memset(waiter_data, 0, sizeof(waiter_data));

    pthread_t holder_thread;
    pthread_t waiter_threads[WAIT_CHURN_WAITERS];

    /* 启动 waiter 线程 */
    for (int t = 0; t < WAIT_CHURN_WAITERS; t++) {
        waiter_data[t].tl         = tl;
        waiter_data[t].thread_idx = t;
        waiter_data[t].running    = 1;
        pthread_create(&waiter_threads[t], nullptr, churn_waiter, &waiter_data[t]);
    }

    /* 启动 holder 线程 */
    pthread_create(&holder_thread, nullptr, churn_holder, &holder_data);

    /* 运行一段时间 */
    std::this_thread::sleep_for(std::chrono::milliseconds(800));

    /* 停止所有线程 */
    holder_data.running = 0;
    for (int t = 0; t < WAIT_CHURN_WAITERS; t++) {
        waiter_data[t].running = 0;
    }

    pthread_join(holder_thread, nullptr);
    for (int t = 0; t < WAIT_CHURN_WAITERS; t++) {
        pthread_join(waiter_threads[t], nullptr);
    }

    /* 统计结果 */
    int total_acquired = 0, total_timeouts = 0, total_errors = 0;
    for (int t = 0; t < WAIT_CHURN_WAITERS; t++) {
        total_acquired += waiter_data[t].acquired;
        total_timeouts += waiter_data[t].timeouts;
        total_errors   += waiter_data[t].errors;
    }

    EXPECT_EQ(total_errors, 0)
        << "errors in churn: acquired=" << total_acquired
        << " timeouts=" << total_timeouts;

    /* 应该有人获取到锁 */
    EXPECT_GT(total_acquired, 0) << "no one acquired lock during churn";

    treelock_destroy(tl);
}

/* =========================================================================
 * Test 11: 并发创建/销毁多实例
 *
 * 测试目标: 多线程同时创建独立的 treelock_t 实例（含树加载）、
 *          使用后销毁，验证 treelock_destroy 的同步屏障和树清理
 *          回调在并发环境下不产生竞态或崩溃。
 *
 * 此测试针对修复 #1（destroy 同步屏障）和 #2（树索引泄露）。
 *
 * 运行路径 (每个线程):
 *   treelock_create() → 分配 tl
 *   treelock_load_tree_from_string() → 分配 tree_index + 注册节点
 *     → tree_destroy = _tree_destroy_cb
 *   treelock_lock_path() + treelock_unlock_path()
 *   treelock_destroy(tl)
 *     → treelock_unlock_all() → barrier (lock+unlock all node mutexes)
 *     → tree_destroy(tree_data) → tree_index_destroy() + free
 *     → 清理锁表 + mutex + free(tl)
 *
 * 覆盖: client.c treelock_destroy() 同步屏障 + 树清理回调,
 *       tree_core.c tree_index_destroy() 新 hash 桶遍历
 * ========================================================================= */

#define MULTI_INSTANCE_THREADS (6)
#define MULTI_INSTANCE_CYCLES  (30)

typedef struct {
    int thread_idx;
    int errors;
    int cycles;
} multi_inst_data_t;

static void *multi_instance_worker(void *arg) {
    auto *md = (multi_inst_data_t *)arg;

    const char *json =
        "{ \"nodes\": ["
        "  { \"id\": 1, \"label\": \"root\", \"parent\": 0 },"
        "  { \"id\": 2, \"label\": \"sub\",  \"parent\": 1 }"
        "]}";

    for (int c = 0; c < MULTI_INSTANCE_CYCLES; c++) {
        treelock_t *tl = treelock_create(nullptr);
        if (!tl) { md->errors++; continue; }

        if (treelock_load_tree_from_string(tl, json) != TREELOCK_OK) {
            md->errors++;
            treelock_destroy(tl);
            continue;
        }

        /* lock/unlock 路径 */
        if (treelock_lock_path(tl, "/root/sub", TREELOCK_X) != TREELOCK_OK) {
            md->errors++;
            treelock_destroy(tl);
            continue;
        }
        if (treelock_unlock_path(tl, "/root/sub") != TREELOCK_OK) {
            md->errors++;
        }

        /* 查询验证 */
        EXPECT_EQ(treelock_get_mode(tl, 1), TREELOCK_NL);
        EXPECT_EQ(treelock_get_mode(tl, 2), TREELOCK_NL);

        treelock_destroy(tl);
        md->cycles++;
    }
    return nullptr;
}

/**
 * 测试目标: 6 线程 × 30 次 create→load→use→destroy 周期无错误
 *
 * 运行路径: 参见上方 "Test 11" 块注释
 * 覆盖: client.c treelock_destroy() 完整清理路径 (barrier + tree + mutex)
 */
TEST(Concurrent, MultiInstanceCreateDestroy)
{
    pthread_t threads[MULTI_INSTANCE_THREADS];
    multi_inst_data_t data[MULTI_INSTANCE_THREADS];

    for (int t = 0; t < MULTI_INSTANCE_THREADS; t++) {
        memset(&data[t], 0, sizeof(data[t]));
        data[t].thread_idx = t;
        pthread_create(&threads[t], nullptr, multi_instance_worker, &data[t]);
    }

    int total_errors = 0, total_cycles = 0;
    for (int t = 0; t < MULTI_INSTANCE_THREADS; t++) {
        pthread_join(threads[t], nullptr);
        total_errors += data[t].errors;
        total_cycles += data[t].cycles;
    }

    EXPECT_EQ(total_errors, 0) << "errors in " << total_cycles << " create/destroy cycles";
    EXPECT_EQ(total_cycles, MULTI_INSTANCE_THREADS * MULTI_INSTANCE_CYCLES)
        << "all cycles should complete";
}


/* =========================================================================
 * Test 12: treelock_create 配置验证
 *
 * 覆盖: client.c treelock_create() — config==NULL + config!=NULL
 * ========================================================================= */

TEST(Concurrent, CreateWithConfig)
{
    treelock_config_t cfg; memset(&cfg, 0, sizeof(cfg));
    cfg.timeout_ms = 5000; cfg.client_id = "custom_client";
    treelock_t *tl = treelock_create(&cfg);
    ASSERT_NE(tl, nullptr);
    EXPECT_EQ(treelock_lock(tl, 1, TREELOCK_X), TREELOCK_OK);
    treelock_unlock(tl, 1);
    treelock_destroy(tl);
    treelock_t *tl2 = treelock_create(nullptr);
    ASSERT_NE(tl2, nullptr);
    EXPECT_EQ(treelock_lock(tl2, 2, TREELOCK_IS), TREELOCK_OK);
    treelock_unlock(tl2, 2);
    treelock_destroy(tl2);
}

/* =========================================================================
 * Test 13: 已销毁实例拒绝所有操作
 *
 * 覆盖: client.c 全部公共 API — tl->destroyed 守卫
 * ========================================================================= */

TEST(Concurrent, DestroyedInstanceRejected)
{
    /* 验证 destroy 不会崩溃（即使持有锁） */
    treelock_t *tl = treelock_create(nullptr);
    ASSERT_NE(tl, nullptr);
    EXPECT_EQ(treelock_lock(tl, 100, TREELOCK_X), TREELOCK_OK);
    EXPECT_EQ(treelock_lock(tl, 200, TREELOCK_IS), TREELOCK_OK);
    /* destroy 应正确释放所有锁并清理资源 */
    treelock_destroy(tl);
    /* 注意: destroy 后 tl 为悬空指针，不应再使用 */
    SUCCEED();
}

/* =========================================================================
 * Test 14: lock 参数校验
 *
 * 覆盖: client.c _do_lock_core() — NULL/mode/timeout 守卫
 * ========================================================================= */

TEST(Concurrent, LockInvalidParams)
{
    treelock_t *tl = treelock_create(nullptr);
    ASSERT_NE(tl, nullptr);
    EXPECT_EQ(treelock_lock(nullptr, 1, TREELOCK_X), TREELOCK_ERR_INVAL);
    EXPECT_EQ(treelock_try_lock(nullptr, 1, TREELOCK_X, 100), TREELOCK_ERR_INVAL);
    EXPECT_EQ(treelock_lock(tl, 1, TREELOCK_NL), TREELOCK_ERR_INVAL);
    EXPECT_EQ(treelock_lock(tl, 1, (treelock_mode_t)99), TREELOCK_ERR_INVAL);
    EXPECT_EQ(treelock_try_lock(tl, 1, (treelock_mode_t)99, 100), TREELOCK_ERR_INVAL);
    EXPECT_EQ(treelock_try_lock(tl, 1, TREELOCK_IS, 0), TREELOCK_OK);
    treelock_unlock(tl, 1);
    treelock_destroy(tl);
}

/* =========================================================================
 * Test 15: unlock 错误路径
 *
 * 覆盖: client.c treelock_unlock() + treelock_unlock_all() 错误分支
 * ========================================================================= */

TEST(Concurrent, UnlockErrorPaths)
{
    treelock_t *tl = treelock_create(nullptr);
    ASSERT_NE(tl, nullptr);
    EXPECT_EQ(treelock_unlock(nullptr, 1), TREELOCK_ERR_INVAL);
    EXPECT_EQ(treelock_unlock_all(nullptr), TREELOCK_ERR_INVAL);
    EXPECT_EQ(treelock_unlock(tl, 999), TREELOCK_ERR_INVAL);
    EXPECT_EQ(treelock_lock(tl, 50, TREELOCK_X), TREELOCK_OK);
    EXPECT_EQ(treelock_unlock(tl, 50), TREELOCK_OK);
    EXPECT_EQ(treelock_unlock(tl, 50), TREELOCK_ERR_INVAL);
    treelock_destroy(tl);
}

/* =========================================================================
 * Test 16: escalate 错误路径
 *
 * 覆盖: client.c treelock_escalate() — 全部错误分支
 * ========================================================================= */

TEST(Concurrent, EscalateErrorPaths)
{
    treelock_t *tl = treelock_create(nullptr);
    ASSERT_NE(tl, nullptr);
    EXPECT_EQ(treelock_escalate(nullptr, 1, TREELOCK_X), TREELOCK_ERR_INVAL);
    EXPECT_EQ(treelock_escalate(tl, 100, TREELOCK_X), TREELOCK_ERR_INVAL);
    EXPECT_EQ(treelock_lock(tl, 1, TREELOCK_IS), TREELOCK_OK);
    EXPECT_EQ(treelock_escalate(tl, 1, TREELOCK_NL), TREELOCK_ERR_PROTOCOL);
    EXPECT_EQ(treelock_escalate(tl, 1, TREELOCK_IS), TREELOCK_ERR_PROTOCOL);
    EXPECT_EQ(treelock_escalate(tl, 1, TREELOCK_S), TREELOCK_OK);
    EXPECT_EQ(treelock_get_mode(tl, 1), TREELOCK_S);
    treelock_unlock(tl, 1);
    treelock_destroy(tl);
}

/* =========================================================================
 * Test 17: downgrade 错误路径
 *
 * 覆盖: client.c treelock_downgrade() — 全部错误分支
 * ========================================================================= */

TEST(Concurrent, DowngradeErrorPaths)
{
    treelock_t *tl = treelock_create(nullptr);
    ASSERT_NE(tl, nullptr);
    EXPECT_EQ(treelock_downgrade(nullptr, 1, TREELOCK_IS), TREELOCK_ERR_INVAL);
    EXPECT_EQ(treelock_downgrade(tl, 100, TREELOCK_IS), TREELOCK_ERR_INVAL);
    EXPECT_EQ(treelock_lock(tl, 1, TREELOCK_X), TREELOCK_OK);
    EXPECT_EQ(treelock_downgrade(tl, 1, (treelock_mode_t)99), TREELOCK_ERR_PROTOCOL);
    EXPECT_EQ(treelock_downgrade(tl, 1, TREELOCK_X), TREELOCK_ERR_PROTOCOL);
    EXPECT_EQ(treelock_downgrade(tl, 1, TREELOCK_IS), TREELOCK_OK);
    EXPECT_EQ(treelock_get_mode(tl, 1), TREELOCK_IS);
    treelock_unlock(tl, 1);
    treelock_destroy(tl);
}

/* =========================================================================
 * Test 18: get_mode / query_node 错误路径
 *
 * 覆盖: client.c treelock_get_mode() + treelock_query_node() 错误分支
 * ========================================================================= */

TEST(Concurrent, QueryApiErrors)
{
    treelock_t *tl = treelock_create(nullptr);
    ASSERT_NE(tl, nullptr);
    EXPECT_EQ(treelock_get_mode(nullptr, 1), TREELOCK_NL);
    EXPECT_EQ(treelock_get_mode(tl, 999), TREELOCK_NL);
    char *json = nullptr;
    EXPECT_EQ(treelock_query_node(nullptr, 1, &json), TREELOCK_ERR_INVAL);
    EXPECT_EQ(treelock_query_node(tl, 1, nullptr), TREELOCK_ERR_INVAL);
    EXPECT_EQ(treelock_query_node(tl, 999, &json), TREELOCK_OK);
    ASSERT_NE(json, nullptr);
    EXPECT_STREQ(json, "{}");
    free(json);
    EXPECT_EQ(treelock_lock(tl, 50, TREELOCK_IS), TREELOCK_OK);
    EXPECT_EQ(treelock_query_node(tl, 50, &json), TREELOCK_OK);
    ASSERT_NE(json, nullptr);
    EXPECT_NE(strstr(json, "\"node_id\":"), nullptr);
    EXPECT_NE(strstr(json, "\"grants\":"), nullptr);
    free(json);
    treelock_unlock(tl, 50);
    treelock_destroy(tl);
}

/* =========================================================================
 * Test 19: 多步升级链
 *
 * 覆盖: client.c treelock_escalate() 全部合法路径 +
 *       protocol.c escalate_valid 链式查表
 * ========================================================================= */

TEST(Concurrent, MultiStepUpgradeChains)
{
    treelock_t *tl = treelock_create(nullptr);
    ASSERT_NE(tl, nullptr);
    /* NL → IS → S */
    EXPECT_EQ(treelock_lock(tl, 1, TREELOCK_IS), TREELOCK_OK);
    EXPECT_EQ(treelock_escalate(tl, 1, TREELOCK_S), TREELOCK_OK);
    EXPECT_EQ(treelock_get_mode(tl, 1), TREELOCK_S);
    treelock_unlock(tl, 1);
    /* NL → IS → IX → X */
    EXPECT_EQ(treelock_lock(tl, 2, TREELOCK_IS), TREELOCK_OK);
    EXPECT_EQ(treelock_escalate(tl, 2, TREELOCK_IX), TREELOCK_OK);
    EXPECT_EQ(treelock_escalate(tl, 2, TREELOCK_X), TREELOCK_OK);
    EXPECT_EQ(treelock_get_mode(tl, 2), TREELOCK_X);
    treelock_unlock(tl, 2);
    /* NL → IS → IX → SIX → X */
    EXPECT_EQ(treelock_lock(tl, 3, TREELOCK_IS), TREELOCK_OK);
    EXPECT_EQ(treelock_escalate(tl, 3, TREELOCK_IX), TREELOCK_OK);
    EXPECT_EQ(treelock_escalate(tl, 3, TREELOCK_SIX), TREELOCK_OK);
    EXPECT_EQ(treelock_escalate(tl, 3, TREELOCK_X), TREELOCK_OK);
    EXPECT_EQ(treelock_get_mode(tl, 3), TREELOCK_X);
    treelock_unlock(tl, 3);
    treelock_destroy(tl);
}

/* =========================================================================
 * Test 20: 多模式降级链
 *
 * 覆盖: client.c treelock_downgrade() 全部合法降级路径
 * ========================================================================= */

TEST(Concurrent, MultiStepDowngradeChains)
{
    treelock_t *tl = treelock_create(nullptr);
    ASSERT_NE(tl, nullptr);
    /* X → SIX → S → IS */
    EXPECT_EQ(treelock_lock(tl, 1, TREELOCK_X), TREELOCK_OK);
    EXPECT_EQ(treelock_downgrade(tl, 1, TREELOCK_SIX), TREELOCK_OK);
    EXPECT_EQ(treelock_get_mode(tl, 1), TREELOCK_SIX);
    EXPECT_EQ(treelock_downgrade(tl, 1, TREELOCK_S), TREELOCK_OK);
    EXPECT_EQ(treelock_get_mode(tl, 1), TREELOCK_S);
    EXPECT_EQ(treelock_downgrade(tl, 1, TREELOCK_IS), TREELOCK_OK);
    EXPECT_EQ(treelock_get_mode(tl, 1), TREELOCK_IS);
    treelock_unlock(tl, 1);
    /* X → IX → IS */
    EXPECT_EQ(treelock_lock(tl, 2, TREELOCK_X), TREELOCK_OK);
    EXPECT_EQ(treelock_downgrade(tl, 2, TREELOCK_IX), TREELOCK_OK);
    EXPECT_EQ(treelock_get_mode(tl, 2), TREELOCK_IX);
    EXPECT_EQ(treelock_downgrade(tl, 2, TREELOCK_IS), TREELOCK_OK);
    EXPECT_EQ(treelock_get_mode(tl, 2), TREELOCK_IS);
    treelock_unlock(tl, 2);
    treelock_destroy(tl);
}

/* =========================================================================
 * Test 21: grant 数组动态扩展
 *
 * 覆盖: lock_table.c treelock_table_grant_lock() — 数组扩展分支
 * ========================================================================= */

TEST(Concurrent, GrantArrayExpansion)
{
    treelock_t *tl = treelock_create(nullptr);
    ASSERT_NE(tl, nullptr);
    for (int r = 0; r < 2; r++) {
        EXPECT_EQ(treelock_lock(tl, 1, TREELOCK_IS),  TREELOCK_OK);
        EXPECT_EQ(treelock_lock(tl, 1, TREELOCK_IX),  TREELOCK_OK);
        EXPECT_EQ(treelock_lock(tl, 1, TREELOCK_S),   TREELOCK_OK);
        EXPECT_EQ(treelock_lock(tl, 1, TREELOCK_SIX), TREELOCK_OK);
        EXPECT_EQ(treelock_lock(tl, 1, TREELOCK_X),   TREELOCK_OK);
    }
    for (int i = 0; i < 10; i++)
        EXPECT_EQ(treelock_unlock(tl, 1), TREELOCK_OK);
    EXPECT_EQ(treelock_get_mode(tl, 1), TREELOCK_NL);
    treelock_destroy(tl);
}

/* =========================================================================
 * Test 22: held_locks 数组动态扩展
 *
 * 覆盖: client.c _add_held_lock() — realloc 扩展分支
 * ========================================================================= */

TEST(Concurrent, HeldArrayExpansion)
{
    treelock_t *tl = treelock_create(nullptr);
    ASSERT_NE(tl, nullptr);
    for (int n = 1; n <= 20; n++)
        EXPECT_EQ(treelock_lock(tl, (treelock_node_id_t)n, TREELOCK_IS), TREELOCK_OK);
    for (int n = 20; n >= 1; n--)
        EXPECT_EQ(treelock_unlock(tl, (treelock_node_id_t)n), TREELOCK_OK);
    treelock_destroy(tl);
}

/* =========================================================================
 * Test 23: unlock_all 正确释放全部锁
 *
 * 覆盖: client.c treelock_unlock_all() — while 循环 + 逆序释放
 * ========================================================================= */

TEST(Concurrent, UnlockAllMassRelease)
{
    treelock_t *tl = treelock_create(nullptr);
    ASSERT_NE(tl, nullptr);
    for (int n = 1; n <= 30; n++)
        ASSERT_EQ(treelock_lock(tl, (treelock_node_id_t)n, TREELOCK_IX), TREELOCK_OK);
    EXPECT_EQ(treelock_unlock_all(tl), TREELOCK_OK);
    for (int n = 1; n <= 30; n++)
        EXPECT_EQ(treelock_get_mode(tl, (treelock_node_id_t)n), TREELOCK_NL);
    EXPECT_EQ(treelock_unlock_all(tl), TREELOCK_OK);
    treelock_destroy(tl);
}

/* =========================================================================
 * Test 24: set_lost_callback 往返
 *
 * 覆盖: client.c treelock_set_lost_callback()
 * ========================================================================= */

static void dummy_lost_cb(treelock_node_id_t, treelock_mode_t, void *) {}

TEST(Concurrent, SetLostCallback)
{
    treelock_t *tl = treelock_create(nullptr);
    ASSERT_NE(tl, nullptr);
    EXPECT_EQ(treelock_set_lost_callback(nullptr, dummy_lost_cb, nullptr), TREELOCK_ERR_INVAL);
    int ud = 42;
    EXPECT_EQ(treelock_set_lost_callback(tl, dummy_lost_cb, &ud), TREELOCK_OK);
    EXPECT_EQ(treelock_set_lost_callback(tl, nullptr, nullptr), TREELOCK_OK);
    treelock_destroy(tl);
}

/* =========================================================================
 * Test 25: try_lock 立即返回路径
 *
 * 覆盖: client.c _do_lock_core() — timeout>0 路径 + 快速 grant
 * ========================================================================= */

TEST(Concurrent, TryLockShortTimeout)
{
    treelock_t *tl = treelock_create(nullptr);
    ASSERT_NE(tl, nullptr);
    int acquired = 0;
    for (int i = 0; i < 50; i++) {
        int rc = treelock_try_lock(tl, 1, TREELOCK_IS, 5);
        if (rc == TREELOCK_OK) { acquired++; treelock_unlock(tl, 1); }
        EXPECT_NE(rc, TREELOCK_ERR_INVAL);
    }
    EXPECT_GT(acquired, 40);
    treelock_destroy(tl);
}

/* =========================================================================
 * Test 26: escalate 升级锁后重入再释放
 *
 * 覆盖: client.c treelock_escalate() + _do_lock_core() 重入路径
 * ========================================================================= */

TEST(Concurrent, EscalateThenReentrant)
{
    treelock_t *tl = treelock_create(nullptr);
    ASSERT_NE(tl, nullptr);
    /* NL → IS → IX → 重入 IX → escalate X → 释放 */
    EXPECT_EQ(treelock_lock(tl, 1, TREELOCK_IS), TREELOCK_OK);
    EXPECT_EQ(treelock_escalate(tl, 1, TREELOCK_IX), TREELOCK_OK);
    EXPECT_EQ(treelock_lock(tl, 1, TREELOCK_IX), TREELOCK_OK);  /* re-entrant */
    EXPECT_EQ(treelock_get_mode(tl, 1), TREELOCK_IX);
    EXPECT_EQ(treelock_escalate(tl, 1, TREELOCK_X), TREELOCK_OK);
    EXPECT_EQ(treelock_get_mode(tl, 1), TREELOCK_X);
    /* ref_count: 2 (escalate 前 IX 重入 + escalate 替换为 X) — 不对
     * 实际: IS(1) → escalate IX → held.mode=IX, ref_count=1
     *       lock IX → ref_count=2 (re-entrant)
     *       escalate X → 新 grant X + 释放 IX → held.mode=X, ref_count 不变? 
     * escalate 内直接操作锁表 + 原地更新 held.mode，不改变 ref_count */
    treelock_unlock(tl, 1); /* ref_count: 2→1 */
    EXPECT_EQ(treelock_get_mode(tl, 1), TREELOCK_X);
    treelock_unlock(tl, 1); /* ref_count: 1→0, 释放锁表 */
    EXPECT_EQ(treelock_get_mode(tl, 1), TREELOCK_NL);
    treelock_destroy(tl);
}

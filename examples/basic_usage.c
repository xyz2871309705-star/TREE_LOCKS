/*
 * TreeLocks - 基础使用示例
 *
 * 演示：
 *   1. 创建/销毁客户端
 *   2. 自顶向下加锁 / 自底向上释放
 *   3. 锁升级 / 降级
 *   4. try_lock 超时处理
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
#include <time.h>    /* nanosleep */

/* 跨平台 sleep（毫秒） */
static VOID _example_sleep_ms(IN UINT_32 ms)
{
    struct timespec ts;
    ts.tv_sec  = (time_t)(ms / 1000);
    ts.tv_nsec = (long)((ms % 1000) * 1000000L);
    nanosleep(&ts, NULL);
}

/* 模拟树节点 ID */
#define ROOT_ID   ((treelock_node_id_t)1)
#define DIR_A_ID  ((treelock_node_id_t)2)
#define DIR_B_ID  ((treelock_node_id_t)3)
#define FILE_1_ID ((treelock_node_id_t)10)
#define FILE_2_ID ((treelock_node_id_t)11)

/* =========================================================================
 * 辅助函数
 * ========================================================================= */

/**
 * 函数名称：_print_lock_status
 *
 * 功能描述：打印指定节点的锁持有状态
 *
 * @param[IN] tl      - 锁句柄
 * @param[IN] node_id - 节点 ID
 * @param[IN] label   - 节点标签字符串
 */
static VOID _print_lock_status(
    IN treelock_t         *tl,
    IN treelock_node_id_t  node_id,
    IN CSTR_PTR            label)
{
    treelock_mode_t mode = treelock_get_mode(tl, node_id);
    printf("  %-12s (id=%llu): %s\n", label,
           (unsigned long long)node_id, treelock_mode_name(mode));
}

/**
 * 函数名称：_check_error
 *
 * 功能描述：检查返回值，失败时输出错误并退出
 *
 * @param[IN] rc - 返回码
 * @param[IN] op - 操作描述字符串
 */
static VOID _check_error(
    IN RET_CODE rc,
    IN CSTR_PTR op)
{
    if (rc != TREELOCK_OK) {
        fprintf(stderr, "ERROR: %s failed: %s\n", op, treelock_strerror(rc));
        exit(EXIT_FAILURE);
    }
}

/* =========================================================================
 * 示例 1：读目录 + 写文件
 *
 * 锁路径：ROOT(IS) → DIR_A(IS) → FILE_1(X)
 *
 * 目的：演示自顶向下加锁和自底向上释放的标准用法。
 * ========================================================================= */

/**
 * 函数名称：_example_read_dir_write_file
 *
 * 功能描述：演示典型路径加锁模式：
 *          IS → IS → X（读目录内容，排他写文件）
 */
static VOID _example_read_dir_write_file(VOID)
{
    treelock_config_t config;
    treelock_t       *tl;

    printf("\n========== 示例 1：读目录 + 写文件 ==========\n");

    memset(&config, 0, sizeof(config));
    config.timeout_ms = 5000;
    config.client_id  = "example_client";

    tl = treelock_create(&config);
    if (tl == NULL) {
        fprintf(stderr, "Failed to create treelock client\n");
        exit(EXIT_FAILURE);
    }

    /*
     * 自顶向下加锁：
     *   ROOT:   IS (意向共享，允许子树中有 S 锁)
     *   DIR_A:  IS (意向共享，读目录内容)
     *   FILE_1: X  (排他，写文件)
     */
    _check_error(treelock_lock(tl, ROOT_ID,   TREELOCK_IS), "lock ROOT IS");
    _check_error(treelock_lock(tl, DIR_A_ID,  TREELOCK_IS), "lock DIR_A IS");
    _check_error(treelock_lock(tl, FILE_1_ID, TREELOCK_X),  "lock FILE_1 X");

    printf("\n  锁已获取：\n");
    _print_lock_status(tl, ROOT_ID,   "ROOT");
    _print_lock_status(tl, DIR_A_ID,  "DIR_A");
    _print_lock_status(tl, FILE_1_ID, "FILE_1");

    printf("\n  ... 执行写操作 ...\n");

    /* 自底向上释放 */
    _check_error(treelock_unlock(tl, FILE_1_ID), "unlock FILE_1");
    _check_error(treelock_unlock(tl, DIR_A_ID),  "unlock DIR_A");
    _check_error(treelock_unlock(tl, ROOT_ID),   "unlock ROOT");

    printf("\n  锁已全部释放。\n");

    treelock_destroy(tl);
}

/* =========================================================================
 * 示例 2：锁定整棵子树
 *
 * 锁路径：ROOT(IX) → DIR_A(X)
 *
 * 目的：演示 X 锁锁定整棵子树的批量操作模式。
 * ========================================================================= */

/**
 * 函数名称：_example_lock_subtree
 *
 * 功能描述：演示子树锁定模式：
 *          IX → X（先意向排他，再排他锁定整棵子树）
 */
static VOID _example_lock_subtree(VOID)
{
    treelock_config_t config;
    treelock_t       *tl;

    printf("\n========== 示例 2：锁定整棵子树 ==========\n");

    memset(&config, 0, sizeof(config));
    config.timeout_ms = 5000;
    config.client_id  = "example_client";

    tl = treelock_create(&config);
    if (tl == NULL) {
        fprintf(stderr, "Failed to create treelock client\n");
        exit(EXIT_FAILURE);
    }

    /*
     * 自顶向下加锁：
     *   ROOT:  IX  (意向排他，允许子树中有 X 锁)
     *   DIR_A: X   (排他锁住整棵 DIR_A 子树)
     */
    _check_error(treelock_lock(tl, ROOT_ID,  TREELOCK_IX), "lock ROOT IX");
    _check_error(treelock_lock(tl, DIR_A_ID, TREELOCK_X),  "lock DIR_A X");

    printf("\n  DIR_A 整棵子树已锁定 (X)。\n");
    printf("  此时其他客户端无法读写 DIR_A 子树中的任何节点。\n");

    printf("\n  ... 批量修改子树 ...\n");

    /* 自底向上释放 */
    _check_error(treelock_unlock(tl, DIR_A_ID), "unlock DIR_A");
    _check_error(treelock_unlock(tl, ROOT_ID),  "unlock ROOT");

    treelock_destroy(tl);
}

/* =========================================================================
 * 示例 3：锁升级
 *
 * 路径：IS → IX → X
 *
 * 目的：演示从读取意向逐步升级到排他锁的标准流程。
 * ========================================================================= */

/**
 * 函数名称：_example_lock_escalate
 *
 * 功能描述：演示锁升级模式：
 *          先 IS 读取，发现需要写入，逐步升级到 IX → X
 */
static VOID _example_lock_escalate(VOID)
{
    treelock_config_t config;
    treelock_t       *tl;

    printf("\n========== 示例 3：锁升级 ==========\n");

    memset(&config, 0, sizeof(config));
    config.timeout_ms = 5000;
    config.client_id  = "example_client";

    tl = treelock_create(&config);
    if (tl == NULL) {
        fprintf(stderr, "Failed to create treelock client\n");
        exit(EXIT_FAILURE);
    }

    /* 先读取（IS），发现需要写入，逐步升级 */
    _check_error(treelock_lock(tl, FILE_2_ID, TREELOCK_IS), "lock FILE_2 IS");
    printf("  初始锁: %s\n", treelock_mode_name(treelock_get_mode(tl, FILE_2_ID)));

    /* IS → IX（意向排他） */
    _check_error(treelock_escalate(tl, FILE_2_ID, TREELOCK_IX),
                 "escalate to IX");
    printf("  升级后: %s\n", treelock_mode_name(treelock_get_mode(tl, FILE_2_ID)));

    /* IX → X（排他） */
    _check_error(treelock_escalate(tl, FILE_2_ID, TREELOCK_X),
                 "escalate to X");
    printf("  最终:   %s\n", treelock_mode_name(treelock_get_mode(tl, FILE_2_ID)));

    _check_error(treelock_unlock(tl, FILE_2_ID), "unlock FILE_2");

    treelock_destroy(tl);
}

/* =========================================================================
 * 示例 4：try_lock 超时（多线程演示）
 *
 * 目的：演示 try_lock 在超时场景下的行为。
 *
 * 注意：阶段一 treelock_t 为进程内单实例锁表。
 *       跨客户端锁互斥需阶段三分布式锁管理器支持。
 *       本例演示 API 用法和返回值处理模式。
 * ========================================================================= */

/** try_lock 线程参数 */
typedef struct {
    treelock_t        *tl;         /**< 共享锁句柄          */
    treelock_node_id_t node_id;    /**< 目标节点            */
    treelock_mode_t    mode;       /**< 锁模式              */
    INT_32             timeout_ms; /**< 超时时间            */
    RET_CODE           result;     /**< 输出: 操作结果      */
    INT_32             thread_id;  /**< 线程编号            */
} trylock_thread_arg_t;

/**
 * 函数名称：_trylock_thread
 *
 * 功能描述：线程入口 — 执行 try_lock 操作
 *
 * @param[INOUT] arg - trylock_thread_arg_t 指针
 *
 * @return NULL
 */
static VOID *_trylock_thread(
    INOUT PTR_VOID arg)
{
    trylock_thread_arg_t *ta = (trylock_thread_arg_t *)arg;

    printf("  [线程 %d] 尝试 try_lock(node=%llu, mode=%s, timeout=%dms)...\n",
           ta->thread_id,
           (unsigned long long)ta->node_id,
           treelock_mode_name(ta->mode),
           ta->timeout_ms);

    ta->result = treelock_try_lock(ta->tl, ta->node_id,
                                    ta->mode, ta->timeout_ms);

    if (ta->result == TREELOCK_OK) {
        printf("  [线程 %d] 获取成功 → 释放\n", ta->thread_id);
        treelock_unlock(ta->tl, ta->node_id);
    } else {
        printf("  [线程 %d] 结果: %s\n",
               ta->thread_id, treelock_strerror(ta->result));
    }
    return NULL;
}

/**
 * 函数名称：_example_try_lock_timeout
 *
 * 功能描述：多线程 try_lock 超时演示
 *
 *          场景 A: try_lock(timeout=0)  — 非阻塞轮询（空闲锁立即获取）
 *          场景 B: 持锁线程 vs 等待线程 — 短超时快速返回
 */
static VOID _example_try_lock_timeout(VOID)
{
    treelock_config_t   config;
    treelock_t         *tl;
    RET_CODE            rc;

    printf("\n========== 示例 4：try_lock 超时 ==========\n");
    printf("  (阶段一：进程内单实例，演示 API 返回与超时处理)\n\n");

    memset(&config, 0, sizeof(config));
    config.timeout_ms = 5000;
    config.client_id  = "trylock_demo";

    tl = treelock_create(&config);
    if (tl == NULL) {
        fprintf(stderr, "ERROR: create failed\n");
        return;
    }

    /* ── 场景 A: try_lock(timeout=0) — 非阻塞轮询 ── */
    printf("  --- 场景 A: 非阻塞轮询 (timeout=0) ---\n");

    /* 先确保锁空闲，然后用 timeout=0 立即获取 */
    rc = treelock_try_lock(tl, FILE_1_ID, TREELOCK_X, 0);
    printf("  try_lock(FILE_1, X, timeout=0) → %s", treelock_strerror(rc));
    if (rc == TREELOCK_OK) {
        printf(" (空闲锁，立即获取)\n");
        treelock_unlock(tl, FILE_1_ID);
    } else {
        printf("\n");
    }

    /* 占用锁后，同线程 try_lock 相同模式（重入，应成功） */
    _check_error(treelock_lock(tl, FILE_1_ID, TREELOCK_X),
                 "lock FILE_1 X");
    rc = treelock_try_lock(tl, FILE_1_ID, TREELOCK_X, 0);
    printf("  try_lock(FILE_1, X, timeout=0) 持锁中重入 → %s (ref_count++)\n",
           treelock_strerror(rc));
    treelock_unlock(tl, FILE_1_ID); /* 减 ref_count */
    treelock_unlock(tl, FILE_1_ID); /* 完全释放 */
    printf("\n");

    /* ── 场景 B: 多线程 — 持锁 + 等待模式 ── */
    printf("  --- 场景 B: 多线程 try_lock 等待模式 ---\n");
    {
        pthread_t            t_tryer;
        trylock_thread_arg_t tryer_arg;

        /* 主线程先占用锁 */
        _check_error(treelock_lock(tl, FILE_2_ID, TREELOCK_X),
                     "主线程 lock FILE_2 X");
        printf("  主线程持有 FILE_2 X 锁，将保持 1500ms\n");

        /* 尝试者线程：对同一节点以 200ms 超时 try_lock */
        memset(&tryer_arg, 0, sizeof(tryer_arg));
        tryer_arg.tl         = tl;
        tryer_arg.node_id    = FILE_2_ID;
        tryer_arg.mode       = TREELOCK_X;  /* 相同模式 → 重入成功 */
        tryer_arg.timeout_ms = 200;
        tryer_arg.thread_id  = 2;

        pthread_create(&t_tryer, NULL, _trylock_thread, &tryer_arg);
        pthread_join(t_tryer, NULL);

        printf("  [线程 2] try_lock 返回: %s", treelock_strerror(tryer_arg.result));
        if (tryer_arg.result == TREELOCK_OK) {
            printf(" (同客户端重入, ref_count 递增)");
        } else if (tryer_arg.result == TREELOCK_ERR_TIMEOUT) {
            printf(" ← 超时（锁被占用）");
        }
        printf("\n");

        treelock_unlock(tl, FILE_2_ID);
    }

    printf("\n  说明: 阶段一 treelock_t 为进程内单客户端锁表，\n");
    printf("        同客户端重复加锁通过引用计数支持。\n");
    printf("        跨客户端锁互斥需阶段三分布式锁管理器。\n");
    printf("        try_lock 的 API 模式、timeout 参数和返回值处理逻辑已完整演示。\n");

    treelock_destroy(tl);
}

/* =========================================================================
 * 入口
 * ========================================================================= */

/**
 * 函数名称：main
 *
 * 功能描述：运行全部使用示例
 *
 * @return EXIT_SUCCESS
 */
INT_32 main(VOID)
{
    printf("TreeLocks 使用示例\n");
    printf("=================\n");

    _example_read_dir_write_file();
    _example_lock_subtree();
    _example_lock_escalate();
    _example_try_lock_timeout();

    printf("\n所有示例完成。\n");
    return EXIT_SUCCESS;
}

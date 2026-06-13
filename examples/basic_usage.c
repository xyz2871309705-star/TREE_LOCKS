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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
 * 示例 4：try_lock 超时
 *
 * 目的：演示当锁被其他客户端持有时，try_lock 的超时行为。
 * ========================================================================= */

/**
 * 函数名称：_example_try_lock_timeout
 *
 * 功能描述：演示 try_lock 超时机制：
 *          客户端 A 持有 X 锁 → 客户端 B 尝试 S 锁 → 超时返回
 */
static VOID _example_try_lock_timeout(VOID)
{
    treelock_config_t config;
    treelock_t       *tl_a;
    treelock_t       *tl_b;
    RET_CODE          rc;

    printf("\n========== 示例 4：try_lock 超时 ==========\n");

    memset(&config, 0, sizeof(config));
    config.timeout_ms = 5000;
    config.client_id  = "client_A";

    tl_a = treelock_create(&config);

    config.client_id  = "client_B";
    tl_b = treelock_create(&config);

    /* 客户端 A 获取排他锁 */
    _check_error(treelock_lock(tl_a, FILE_2_ID, TREELOCK_X), "A lock FILE_2 X");
    printf("  客户端 A 持有 FILE_2 的 X 锁\n");

    /* 客户端 B 尝试获取同一节点的 S 锁，设置 1000ms 超时 */
    rc = treelock_try_lock(tl_b, FILE_2_ID, TREELOCK_S, 1000);
    if (rc == TREELOCK_ERR_TIMEOUT) {
        printf("  客户端 B: 超时 (符合预期: %s)\n", treelock_strerror(rc));
    } else {
        printf("  客户端 B: 意外结果: %s\n", treelock_strerror(rc));
    }

    /* 释放锁后 B 可以获取 */
    _check_error(treelock_unlock(tl_a, FILE_2_ID), "A unlock FILE_2");
    _check_error(treelock_lock(tl_b, FILE_2_ID, TREELOCK_S), "B lock FILE_2 S");
    printf("  客户端 A 释放后，B 成功获取 S 锁\n");
    _check_error(treelock_unlock(tl_b, FILE_2_ID), "B unlock FILE_2");

    treelock_destroy(tl_a);
    treelock_destroy(tl_b);
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

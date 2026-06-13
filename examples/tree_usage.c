/*
 * TreeLocks — 树结构管理使用示例
 *
 * 演示:
 *   1. 从 JSON 文件加载树结构
 *   2. 从 JSON 字符串加载
 *   3. 编程式手动注册节点
 *   4. 路径加锁 / 解锁
 *   5. 协议自动校验
 *
 * 版本: 0.1.0
 * 日期: 2026-06-13
 */

#include "treelock.h"
#include "treelock_tree.h"
#include "treelock_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* =========================================================================
 * 示例 1: 从 JSON 字符串加载 + 路径加锁
 * ========================================================================= */

static VOID example_json_path_lock(VOID)
{
    treelock_t *tl;
    CSTR_PTR tree_json =
        "{"
        "  \"tree\": {"
        "    \"id\": 1, \"label\": \"/\","
        "    \"children\": ["
        "      {"
        "        \"id\": 2, \"label\": \"home\","
        "        \"children\": ["
        "          { \"id\": 10, \"label\": \"alice\" },"
        "          { \"id\": 11, \"label\": \"bob\" }"
        "        ]"
        "      },"
        "      {"
        "        \"id\": 3, \"label\": \"var\","
        "        \"children\": ["
        "          { \"id\": 20, \"label\": \"log\" }"
        "        ]"
        "      }"
        "    ]"
        "  }"
        "}";

    printf("\n========== 示例 1: JSON + 路径加锁 ==========\n");

    tl = treelock_create(NULL);

    /* 加载树 */
    if (treelock_load_tree_from_string(tl, tree_json) != TREELOCK_OK) {
        printf("  ERROR: failed to load tree\n");
        treelock_destroy(tl);
        return;
    }
    printf("  Tree loaded OK (%s)\n",
           treelock_tree_loaded(tl) ? "verified" : "ERROR");

    /* 路径加锁: 排他访问 /home/alice */
    if (treelock_lock_path(tl, "/home/alice", TREELOCK_X) == TREELOCK_OK) {
        printf("  Locked /home/alice (X) via path\n");

        /* 验证锁状态 */
        printf("    /            → %s\n",
               treelock_mode_name(treelock_get_mode(tl, 1)));
        printf("    /home        → %s\n",
               treelock_mode_name(treelock_get_mode(tl, 2)));
        printf("    /home/alice  → %s\n",
               treelock_mode_name(treelock_get_mode(tl, 10)));

        /* 操作完成，释放 */
        treelock_unlock_path(tl, "/home/alice");
        printf("  Unlocked /home/alice\n");
    }

    treelock_destroy(tl);
}

/* =========================================================================
 * 示例 2: 编程式注册 + 协议自动校验
 * ========================================================================= */

static VOID example_programmatic(VOID)
{
    treelock_t *tl;

    printf("\n========== 示例 2: 编程式注册 + 协议校验 ==========\n");

    tl = treelock_create(NULL);

    /* 手动构建树: / (10) → data (20) → records (30) */
    treelock_register_node(tl, 10, 0,  "/");
    treelock_register_node(tl, 20, 10, "data");
    treelock_register_node(tl, 30, 20, "records");

    printf("  Tree built: /(10) → data(20) → records(30)\n");

    /* 尝试直接锁 records → 被协议拒绝！ */
    {
        RET_CODE rc = treelock_lock(tl, 30, TREELOCK_S);
        printf("  Lock records(S) without ancestors → %s (expected: protocol violation)\n",
               treelock_strerror(rc));
    }

    /* 正确的加锁顺序 */
    treelock_lock(tl, 10, TREELOCK_IS);   /* 根: 意向共享 */
    treelock_lock(tl, 20, TREELOCK_IS);   /* data: 意向共享 */
    treelock_lock(tl, 30, TREELOCK_S);    /* records: 共享读 */
    printf("  Correct locking: / IS → data IS → records S → OK\n");

    /* 清理 */
    treelock_unlock(tl, 30);
    treelock_unlock(tl, 20);
    treelock_unlock(tl, 10);
    printf("  Released all locks\n");

    treelock_destroy(tl);
}

/* =========================================================================
 * 示例 3: 无树结构 — 向后兼容
 * ========================================================================= */

static VOID example_no_tree_compat(VOID)
{
    treelock_t *tl;

    printf("\n========== 示例 3: 向后兼容（无树）==========\n");

    tl = treelock_create(NULL);

    /* 无树结构时，任意节点可直接加锁 — 完全向后兼容 */
    treelock_lock(tl, 42, TREELOCK_X);
    printf("  Locked node 42 (X) without tree → OK (backward compatible)\n");
    treelock_unlock(tl, 42);

    treelock_destroy(tl);
}

/* =========================================================================
 * 入口
 * ========================================================================= */

INT_32 main(VOID)
{
    printf("TreeLocks — Tree Structure Usage Examples\n");
    printf("==========================================\n");

    example_json_path_lock();
    example_programmatic();
    example_no_tree_compat();

    printf("\nAll examples completed.\n");
    return EXIT_SUCCESS;
}

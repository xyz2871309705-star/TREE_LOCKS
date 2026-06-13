/*
 * TreeLocks — 树结构管理使用示例
 *
 * 演示:
 *   1. 从嵌套 JSON 文件加载树 + 路径加锁/解锁
 *   2. 从扁平 JSON 文件加载树 + 验证与嵌套格式等价
 *   3. 编程式注册节点 + 协议自动校验
 *   4. 无树结构 — 向后兼容模式
 *
 * 版本: 0.2.0
 * 日期: 2026-06-13
 */

#include "treelock.h"
#include "treelock_tree.h"
#include "treelock_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* =========================================================================
 * 示例 1: 从嵌套 JSON 文件加载 + 路径加锁
 *
 * 使用 examples/filesystem_tree.json (嵌套格式)
 * ========================================================================= */

static VOID example_nested_json_file(VOID)
{
    treelock_t *tl;
    CSTR_PTR    tree_file = "filesystem_tree.json";

    printf("\n========== 示例 1: 嵌套 JSON 文件 + 路径加锁 ==========\n");

    tl = treelock_create(NULL);
    if (tl == NULL) {
        printf("  ERROR: failed to create client\n");
        return;
    }

    /* 从 JSON 文件加载树结构 */
    if (treelock_load_tree_from_file(tl, tree_file) != TREELOCK_OK) {
        printf("  ERROR: failed to load '%s'\n", tree_file);
        printf("  (请在工作目录或 build/ 目录下运行此示例)\n");
        treelock_destroy(tl);
        return;
    }
    printf("  Loaded '%s' — %s\n", tree_file,
           treelock_tree_loaded(tl) ? "OK" : "ERROR");

    /* ── 场景 A: 排他写 /home/alice ── */
    printf("\n  --- 场景 A: 排他写 /home/alice ---\n");

    if (treelock_lock_path(tl, "/home/alice", TREELOCK_X) == TREELOCK_OK) {
        printf("  Locked /home/alice (X) → 路径自动: / (IX) → /home (IX) → /home/alice (X)\n");

        treelock_unlock_path(tl, "/home/alice");
        printf("  Released all locks\n");
    }

    /* ── 场景 B: 共享读 /var/log ── */
    printf("\n  --- 场景 B: 共享读 /var/log ---\n");

    if (treelock_lock_path(tl, "/var/log", TREELOCK_S) == TREELOCK_OK) {
        printf("  Locked /var/log (S) → 路径自动: / (IS) → /var (IS) → /var/log (S)\n");

        treelock_unlock_path(tl, "/var/log");
        printf("  Released all locks\n");
    }

    /* ── 场景 C: 排他锁整个 /etc 子树 ── */
    printf("\n  --- 场景 C: 锁定整棵子树 /etc ---\n");

    if (treelock_lock_path(tl, "/etc", TREELOCK_X) == TREELOCK_OK) {
        printf("  Locked /etc (X) → 整个子树不可并发修改\n");
        printf("  其他客户端此时无法读写 /etc/config\n");

        treelock_unlock_path(tl, "/etc");
        printf("  Released /etc\n");
    }

    treelock_destroy(tl);
}

/* =========================================================================
 * 示例 2: 从扁平 JSON 文件加载 + 验证等价性
 *
 * 使用 examples/filesystem_tree_flat.json (扁平格式)
 * 演示与嵌套格式定义完全相同的树结构。
 * ========================================================================= */

static VOID example_flat_json_file(VOID)
{
    treelock_t *tl;
    CSTR_PTR    tree_file = "filesystem_tree_flat.json";
    treelock_node_id_t node_id;

    printf("\n========== 示例 2: 扁平 JSON 文件 + 等价验证 ==========\n");

    tl = treelock_create(NULL);
    if (tl == NULL) {
        printf("  ERROR: failed to create client\n");
        return;
    }

    /* 从扁平 JSON 文件加载 */
    if (treelock_load_tree_from_file(tl, tree_file) != TREELOCK_OK) {
        printf("  ERROR: failed to load '%s'\n", tree_file);
        printf("  (请在工作目录或 build/ 目录下运行此示例)\n");
        treelock_destroy(tl);
        return;
    }
    printf("  Loaded '%s' — OK\n", tree_file);
    printf("  格式: 扁平数组 (9 个节点)\n");

    /* 验证路径查找 — 与嵌套格式结果一致 */
    treelock_lookup_path(tl, "/home/alice", &node_id);
    printf("  lookup '/home/alice'  → id=%llu (应为 10)\n",
           (unsigned long long)node_id);

    treelock_lookup_path(tl, "/var/cache",  &node_id);
    printf("  lookup '/var/cache'   → id=%llu (应为 21)\n",
           (unsigned long long)node_id);

    treelock_lookup_path(tl, "/etc/config", &node_id);
    printf("  lookup '/etc/config'  → id=%llu (应为 30)\n",
           (unsigned long long)node_id);

    /* 路径加锁同样可用 */
    treelock_lock_path(tl, "/home/bob", TREELOCK_S);
    printf("  lock_path '/home/bob' (S) → OK\n");

    treelock_unlock_path(tl, "/home/bob");

    treelock_destroy(tl);
}

/* =========================================================================
 * 示例 3: 编程式注册 + 协议自动校验
 * ========================================================================= */

static VOID example_programmatic(VOID)
{
    treelock_t *tl;

    printf("\n========== 示例 3: 编程式注册 + 协议校验 ==========\n");

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
 * 示例 4: 无树结构 — 向后兼容
 * ========================================================================= */

static VOID example_no_tree_compat(VOID)
{
    treelock_t *tl;

    printf("\n========== 示例 4: 向后兼容（无树）==========\n");

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

    example_nested_json_file();
    example_flat_json_file();
    example_programmatic();
    example_no_tree_compat();

    printf("\nAll examples completed.\n");
    return EXIT_SUCCESS;
}

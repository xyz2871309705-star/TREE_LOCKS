/*
 * TreeLocks — 树结构管理单元测试
 *
 * 测试覆盖:
 *   1. JSON 解析（扁平/嵌套格式）
 *   2. 树校验（ID 唯一/单根/无环）
 *   3. 手动注册节点
 *   4. 协议自动校验（lock 时检查祖先锁）
 *   5. 路径加锁/解锁
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

/* 测试计数 */
static INT_32 g_passed = 0;
static INT_32 g_failed = 0;

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf("  FAIL: %s\n", msg); \
        g_failed++; \
    } else { \
        printf("  PASS: %s\n", msg); \
        g_passed++; \
    } \
} while (0)

/* =========================================================================
 * 测试 1: 手动注册节点
 * ========================================================================= */

static VOID test_register_node(VOID)
{
    treelock_t *tl;
    treelock_node_id_t parent_id;

    printf("\n--- Test 1: manual register_node ---\n");

    tl = treelock_create(NULL);
    TEST_ASSERT(tl != NULL, "create client");

    /* 注册根节点 */
    TEST_ASSERT(treelock_register_node(tl, 1, 0, "/") == TREELOCK_OK,
                "register root node");

    /* 注册子节点 */
    TEST_ASSERT(treelock_register_node(tl, 2, 1, "home") == TREELOCK_OK,
                "register child node");

    /* 重复 ID 应失败 */
    TEST_ASSERT(treelock_register_node(tl, 1, 2, "dup") == TREELOCK_ERR_INVAL,
                "duplicate node_id rejected");

    /* 查询父节点 */
    TEST_ASSERT(treelock_get_parent(tl, 1, &parent_id) == TREELOCK_OK &&
                parent_id == 0,
                "root parent is 0");

    TEST_ASSERT(treelock_get_parent(tl, 2, &parent_id) == TREELOCK_OK &&
                parent_id == 1,
                "child parent is correct");

    TEST_ASSERT(treelock_tree_loaded(tl) == TRUE, "tree is loaded");

    treelock_destroy(tl);
}

/* =========================================================================
 * 测试 2: JSON 字符串加载（扁平格式）
 * ========================================================================= */

static VOID test_json_flat(VOID)
{
    treelock_t *tl;
    CSTR_PTR json =
        "{"
        "  \"nodes\": ["
        "    { \"id\": 10, \"label\": \"root\", \"parent\": 0 },"
        "    { \"id\": 20, \"label\": \"a\",    \"parent\": 10 },"
        "    { \"id\": 30, \"label\": \"b\",    \"parent\": 10 },"
        "    { \"id\": 40, \"label\": \"c\",    \"parent\": 20 }"
        "  ]"
        "}";
    treelock_node_id_t parent_id;
    treelock_node_id_t node_id;

    printf("\n--- Test 2: load JSON (flat) ---\n");

    tl = treelock_create(NULL);
    TEST_ASSERT(tl != NULL, "create client");

    TEST_ASSERT(treelock_load_tree_from_string(tl, json) == TREELOCK_OK,
                "load flat JSON");

    /* 验证树结构 */
    TEST_ASSERT(treelock_get_parent(tl, 10, &parent_id) == TREELOCK_OK &&
                parent_id == 0, "root parent=0");
    TEST_ASSERT(treelock_get_parent(tl, 20, &parent_id) == TREELOCK_OK &&
                parent_id == 10, "a parent=root");
    TEST_ASSERT(treelock_get_parent(tl, 40, &parent_id) == TREELOCK_OK &&
                parent_id == 20, "c parent=a");

    /* 路径查找 */
    TEST_ASSERT(treelock_lookup_path(tl, "/root", &node_id) == TREELOCK_OK &&
                node_id == 10, "lookup /root");
    TEST_ASSERT(treelock_lookup_path(tl, "/root/a", &node_id) == TREELOCK_OK &&
                node_id == 20, "lookup /root/a");
    TEST_ASSERT(treelock_lookup_path(tl, "/root/a/c", &node_id) == TREELOCK_OK &&
                node_id == 40, "lookup /root/a/c");

    /* 无效路径 */
    TEST_ASSERT(treelock_lookup_path(tl, "/root/nonexist", &node_id) == TREELOCK_ERR_INVAL,
                "lookup invalid path fails");

    treelock_destroy(tl);
}

/* =========================================================================
 * 测试 3: JSON 嵌套格式
 * ========================================================================= */

static VOID test_json_nested(VOID)
{
    treelock_t *tl;
    CSTR_PTR json =
        "{"
        "  \"tree\": {"
        "    \"id\": 100,"
        "    \"label\": \"/\","
        "    \"children\": ["
        "      {"
        "        \"id\": 200,"
        "        \"label\": \"dir\","
        "        \"children\": ["
        "          { \"id\": 300, \"label\": \"file1\" },"
        "          { \"id\": 400, \"label\": \"file2\" }"
        "        ]"
        "      }"
        "    ]"
        "  }"
        "}";
    treelock_node_id_t node_id;

    printf("\n--- Test 3: load JSON (nested) ---\n");

    tl = treelock_create(NULL);
    TEST_ASSERT(tl != NULL, "create client");

    TEST_ASSERT(treelock_load_tree_from_string(tl, json) == TREELOCK_OK,
                "load nested JSON");

    TEST_ASSERT(treelock_lookup_path(tl, "/", &node_id) == TREELOCK_OK &&
                node_id == 100, "lookup /");
    TEST_ASSERT(treelock_lookup_path(tl, "/dir", &node_id) == TREELOCK_OK &&
                node_id == 200, "lookup /dir");
    TEST_ASSERT(treelock_lookup_path(tl, "/dir/file1", &node_id) == TREELOCK_OK &&
                node_id == 300, "lookup /dir/file1");

    treelock_destroy(tl);
}

/* =========================================================================
 * 测试 4: 校验 — 重复 ID
 * ========================================================================= */

static VOID test_validate_duplicate_id(VOID)
{
    treelock_t *tl;
    CSTR_PTR json =
        "{ \"nodes\": ["
        "  { \"id\": 1, \"parent\": 0 },"
        "  { \"id\": 1, \"parent\": 0 }"
        "]}";

    printf("\n--- Test 4: validate — duplicate ID ---\n");

    tl = treelock_create(NULL);
    TEST_ASSERT(tl != NULL, "create client");

    TEST_ASSERT(treelock_load_tree_from_string(tl, json) == TREELOCK_ERR_INVAL,
                "duplicate ID rejected");

    treelock_destroy(tl);
}

/* =========================================================================
 * 测试 5: 校验 — 多根节点
 * ========================================================================= */

static VOID test_validate_multi_root(VOID)
{
    treelock_t *tl;
    CSTR_PTR json =
        "{ \"nodes\": ["
        "  { \"id\": 1, \"parent\": 0 },"
        "  { \"id\": 2, \"parent\": 0 }"
        "]}";

    printf("\n--- Test 5: validate — multi root ---\n");

    tl = treelock_create(NULL);
    TEST_ASSERT(tl != NULL, "create client");

    TEST_ASSERT(treelock_load_tree_from_string(tl, json) == TREELOCK_ERR_INVAL,
                "multi root rejected");

    treelock_destroy(tl);
}

/* =========================================================================
 * 测试 6: 校验 — 环路
 * ========================================================================= */

static VOID test_validate_cycle(VOID)
{
    treelock_t *tl;
    /* A(1) → B(2) → C(3) → A(1) */
    CSTR_PTR json =
        "{ \"nodes\": ["
        "  { \"id\": 1, \"parent\": 3 },"
        "  { \"id\": 2, \"parent\": 1 },"
        "  { \"id\": 3, \"parent\": 2 }"
        "]}";

    printf("\n--- Test 6: validate — cycle ---\n");

    tl = treelock_create(NULL);
    TEST_ASSERT(tl != NULL, "create client");

    TEST_ASSERT(treelock_load_tree_from_string(tl, json) == TREELOCK_ERR_INVAL,
                "cycle detected");

    treelock_destroy(tl);
}

/* =========================================================================
 * 测试 7: 协议自动校验 — 未锁祖先则拒绝
 * ========================================================================= */

static VOID test_protocol_enforced(VOID)
{
    treelock_t *tl;
    RET_CODE rc;

    printf("\n--- Test 7: protocol auto-enforcement ---\n");

    tl = treelock_create(NULL);
    TEST_ASSERT(tl != NULL, "create client");

    /* 注册树: root(1) → child(2) */
    TEST_ASSERT(treelock_register_node(tl, 1, 0, "/") == TREELOCK_OK,
                "register root");
    TEST_ASSERT(treelock_register_node(tl, 2, 1, "child") == TREELOCK_OK,
                "register child");

    /* 未锁 root，直接锁 child 应被协议拒绝 */
    rc = treelock_lock(tl, 2, TREELOCK_S);
    TEST_ASSERT(rc == TREELOCK_ERR_PROTOCOL,
                "lock child without parent fails");

    /* 先锁 root IS，再锁 child S 应成功 */
    TEST_ASSERT(treelock_lock(tl, 1, TREELOCK_IS) == TREELOCK_OK,
                "lock root IS");
    rc = treelock_lock(tl, 2, TREELOCK_S);
    TEST_ASSERT(rc == TREELOCK_OK,
                "lock child S after root IS succeeds");

    /* 清理 */
    treelock_unlock(tl, 2);
    treelock_unlock(tl, 1);

    /* 测试 X 锁：需要父节点先有 IX */
    rc = treelock_lock(tl, 2, TREELOCK_X);
    TEST_ASSERT(rc == TREELOCK_ERR_PROTOCOL,
                "lock child X without parent IX fails");

    TEST_ASSERT(treelock_lock(tl, 1, TREELOCK_IX) == TREELOCK_OK,
                "lock root IX");
    TEST_ASSERT(treelock_lock(tl, 2, TREELOCK_X) == TREELOCK_OK,
                "lock child X after root IX succeeds");

    treelock_unlock(tl, 2);
    treelock_unlock(tl, 1);

    treelock_destroy(tl);
}

/* =========================================================================
 * 测试 8: 路径加锁
 * ========================================================================= */

static VOID test_lock_path(VOID)
{
    treelock_t *tl;
    treelock_mode_t mode;

    printf("\n--- Test 8: lock_path / unlock_path ---\n");

    tl = treelock_create(NULL);
    TEST_ASSERT(tl != NULL, "create client");

    /* 注册树: / (1) → home (2) → alice (3) */
    TEST_ASSERT(treelock_register_node(tl, 1, 0, "/") == TREELOCK_OK,
                "register /");
    TEST_ASSERT(treelock_register_node(tl, 2, 1, "home") == TREELOCK_OK,
                "register /home");
    TEST_ASSERT(treelock_register_node(tl, 3, 2, "alice") == TREELOCK_OK,
                "register /home/alice");

    /* 路径加锁 */
    TEST_ASSERT(treelock_lock_path(tl, "/home/alice", TREELOCK_X) == TREELOCK_OK,
                "lock_path /home/alice X");

    /* 验证所有路径节点都已加锁 */
    mode = treelock_get_mode(tl, 1);
    TEST_ASSERT(mode == TREELOCK_IX,
                "root holds IX (auto-selected for X child)");

    mode = treelock_get_mode(tl, 2);
    TEST_ASSERT(mode == TREELOCK_IX,
                "/home holds IX (auto-selected for X child)");

    mode = treelock_get_mode(tl, 3);
    TEST_ASSERT(mode == TREELOCK_X,
                "/home/alice holds X");

    /* 路径解锁 */
    TEST_ASSERT(treelock_unlock_path(tl, "/home/alice") == TREELOCK_OK,
                "unlock_path /home/alice");

    /* 验证全部释放 */
    TEST_ASSERT(treelock_get_mode(tl, 1) == TREELOCK_NL, "root released");
    TEST_ASSERT(treelock_get_mode(tl, 2) == TREELOCK_NL, "/home released");
    TEST_ASSERT(treelock_get_mode(tl, 3) == TREELOCK_NL, "/home/alice released");

    treelock_destroy(tl);
}

/* =========================================================================
 * 测试 9: 向后兼容 — 无树时协议校验跳过
 * ========================================================================= */

static VOID test_backward_compat(VOID)
{
    treelock_t *tl;
    RET_CODE rc;

    printf("\n--- Test 9: backward compatibility (no tree) ---\n");

    tl = treelock_create(NULL);
    TEST_ASSERT(tl != NULL, "create client");

    TEST_ASSERT(treelock_tree_loaded(tl) == FALSE, "tree not loaded");

    /* 无树时任意节点可直接加锁 */
    rc = treelock_lock(tl, 999, TREELOCK_X);
    TEST_ASSERT(rc == TREELOCK_OK,
                "lock without tree works (backward compat)");

    treelock_unlock(tl, 999);
    treelock_destroy(tl);
}

/* =========================================================================
 * 入口
 * ========================================================================= */

INT_32 main(VOID)
{
    printf("TreeLocks Tree Structure Tests\n");
    printf("==============================\n");

    test_register_node();
    test_json_flat();
    test_json_nested();
    test_validate_duplicate_id();
    test_validate_multi_root();
    test_validate_cycle();
    test_protocol_enforced();
    test_lock_path();
    test_backward_compat();

    printf("\n==============================\n");
    printf("Results: %d passed, %d failed\n", g_passed, g_failed);

    return (g_failed > 0) ? EXIT_FAILURE : EXIT_SUCCESS;
}

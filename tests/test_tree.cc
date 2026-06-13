/*
 * TreeLocks — 树结构管理单元测试 (GTest)
 *
 * 测试覆盖:
 *   1. JSON 解析（扁平/嵌套格式）
 *   2. 树校验（ID 唯一/单根/无环）
 *   3. 手动注册节点
 *   4. 协议自动校验（lock 时检查祖先锁）
 *   5. 路径加锁/解锁
 *
 * 版本: 0.2.0
 * 日期: 2026-06-13
 */

#include <gtest/gtest.h>
#include <cstdio>
#include <cstring>

extern "C" {
#include "treelock.h"
#include "treelock_tree.h"
#include "treelock_log.h"
}

/* =========================================================================
 * Test 1: manual register_node
 * ========================================================================= */

TEST(TreeTest, ManualRegisterNode)
{
    treelock_t *tl = treelock_create(nullptr);
    ASSERT_NE(tl, nullptr);

    /* register root */
    EXPECT_EQ(treelock_register_node(tl, 1, 0, "/"), TREELOCK_OK);

    /* register child */
    EXPECT_EQ(treelock_register_node(tl, 2, 1, "home"), TREELOCK_OK);

    /* duplicate ID rejected */
    EXPECT_EQ(treelock_register_node(tl, 1, 2, "dup"), TREELOCK_ERR_INVAL);

    /* query parent */
    treelock_node_id_t parent_id;
    EXPECT_EQ(treelock_get_parent(tl, 1, &parent_id), TREELOCK_OK);
    EXPECT_EQ(parent_id, 0u);

    EXPECT_EQ(treelock_get_parent(tl, 2, &parent_id), TREELOCK_OK);
    EXPECT_EQ(parent_id, 1u);

    EXPECT_TRUE(treelock_tree_loaded(tl));

    treelock_destroy(tl);
}

/* =========================================================================
 * Test 2: load flat JSON
 * ========================================================================= */

TEST(TreeTest, FlatJson)
{
    const char *json =
        "{"
        "  \"nodes\": ["
        "    { \"id\": 10, \"label\": \"root\", \"parent\": 0 },"
        "    { \"id\": 20, \"label\": \"a\",    \"parent\": 10 },"
        "    { \"id\": 30, \"label\": \"b\",    \"parent\": 10 },"
        "    { \"id\": 40, \"label\": \"c\",    \"parent\": 20 }"
        "  ]"
        "}";

    treelock_t *tl = treelock_create(nullptr);
    ASSERT_NE(tl, nullptr);
    ASSERT_EQ(treelock_load_tree_from_string(tl, json), TREELOCK_OK);

    /* verify tree structure */
    treelock_node_id_t parent_id;
    EXPECT_EQ(treelock_get_parent(tl, 10, &parent_id), TREELOCK_OK);
    EXPECT_EQ(parent_id, 0u);
    EXPECT_EQ(treelock_get_parent(tl, 20, &parent_id), TREELOCK_OK);
    EXPECT_EQ(parent_id, 10u);
    EXPECT_EQ(treelock_get_parent(tl, 40, &parent_id), TREELOCK_OK);
    EXPECT_EQ(parent_id, 20u);

    /* path lookup */
    treelock_node_id_t node_id;
    EXPECT_EQ(treelock_lookup_path(tl, "/root", &node_id), TREELOCK_OK);
    EXPECT_EQ(node_id, 10u);
    EXPECT_EQ(treelock_lookup_path(tl, "/root/a", &node_id), TREELOCK_OK);
    EXPECT_EQ(node_id, 20u);
    EXPECT_EQ(treelock_lookup_path(tl, "/root/a/c", &node_id), TREELOCK_OK);
    EXPECT_EQ(node_id, 40u);

    /* invalid path */
    EXPECT_EQ(treelock_lookup_path(tl, "/root/nonexist", &node_id), TREELOCK_ERR_INVAL);

    treelock_destroy(tl);
}

/* =========================================================================
 * Test 3: nested JSON
 * ========================================================================= */

TEST(TreeTest, NestedJson)
{
    const char *json =
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

    treelock_t *tl = treelock_create(nullptr);
    ASSERT_NE(tl, nullptr);
    ASSERT_EQ(treelock_load_tree_from_string(tl, json), TREELOCK_OK);

    treelock_node_id_t node_id;
    EXPECT_EQ(treelock_lookup_path(tl, "/", &node_id), TREELOCK_OK);
    EXPECT_EQ(node_id, 100u);
    EXPECT_EQ(treelock_lookup_path(tl, "/dir", &node_id), TREELOCK_OK);
    EXPECT_EQ(node_id, 200u);
    EXPECT_EQ(treelock_lookup_path(tl, "/dir/file1", &node_id), TREELOCK_OK);
    EXPECT_EQ(node_id, 300u);

    treelock_destroy(tl);
}

/* =========================================================================
 * Test 4: validation — duplicate ID
 * ========================================================================= */

TEST(TreeTest, ValidateDuplicateId)
{
    const char *json =
        "{ \"nodes\": ["
        "  { \"id\": 1, \"parent\": 0 },"
        "  { \"id\": 1, \"parent\": 0 }"
        "]}";

    treelock_t *tl = treelock_create(nullptr);
    ASSERT_NE(tl, nullptr);
    EXPECT_EQ(treelock_load_tree_from_string(tl, json), TREELOCK_ERR_INVAL);
    treelock_destroy(tl);
}

/* =========================================================================
 * Test 5: validation — multi root
 * ========================================================================= */

TEST(TreeTest, ValidateMultiRoot)
{
    const char *json =
        "{ \"nodes\": ["
        "  { \"id\": 1, \"parent\": 0 },"
        "  { \"id\": 2, \"parent\": 0 }"
        "]}";

    treelock_t *tl = treelock_create(nullptr);
    ASSERT_NE(tl, nullptr);
    EXPECT_EQ(treelock_load_tree_from_string(tl, json), TREELOCK_ERR_INVAL);
    treelock_destroy(tl);
}

/* =========================================================================
 * Test 6: validation — cycle
 * ========================================================================= */

TEST(TreeTest, ValidateCycle)
{
    /* A(1) → B(2) → C(3) → A(1) */
    const char *json =
        "{ \"nodes\": ["
        "  { \"id\": 1, \"parent\": 3 },"
        "  { \"id\": 2, \"parent\": 1 },"
        "  { \"id\": 3, \"parent\": 2 }"
        "]}";

    treelock_t *tl = treelock_create(nullptr);
    ASSERT_NE(tl, nullptr);
    EXPECT_EQ(treelock_load_tree_from_string(tl, json), TREELOCK_ERR_INVAL);
    treelock_destroy(tl);
}

/* =========================================================================
 * Test 7: protocol auto-enforcement
 * ========================================================================= */

TEST(TreeTest, ProtocolEnforcement)
{
    treelock_t *tl = treelock_create(nullptr);
    ASSERT_NE(tl, nullptr);

    /* register tree: root(1) → child(2) */
    ASSERT_EQ(treelock_register_node(tl, 1, 0, "/"), TREELOCK_OK);
    ASSERT_EQ(treelock_register_node(tl, 2, 1, "child"), TREELOCK_OK);

    /* lock child without parent → rejected */
    EXPECT_EQ(treelock_lock(tl, 2, TREELOCK_S), TREELOCK_ERR_PROTOCOL);

    /* lock root IS → then child S → OK */
    EXPECT_EQ(treelock_lock(tl, 1, TREELOCK_IS), TREELOCK_OK);
    EXPECT_EQ(treelock_lock(tl, 2, TREELOCK_S), TREELOCK_OK);
    treelock_unlock(tl, 2);
    treelock_unlock(tl, 1);

    /* lock child X without parent IX → rejected */
    EXPECT_EQ(treelock_lock(tl, 2, TREELOCK_X), TREELOCK_ERR_PROTOCOL);

    /* lock root IX → then child X → OK */
    EXPECT_EQ(treelock_lock(tl, 1, TREELOCK_IX), TREELOCK_OK);
    EXPECT_EQ(treelock_lock(tl, 2, TREELOCK_X), TREELOCK_OK);
    treelock_unlock(tl, 2);
    treelock_unlock(tl, 1);

    treelock_destroy(tl);
}

/* =========================================================================
 * Test 8: lock_path / unlock_path
 * ========================================================================= */

TEST(TreeTest, LockPathAndUnlockPath)
{
    treelock_t *tl = treelock_create(nullptr);
    ASSERT_NE(tl, nullptr);

    /* register tree: / (1) → home (2) → alice (3) */
    ASSERT_EQ(treelock_register_node(tl, 1, 0, "/"), TREELOCK_OK);
    ASSERT_EQ(treelock_register_node(tl, 2, 1, "home"), TREELOCK_OK);
    ASSERT_EQ(treelock_register_node(tl, 3, 2, "alice"), TREELOCK_OK);

    /* lock_path */
    EXPECT_EQ(treelock_lock_path(tl, "/home/alice", TREELOCK_X), TREELOCK_OK);

    /* verify all path nodes locked */
    EXPECT_EQ(treelock_get_mode(tl, 1), TREELOCK_IX);
    EXPECT_EQ(treelock_get_mode(tl, 2), TREELOCK_IX);
    EXPECT_EQ(treelock_get_mode(tl, 3), TREELOCK_X);

    /* unlock_path */
    EXPECT_EQ(treelock_unlock_path(tl, "/home/alice"), TREELOCK_OK);

    /* verify all released */
    EXPECT_EQ(treelock_get_mode(tl, 1), TREELOCK_NL);
    EXPECT_EQ(treelock_get_mode(tl, 2), TREELOCK_NL);
    EXPECT_EQ(treelock_get_mode(tl, 3), TREELOCK_NL);

    treelock_destroy(tl);
}

/* =========================================================================
 * Test 9: backward compatibility (no tree)
 * ========================================================================= */

TEST(TreeTest, BackwardCompatibility)
{
    treelock_t *tl = treelock_create(nullptr);
    ASSERT_NE(tl, nullptr);

    EXPECT_FALSE(treelock_tree_loaded(tl));

    /* lock any node without tree → OK (backward compat) */
    EXPECT_EQ(treelock_lock(tl, 999, TREELOCK_X), TREELOCK_OK);
    treelock_unlock(tl, 999);

    treelock_destroy(tl);
}

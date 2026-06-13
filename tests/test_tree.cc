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

/* =========================================================================
 * Test 10: register_node 边界条件
 * ========================================================================= */

TEST(TreeTest, RegisterNodeEdgeCases)
{
    treelock_t *tl = treelock_create(nullptr);
    ASSERT_NE(tl, nullptr);

    /* node_id = 0 非法 */
    EXPECT_EQ(treelock_register_node(tl, 0, 0, nullptr), TREELOCK_ERR_INVAL);

    /* 正常注册根节点 */
    EXPECT_EQ(treelock_register_node(tl, 1, 0, "/"), TREELOCK_OK);

    /* 重复注册根节点应失败 */
    EXPECT_EQ(treelock_register_node(tl, 2, 0, "root2"), TREELOCK_ERR_INVAL);

    /* 父节点不存在 → 失败 */
    EXPECT_EQ(treelock_register_node(tl, 10, 99, "orphan"), TREELOCK_ERR_INVAL);

    /* 正常注册子节点 */
    EXPECT_EQ(treelock_register_node(tl, 3, 1, "child"), TREELOCK_OK);

    /* 重复 ID → 失败 */
    EXPECT_EQ(treelock_register_node(tl, 1, 3, "dup"), TREELOCK_ERR_INVAL);

    treelock_destroy(tl);
}

/* =========================================================================
 * Test 11: 非法 JSON
 * ========================================================================= */

TEST(TreeTest, InvalidJsonSyntax)
{
    treelock_t *tl = treelock_create(nullptr);
    ASSERT_NE(tl, nullptr);

    /* 裸数字 */
    EXPECT_EQ(treelock_load_tree_from_string(tl, "123"), TREELOCK_ERR_INVAL);

    /* 缺大括号 */
    EXPECT_EQ(treelock_load_tree_from_string(tl, "\"tree\":{}"), TREELOCK_ERR_INVAL);

    /* 空对象 */
    EXPECT_EQ(treelock_load_tree_from_string(tl, "{}"), TREELOCK_ERR_INVAL);

    /* 空字符串 */
    EXPECT_EQ(treelock_load_tree_from_string(tl, ""), TREELOCK_ERR_INVAL);

    /* NULL 指针 */
    EXPECT_EQ(treelock_load_tree_from_string(tl, nullptr), TREELOCK_ERR_INVAL);

    treelock_destroy(tl);
}

TEST(TreeTest, InvalidJsonEmptyNodes)
{
    treelock_t *tl = treelock_create(nullptr);
    ASSERT_NE(tl, nullptr);

    /* 空节点数组 → 校验失败（无根节点） */
    EXPECT_EQ(treelock_load_tree_from_string(tl, "{\"nodes\":[]}"), TREELOCK_ERR_INVAL);

    treelock_destroy(tl);
}

TEST(TreeTest, InvalidJsonMissingId)
{
    treelock_t *tl = treelock_create(nullptr);
    ASSERT_NE(tl, nullptr);

    /* 节点缺少必须的 id 字段 */
    const char *json = "{ \"nodes\": [ { \"parent\": 0 } ] }";
    EXPECT_EQ(treelock_load_tree_from_string(tl, json), TREELOCK_ERR_INVAL);

    treelock_destroy(tl);
}

TEST(TreeTest, InvalidJsonSelfParent)
{
    treelock_t *tl = treelock_create(nullptr);
    ASSERT_NE(tl, nullptr);

    /* 自引用：parent == id → 应检测为环 */
    const char *json = "{ \"nodes\": [ { \"id\": 1, \"parent\": 1 } ] }";
    EXPECT_EQ(treelock_load_tree_from_string(tl, json), TREELOCK_ERR_INVAL);

    treelock_destroy(tl);
}

/* =========================================================================
 * Test 12: 树重加载
 * ========================================================================= */

TEST(TreeTest, ReloadTree)
{
    treelock_t *tl = treelock_create(nullptr);
    ASSERT_NE(tl, nullptr);

    /* 加载第一个树 */
    const char *tree1 =
        "{ \"nodes\": ["
        "  { \"id\": 1, \"label\": \"root\", \"parent\": 0 },"
        "  { \"id\": 2, \"label\": \"a\",    \"parent\": 1 }"
        "]}";
    ASSERT_EQ(treelock_load_tree_from_string(tl, tree1), TREELOCK_OK);

    treelock_node_id_t node_id;
    EXPECT_EQ(treelock_lookup_path(tl, "/root/a", &node_id), TREELOCK_OK);
    EXPECT_EQ(node_id, 2u);

    /* 重加载新树（旧树应被自动清理） */
    const char *tree2 =
        "{ \"nodes\": ["
        "  { \"id\": 100, \"label\": \"newroot\", \"parent\": 0 },"
        "  { \"id\": 200, \"label\": \"b\",       \"parent\": 100 }"
        "]}";
    ASSERT_EQ(treelock_load_tree_from_string(tl, tree2), TREELOCK_OK);

    /* 旧路径应失效 */
    EXPECT_EQ(treelock_lookup_path(tl, "/root/a", &node_id), TREELOCK_ERR_INVAL);

    /* 新路径应有效 */
    EXPECT_EQ(treelock_lookup_path(tl, "/newroot/b", &node_id), TREELOCK_OK);
    EXPECT_EQ(node_id, 200u);

    EXPECT_TRUE(treelock_tree_loaded(tl));

    treelock_destroy(tl);
}

/* =========================================================================
 * Test 13: lock_path — IS/S 目标模式
 * ========================================================================= */

TEST(TreeTest, LockPathISMode)
{
    treelock_t *tl = treelock_create(nullptr);
    ASSERT_NE(tl, nullptr);

    /* / (1) → home (2) → data (3) */
    ASSERT_EQ(treelock_register_node(tl, 1, 0, "/"), TREELOCK_OK);
    ASSERT_EQ(treelock_register_node(tl, 2, 1, "home"), TREELOCK_OK);
    ASSERT_EQ(treelock_register_node(tl, 3, 2, "data"), TREELOCK_OK);

    /* lock_path with S → 祖先应用 IS（非 IX） */
    EXPECT_EQ(treelock_lock_path(tl, "/home/data", TREELOCK_S), TREELOCK_OK);

    /* 根节点应为 IS */
    EXPECT_EQ(treelock_get_mode(tl, 1), TREELOCK_IS);
    /* 中间节点应为 IS */
    EXPECT_EQ(treelock_get_mode(tl, 2), TREELOCK_IS);
    /* 目标节点应为 S */
    EXPECT_EQ(treelock_get_mode(tl, 3), TREELOCK_S);

    treelock_unlock_path(tl, "/home/data");

    /* 全部释放 */
    EXPECT_EQ(treelock_get_mode(tl, 1), TREELOCK_NL);
    EXPECT_EQ(treelock_get_mode(tl, 2), TREELOCK_NL);
    EXPECT_EQ(treelock_get_mode(tl, 3), TREELOCK_NL);

    treelock_destroy(tl);
}

/* =========================================================================
 * Test 14: lock_path / unlock_path 边界
 * ========================================================================= */

TEST(TreeTest, LockPathInvalidPath)
{
    treelock_t *tl = treelock_create(nullptr);
    ASSERT_NE(tl, nullptr);

    /* 树未加载时 lock_path 应失败 */
    EXPECT_EQ(treelock_lock_path(tl, "/test", TREELOCK_S), TREELOCK_ERR_INVAL);

    /* 注册树 */
    ASSERT_EQ(treelock_register_node(tl, 1, 0, "/"), TREELOCK_OK);
    ASSERT_EQ(treelock_register_node(tl, 2, 1, "dir"), TREELOCK_OK);

    /* 空路径 */
    EXPECT_EQ(treelock_lock_path(tl, "", TREELOCK_S), TREELOCK_ERR_INVAL);

    /* NULL 路径 */
    EXPECT_EQ(treelock_lock_path(tl, nullptr, TREELOCK_S), TREELOCK_ERR_INVAL);

    /* 不存在路径 */
    EXPECT_EQ(treelock_lock_path(tl, "/nonexist", TREELOCK_S), TREELOCK_ERR_INVAL);

    /* 非法 mode (NL) */
    EXPECT_EQ(treelock_lock_path(tl, "/dir", TREELOCK_NL), TREELOCK_ERR_INVAL);

    /* 合法路径应成功 */
    EXPECT_EQ(treelock_lock_path(tl, "/dir", TREELOCK_IX), TREELOCK_OK);
    treelock_unlock_path(tl, "/dir");

    treelock_destroy(tl);
}

TEST(TreeTest, UnlockPathEdgeCases)
{
    treelock_t *tl = treelock_create(nullptr);
    ASSERT_NE(tl, nullptr);

    /* 树未加载时 unlock_path 应失败 */
    EXPECT_EQ(treelock_unlock_path(tl, "/test"), TREELOCK_ERR_INVAL);

    /* 注册树 */
    ASSERT_EQ(treelock_register_node(tl, 1, 0, "/"), TREELOCK_OK);

    /* 空路径 */
    EXPECT_EQ(treelock_unlock_path(tl, ""), TREELOCK_ERR_INVAL);

    /* NULL 路径 */
    EXPECT_EQ(treelock_unlock_path(tl, nullptr), TREELOCK_ERR_INVAL);

    /* 未持有锁的路径 — 应返回错误（但不崩溃） */
    RET_CODE rc = treelock_unlock_path(tl, "/");
    EXPECT_NE(rc, TREELOCK_OK);

    treelock_destroy(tl);
}

TEST(TreeTest, LockPathRootOnly)
{
    treelock_t *tl = treelock_create(nullptr);
    ASSERT_NE(tl, nullptr);

    /* 注册单个根节点，label="/" */
    ASSERT_EQ(treelock_register_node(tl, 1, 0, "/"), TREELOCK_OK);

    /* lock_path("/") — 仅锁根节点 */
    EXPECT_EQ(treelock_lock_path(tl, "/", TREELOCK_X), TREELOCK_OK);
    EXPECT_EQ(treelock_get_mode(tl, 1), TREELOCK_X);

    EXPECT_EQ(treelock_unlock_path(tl, "/"), TREELOCK_OK);
    EXPECT_EQ(treelock_get_mode(tl, 1), TREELOCK_NL);

    treelock_destroy(tl);
}

/* =========================================================================
 * Test 15: ancestor_mode_for 全模式
 * ========================================================================= */

/* treelock_ancestor_mode_for 声明（内部函数，手动声明避免引入 tree_internal.h 的 cJSON 依赖） */
extern "C" {
treelock_mode_t treelock_ancestor_mode_for(treelock_mode_t child_mode);
}

TEST(TreeTest, AncestorModeForAllModes)
{
    /* IS / S → 祖先用 IS */
    EXPECT_EQ(treelock_ancestor_mode_for(TREELOCK_IS), TREELOCK_IS);
    EXPECT_EQ(treelock_ancestor_mode_for(TREELOCK_S),  TREELOCK_IS);

    /* IX / SIX / X → 祖先用 IX */
    EXPECT_EQ(treelock_ancestor_mode_for(TREELOCK_IX),  TREELOCK_IX);
    EXPECT_EQ(treelock_ancestor_mode_for(TREELOCK_SIX), TREELOCK_IX);
    EXPECT_EQ(treelock_ancestor_mode_for(TREELOCK_X),   TREELOCK_IX);

    /* NL → 返回 NL */
    EXPECT_EQ(treelock_ancestor_mode_for(TREELOCK_NL), TREELOCK_NL);

    /* 非法值 → NL */
    EXPECT_EQ(treelock_ancestor_mode_for((treelock_mode_t)99), TREELOCK_NL);
}

/* =========================================================================
 * Test 16: lookup_path 边界
 * ========================================================================= */

TEST(TreeTest, LookupPathEdgeCases)
{
    treelock_t *tl = treelock_create(nullptr);
    ASSERT_NE(tl, nullptr);

    /* 树未加载 */
    treelock_node_id_t node_id;
    EXPECT_EQ(treelock_lookup_path(tl, "/test", &node_id), TREELOCK_ERR_INVAL);

    /* 注册树 */
    ASSERT_EQ(treelock_register_node(tl, 1, 0, "root"), TREELOCK_OK);
    ASSERT_EQ(treelock_register_node(tl, 2, 1, "a"), TREELOCK_OK);

    /* NULL path */
    EXPECT_EQ(treelock_lookup_path(tl, nullptr, &node_id), TREELOCK_ERR_INVAL);

    /* NULL output pointer */
    EXPECT_EQ(treelock_lookup_path(tl, "/root", nullptr), TREELOCK_ERR_INVAL);

    /* 只查根 */
    EXPECT_EQ(treelock_lookup_path(tl, "/root", &node_id), TREELOCK_OK);
    EXPECT_EQ(node_id, 1u);

    treelock_destroy(tl);
}

/* =========================================================================
 * Test 17: get_parent 边界
 * ========================================================================= */

TEST(TreeTest, GetParentEdgeCases)
{
    treelock_t *tl = treelock_create(nullptr);
    ASSERT_NE(tl, nullptr);

    /* NULL parent_id */
    EXPECT_EQ(treelock_get_parent(tl, 1, nullptr), TREELOCK_ERR_INVAL);

    /* 树未加载 */
    treelock_node_id_t parent_id;
    EXPECT_EQ(treelock_get_parent(tl, 1, &parent_id), TREELOCK_ERR_INVAL);

    /* 注册树 */
    ASSERT_EQ(treelock_register_node(tl, 1, 0, "/"), TREELOCK_OK);

    /* 不存在的节点 */
    EXPECT_EQ(treelock_get_parent(tl, 999, &parent_id), TREELOCK_ERR_INVAL);

    /* 根节点 parent=0 */
    EXPECT_EQ(treelock_get_parent(tl, 1, &parent_id), TREELOCK_OK);
    EXPECT_EQ(parent_id, 0u);

    treelock_destroy(tl);
}

/* =========================================================================
 * Test 18: 单节点树 (仅根节点)
 * ========================================================================= */

TEST(TreeTest, SingleNodeTree)
{
    treelock_t *tl = treelock_create(nullptr);
    ASSERT_NE(tl, nullptr);

    ASSERT_EQ(treelock_register_node(tl, 1, 0, "/"), TREELOCK_OK);

    EXPECT_TRUE(treelock_tree_loaded(tl));

    treelock_node_id_t parent_id;
    EXPECT_EQ(treelock_get_parent(tl, 1, &parent_id), TREELOCK_OK);
    EXPECT_EQ(parent_id, 0u);

    /* lock root */
    EXPECT_EQ(treelock_lock(tl, 1, TREELOCK_X), TREELOCK_OK);
    EXPECT_EQ(treelock_get_mode(tl, 1), TREELOCK_X);
    treelock_unlock(tl, 1);

    treelock_destroy(tl);
}

/* =========================================================================
 * Test 19: destroy 后 tree_loaded 检查
 * ========================================================================= */

TEST(TreeTest, TreeNotLoadedAfterDestroy)
{
    treelock_t *tl = treelock_create(nullptr);
    ASSERT_NE(tl, nullptr);

    ASSERT_EQ(treelock_register_node(tl, 1, 0, "/"), TREELOCK_OK);
    EXPECT_TRUE(treelock_tree_loaded(tl));

    treelock_destroy(tl);

    /* destroy 后 tl 为野指针不应再使用。
     * 仅验证 NULL 参数返回 FALSE */
    EXPECT_FALSE(treelock_tree_loaded(nullptr));
}

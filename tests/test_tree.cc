/*
 * TreeLocks — 树结构管理单元测试 (GTest)
 *
 * 测试覆盖:
 *   1. JSON 解析（扁平/嵌套格式）       — tree_json.c
 *   2. 树校验（ID 唯一/单根/无环）        — tree_validate.c
 *   3. 手动注册节点                     — tree_core.c + tree_api.c
 *   4. 协议自动校验（lock 时检查祖先锁）  — protocol.c + tree_api.c
 *   5. 路径加锁/解锁                     — tree_path.c + tree_api.c
 *
 * 被测源文件:
 *   modules/treelock_tree/src/tree_api.c, tree_core.c, tree_json.c,
 *   tree_path.c, tree_validate.c
 *   modules/treelock_core/src/protocol.c (treelock_validate_protocol)
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
 * Test 1: 手动注册节点
 *
 * 测试目标: 验证 treelock_register_node() 的基本功能 —
 *          注册根节点和子节点、重复 ID 拒绝、父节点查询。
 *
 * 运行路径:
 *   treelock_register_node(tl, 1, 0, "/")
 *     → tree_api.c: 懒初始化 tree_index (tree_index_init)
 *     → tree_core.c: tree_node_create(1, 0, "/") → 分配节点 + strdup label
 *     → tree_core.c: tree_index_insert(idx, node) → hash 查重 → 头插链
 *     → parent_id=0 (TREE_ROOT_PARENT) → idx->root_id = 1
 *
 *   treelock_register_node(tl, 2, 1, "home")
 *     → tree_node_create(2, 1, "home")
 *     → tree_index_insert → 插入 hash 表
 *     → tree_core.c: tree_index_find(idx, 1) → 找到父节点
 *     → tree_core.c: tree_node_add_child(parent, child) → children[] 数组扩展
 *
 *   treelock_register_node(tl, 1, 2, "dup") → 重复 ID
 *     → tree_index_insert → tree_index_find() 查重命中 → LOG + return ERR_INVAL
 *
 *   treelock_get_parent(tl, 1, &parent_id)
 *     → tree_api.c: tree_index_find(idx, 1) → node->parent_id=0
 *
 * 验证: register 成功返回 OK，重复 ID 返回 ERR_INVAL，parent 查询正确
 * ========================================================================= */

/**
 * 测试目标: 手动注册根节点+子节点，验证重复 ID 拒绝和父节点查询
 *
 * 运行路径: 参见上方 "Test 1" 块注释
 * 覆盖: tree_api.c treelock_register_node(), treelock_get_parent(),
 *       tree_core.c tree_node_create(), tree_index_insert(), tree_node_add_child()
 */
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
 * Test 2: 加载扁平格式 JSON
 *
 * 测试目标: 验证从扁平 JSON ({ "nodes": [...] }) 加载树结构，并正确
 *          建立节点关系和 path lookup 功能。
 *
 * 运行路径:
 *   treelock_load_tree_from_string(tl, json)
 *     → tree_api.c: 分配 tree_index → tree_index_init()
 *     → tree_json.c: treelock_load_tree_from_string_internal()
 *       → cJSON_Parse(json) → 解析 JSON 为 cJSON 树
 *       → _parse_tree_json() 检测 "nodes" 键 → 扁平格式
 *         → _parse_flat_nodes() 遍历数组:
 *             每项: _json_get_uint64("id"), _json_get_uint64("parent"),
 *                   _json_get_string("label") → parse_ctx_add_node()
 *         → 转换 parse_ctx[] → treelock_tree_node_t[] (tree_node_create)
 *       → tree_validate.c: treelock_validate_tree_structure()
 *         扫描: ID 非零/唯一, parent 有效性, 单根, 无环
 *       → tree_core.c: tree_index_insert() × N 逐个注册
 *       → 第二轮遍历: tree_node_add_child() 建立父子关系
 *     → 设置 tl->tree_data = idx, tl->tree_get_parent = _tree_get_parent_cb
 *
 *   treelock_lookup_path(tl, "/root/a/c", &node_id)
 *     → tree_api.c → tree_path.c: treelock_resolve_path()
 *       → 从 root_id 开始, 逐段解析: "/" → skip, "root" → match root.label,
 *         "a" → tree_index_find_child_by_label(root, "a"),
 *         "c" → tree_index_find_child_by_label(a, "c")
 *       → 返回 path_ids[], path_len, 取最后一项 = node_id
 *
 * 验证: 4 节点树结构正确，path lookup 返回正确 node_id
 * ========================================================================= */

/**
 * 测试目标: 扁平 JSON 加载 + 结构查询 + path lookup
 *
 * 运行路径: 参见上方 "Test 2" 块注释
 * 覆盖: tree_json.c _parse_flat_nodes(), tree_validate.c (校验通过路径),
 *       tree_core.c 批量注册 + 建立父子关系, tree_path.c treelock_resolve_path()
 */
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
 * Test 3: 加载嵌套格式 JSON
 *
 * 测试目标: 验证递归解析嵌套格式 JSON ({ "tree": {...} }) 的正确性。
 *
 * 运行路径:
 *   _parse_tree_json() 检测 "tree" 键 → 嵌套格式
 *     → _parse_nested_tree(json_root, TREE_ROOT_PARENT, depth=0)
 *       递归处理:
 *         parse_ctx_add_node(node_id, parent_id, label)
 *         cJSON_ArrayForEach(children) → _parse_nested_tree(child, node_id, depth+1)
 *       深度限制: TREE_MAX_DEPTH (256)
 *     后续校验 + 注册路径同 FlatJson
 *
 * 验证: 嵌套 3 层树正确加载，path lookup 返回叶子节点
 * ========================================================================= */

/**
 * 测试目标: 嵌套 JSON 递归解析 + path lookup
 *
 * 运行路径: 参见上方 "Test 3" 块注释
 * 覆盖: tree_json.c _parse_nested_tree() (递归), _parse_tree_json() 格式检测
 */
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
 * Test 4: 校验 — 重复 ID
 *
 * 测试目标: 验证树校验阶段正确检测重复的 node_id。
 *
 * 运行路径:
 *   treelock_validate_tree_structure(nodes, 2, idx)
 *     → tree_validate.c: 第一遍扫描
 *       → validate_set_insert(id_set, 1) → OK
 *       → validate_set_insert(id_set, 1) → validate_set_contains() → TRUE
 *         → TREELOCK_LOG_ERROR "duplicate node_id=1" → return ERR_INVAL
 *
 * 覆盖: tree_validate.c validate_set_insert() 查重逻辑
 * ========================================================================= */

/**
 * 测试目标: 重复 node_id 被校验拒绝
 *
 * 运行路径: 参见上方 "Test 4" 块注释
 * 覆盖: tree_validate.c — ID 唯一性检查路径
 */
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
 * Test 5: 校验 — 多根节点
 *
 * 测试目标: 验证树校验拒绝多个根节点 (parent=0 的节点 > 1)。
 *
 * 运行路径:
 *   treelock_validate_tree_structure(nodes, 2, idx)
 *     → 第一遍扫描:
 *       nodes[0]: parent=0 → root_count++ (1), root_id=1
 *       nodes[1]: parent=0 → root_count++ (2), root_id=2
 *     → root_count > 1 → "multiple root nodes (2)" → return ERR_INVAL
 *
 * 覆盖: tree_validate.c 根节点计数检查
 * ========================================================================= */

/**
 * 测试目标: 多个根节点被校验拒绝
 *
 * 运行路径: 参见上方 "Test 5" 块注释
 * 覆盖: tree_validate.c — root_count > 1 分支
 */
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
 * Test 6: 校验 — 环路检测
 *
 * 测试目标: 验证校验阶段检测到环 A(1)→B(2)→C(3)→A(1)
 *
 * 运行路径:
 *   treelock_validate_tree_structure(nodes, 3, idx)
 *     → 第一遍 + 第二遍扫描通过 (ID 唯一, parent 有效, 单根)
 *     → 环路检测:
 *       node[0] (id=1, parent=3):
 *         追溯: current=3 → 查 nodes[] → nodes[2].parent → current=2
 *               → 查 nodes[] → nodes[1].parent → current=1
 *               → current==node[0].node_id → 检测到环
 *               → "cycle detected at node_id=1" → return ERR_INVAL
 *
 * 覆盖: tree_validate.c 环路检测算法 (向上追溯 + 自引用检测)
 * ========================================================================= */

/**
 * 测试目标: 环路被校验检测到
 *
 * 运行路径: 参见上方 "Test 6" 块注释
 * 覆盖: tree_validate.c — 环路检测 (向上追溯 + 步数限制)
 */
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
 * Test 7: 协议自动强制
 *
 * 测试目标: 验证树加载后 lock 时自动检查父节点意向锁规则。
 *
 * 运行路径:
 *   树: root(1) → child(2)
 *
 *   treelock_lock(tl, 2, S) — 无父锁 → 拒绝
 *     → _do_lock_core() → treelock_validate_protocol(tl, 2, S)
 *       → tree_get_parent(tl->tree_data, 2) → parent_id=1
 *       → treelock_required_parent_mode(S) → TREELOCK_IS
 *       → treelock_get_mode(tl, 1) → TREELOCK_NL
 *       → required=IS, held=NL → LOG WARN + return ERR_PROTOCOL
 *
 *   treelock_lock(tl, 1, IS) → treelock_lock(tl, 2, S) — 正确顺序 → 成功
 *     → 先锁根: _do_lock_core() → validate → parent=0 (根) → OK
 *     → 再锁子: validate → parent=1, held=IS, required=IS → OK
 *
 *   treelock_lock(tl, 2, X) — 父只有 IS 不够 → 拒绝
 *     → required_parent_mode(X) → IX
 *     → held(1)=IS, required=IX → IS∉{IX,SIX,X} → ERR_PROTOCOL
 *
 * 覆盖: protocol.c treelock_validate_protocol() 两条规则分支
 * ========================================================================= */

/**
 * 测试目标: lock 时自动检查祖先节点意向锁
 *
 * 运行路径: 参见上方 "Test 7" 块注释
 * 覆盖: protocol.c treelock_validate_protocol() — IS 分支 + IX 分支
 */
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
 * Test 8: lock_path / unlock_path 基本流程
 *
 * 测试目标: 验证 treelock_lock_path() 自顶向下加意向锁 +
 *          treelock_unlock_path() 自底向上释放。
 *
 * 运行路径:
 *   树: / (1) → home (2) → alice (3)
 *
 *   treelock_lock_path(tl, "/home/alice", X)
 *     → tree_api.c: treelock_lock_path()
 *       → treelock_resolve_path() → path_ids=[1,2,3], path_len=3
 *       → treelock_ancestor_mode_for(X) → IX
 *       → for i=0: treelock_lock(tl, 1, IX) → _do_lock_core() → grant IX on /1
 *       → for i=1: treelock_lock(tl, 2, IX)
 *           → validate: parent=1, held=IX, required=IX → OK
 *           → grant IX on /2
 *       → for i=2: treelock_lock(tl, 3, X)
 *           → validate: parent=2, held=IX, required=IX → OK
 *           → grant X on /3
 *
 *   treelock_unlock_path(tl, "/home/alice")
 *     → tree_api.c: treelock_unlock_path()
 *       → treelock_resolve_path() → path_ids=[1,2,3], path_len=3
 *       → for i=2→0: treelock_unlock(tl, path_ids[i])
 *         → 逆序: 3→2→1 (自底向上)
 *
 * 验证: 祖先节点模式 IX/IX/X, 释放后全部 NL
 * ========================================================================= */

/**
 * 测试目标: lock_path X 模式 → 祖先 IX, unlock_path 逆序释放
 *
 * 运行路径: 参见上方 "Test 8" 块注释
 * 覆盖: tree_api.c treelock_lock_path() + treelock_unlock_path(),
 *       tree_path.c treelock_resolve_path(), treelock_ancestor_mode_for()
 */
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
 * Test 9: 向后兼容 (无树可加锁)
 *
 * 测试目标: 验证未加载树时 lock 操作不受协议校验限制 (向后兼容 Phase 1)。
 *
 * 运行路径:
 *   treelock_tree_loaded(tl) → tree_data==NULL → return FALSE
 *   treelock_lock(tl, 999, X)
 *     → _do_lock_core() → treelock_validate_protocol()
 *       → tree_data==NULL → return TREELOCK_OK (跳过校验)
 *       → 正常 grant X on 999
 *
 * 覆盖: protocol.c treelock_validate_protocol() — tree_data==NULL 快速返回
 * ========================================================================= */

/**
 * 测试目标: 无树时 lock 任意节点不触发协议校验
 *
 * 运行路径: 参见上方 "Test 9" 块注释
 * 覆盖: protocol.c treelock_validate_protocol() NULL 守卫
 */
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
 *
 * 测试目标: 验证 treelock_register_node() 的各类错误处理 —
 *          ID=0、根重复、父不存在、ID 重复。
 *
 * 运行路径:
 *   register_node(tl, 0, 0, null) — ID=0
 *     → tree_api.c: node_id==0 → return ERR_INVAL (不进入创建)
 *
 *   register_node(tl, 1, 0, "/") — 正常根节点
 *     → 懒初始化 + tree_node_create + tree_index_insert
 *     → parent=0 → idx->root_id=1
 *
 *   register_node(tl, 2, 0, "root2") — 根重复
 *     → tree_node_create(2,0,"root2") → tree_index_insert(OK)
 *     → parent=0 → idx->root_id != 0 (已有 root=1)
 *       → LOG "root already exists" → return ERR_INVAL
 *
 *   register_node(tl, 10, 99, "orphan") — 父不存在
 *     → tree_node_create + tree_index_insert
 *     → parent=99 → tree_index_find(idx, 99) → NULL
 *       → return ERR_INVAL
 *
 *   register_node(tl, 3, 1, "child") — 正常子节点
 *     → 父存在 + tree_node_add_child
 *
 *   register_node(tl, 1, 3, "dup") — 重复 ID
 *     → tree_index_insert → tree_index_find(1)!=NULL → ERR_INVAL
 *
 * 覆盖: tree_api.c treelock_register_node() 全部错误分支
 * ========================================================================= */

/**
 * 测试目标: register_node 全部错误处理边界
 *
 * 运行路径: 参见上方 "Test 10" 块注释
 * 覆盖: tree_api.c — ID=0 检查, 根重复检查, 父不存在检查,
 *       tree_core.c tree_index_insert 查重
 */
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
 * Test 11: 非法 JSON 输入
 *
 * 测试目标: 验证各种非法 JSON 输入被正确拒绝，不崩溃。
 *
 * 运行路径:
 *   InvalidJsonSyntax:
 *     "123": cJSON_Parse → number (非 object) → ERR_INVAL
 *     "\"tree\":{}": cJSON_Parse → 合法 JSON 但 root 非 object → ERR_INVAL
 *     "{}": cJSON_Parse → object 但无 "tree"/"nodes" → LOG + ERR_INVAL
 *     "": cJSON_Parse → NULL → ERR_INVAL
 *     nullptr: tl==null 或 json==null → ERR_INVAL (早期返回)
 *
 *   InvalidJsonEmptyNodes:
 *     "{\"nodes\":[]}": 解析成功但 node_count=0
 *       → validate: "empty node list" → ERR_INVAL
 *
 *   InvalidJsonMissingId:
 *     "{\"nodes\":[{\"parent\":0}]}": id 字段缺失
 *       → _parse_flat_nodes: cJSON_GetObjectItem("id") → NULL
 *         → LOG "missing 'id' field" → ERR_INVAL
 *
 *   InvalidJsonSelfParent:
 *     "{\"nodes\":[{\"id\":1,\"parent\":1}]}": parent==id
 *       → 校验第一遍 OK, 第二遍 OK, 根检查: node_count=1, parent=0? NO, parent=1
 *         → root_count=0 → "no root node found" → ERR_INVAL
 *       或: 环路检测 → current=1 == node_id=1 → cycle detected
 *
 * 覆盖: tree_json.c 各种错误返回, tree_validate.c 空列表/无根/自引用
 * ========================================================================= */

/**
 * 测试目标: 非法 JSON 语法被拒绝 (5 种格式)
 *
 * 运行路径: 参见上方 "Test 11" 块注释 — InvalidJsonSyntax
 * 覆盖: tree_json.c cJSON_Parse 错误处理
 */
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

/**
 * 测试目标: 空节点数组被校验拒绝
 *
 * 运行路径: treelock_validate_tree_structure() count==0 → "empty node list"
 * 覆盖: tree_validate.c — count==0 早期返回
 */
TEST(TreeTest, InvalidJsonEmptyNodes)
{
    treelock_t *tl = treelock_create(nullptr);
    ASSERT_NE(tl, nullptr);

    /* 空节点数组 → 校验失败（无根节点） */
    EXPECT_EQ(treelock_load_tree_from_string(tl, "{\"nodes\":[]}"), TREELOCK_ERR_INVAL);

    treelock_destroy(tl);
}

/**
 * 测试目标: 节点缺少 id 字段被拒绝
 *
 * 运行路径: _parse_flat_nodes() — cJSON_GetObjectItem("id")==NULL → ERR_INVAL
 * 覆盖: tree_json.c — flat 格式 id 必须检查
 */
TEST(TreeTest, InvalidJsonMissingId)
{
    treelock_t *tl = treelock_create(nullptr);
    ASSERT_NE(tl, nullptr);

    /* 节点缺少必须的 id 字段 */
    const char *json = "{ \"nodes\": [ { \"parent\": 0 } ] }";
    EXPECT_EQ(treelock_load_tree_from_string(tl, json), TREELOCK_ERR_INVAL);

    treelock_destroy(tl);
}

/**
 * 测试目标: 自引用 parent==id 被检测
 *
 * 运行路径: 校验阶段 root_count=0 (parent=1≠0) → "no root" → ERR_INVAL
 *          或环路检测 → current(1)==node_id(1) → cycle detected
 * 覆盖: tree_validate.c 根检查或环路检测
 */
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
 *
 * 测试目标: 验证加载新树时自动清除旧树结构（不泄漏内存），
 *          旧路径失效 + 新路径生效。
 *
 * 运行路径:
 *   treelock_load_tree_from_string(tl, tree1) → 加载成功, tree_data=idx1
 *   treelock_load_tree_from_string(tl, tree2)
 *     → tree_api.c: tree_data!=NULL → tree_index_destroy(idx1) + free(idx1)
 *     → 分配新 idx → 解析 + 校验 + 注册 tree2
 *     → tree_data=idx2
 *
 *   treelock_lookup_path(tl, "/root/a") → 解析失败 (tree1 已销毁)
 *   treelock_lookup_path(tl, "/newroot/b") → 解析成功 (tree2 有效)
 *
 * 覆盖: tree_api.c treelock_load_tree_from_string() 重加载分支,
 *       tree_core.c tree_index_destroy() 递归释放
 * ========================================================================= */

/**
 * 测试目标: 加载新树覆盖旧树，旧路径失效 + 新路径生效
 *
 * 运行路径: 参见上方 "Test 12" 块注释
 * 覆盖: tree_api.c — tree_data 非空时 tree_index_destroy + free
 */
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
 * Test 13: lock_path IS 目标模式
 *
 * 测试目标: 验证 lock_path 以 S 为目标时，祖先节点使用 IS (而非 IX)。
 *
 * 运行路径:
 *   treelock_lock_path(tl, "/home/data", S)
 *     → treelock_ancestor_mode_for(S) → TREELOCK_IS
 *     → for i=0: treelock_lock(tl, 1, IS) → grant IS on root
 *     → for i=1: treelock_lock(tl, 2, IS) → grant IS on /home
 *     → for i=2: treelock_lock(tl, 3, S)  → grant S on /home/data
 *
 * 对比: lock_path X 模式 → ancestor_mode=IX (Test 8 已验证)
 *
 * 覆盖: tree_path.c treelock_ancestor_mode_for() IS 分支,
 *       tree_api.c treelock_lock_path() 祖先模式应用
 * ========================================================================= */

/**
 * 测试目标: lock_path S 模式 → 祖先用 IS (非 IX)
 *
 * 运行路径: 参见上方 "Test 13" 块注释
 * 覆盖: tree_path.c treelock_ancestor_mode_for() IS/S case
 */
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
 *
 * 测试目标: 验证 lock_path / unlock_path 的各类边界和错误处理。
 *
 * 运行路径:
 *   LockPathInvalidPath:
 *     lock_path(tl, "/test", S) — 树未加载
 *       → tree_data==NULL → LOG + ERR_INVAL
 *     lock_path(tl, "", S) — 空路径
 *       → treelock_resolve_path() 中 path[0]=='\0' → "empty path" → ERR_INVAL
 *     lock_path(tl, nullptr, S) — NULL
 *       → tl==null || path==null → ERR_INVAL (早期)
 *     lock_path(tl, "/nonexist", S) — 不存在
 *       → resolve: tree_index_find_child_by_label(root, "nonexist") → NULL → ERR_INVAL
 *     lock_path(tl, "/dir", NL) — 非法 mode
 *       → mode <= NL || mode > MAX → ERR_INVAL
 *
 *   UnlockPathEdgeCases:
 *     unlock_path(tl, "/") — 未持有锁
 *       → resolve OK → for i: treelock_unlock → held==NULL → ERR_INVAL
 *       → 返回第一个错误
 *
 *   LockPathRootOnly:
 *     lock_path(tl, "/", X) — 仅根
 *       → resolve → path_ids=[1], path_len=1
 *       → i=0 (唯一): lock(1, X) → grant X on root
 *
 * 覆盖: tree_api.c + tree_path.c 全部错误处理分支
 * ========================================================================= */

/**
 * 测试目标: lock_path 各类无效输入被拒绝
 *
 * 运行路径: 参见上方 "Test 14" LockPathInvalidPath 路径
 * 覆盖: tree_api.c — 树未加载/空路径/NULL/模式非法,
 *       tree_path.c — 路径段不存在
 */
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

/**
 * 测试目标: unlock_path 边界条件
 *
 * 运行路径: 参见上方 "Test 14" UnlockPathEdgeCases 路径
 * 覆盖: tree_api.c treelock_unlock_path() — 树未加载/空路径/未持有锁
 */
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

/**
 * 测试目标: lock_path("/") 仅锁根节点
 *
 * 运行路径: 参见上方 "Test 14" LockPathRootOnly 路径
 * 覆盖: tree_path.c — path_len==1 (skip root label → done)
 */
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
 *
 * 测试目标: 验证 treelock_ancestor_mode_for() 对所有 6 种锁模式
 *          返回正确的祖先锁模式。
 *
 * 运行路径:
 *   treelock_ancestor_mode_for(mode)
 *     → tree_path.c: switch(mode)
 *       IS/S    → TREELOCK_IS  (共享类 → 父需意向共享)
 *       IX/SIX/X → TREELOCK_IX (排他类 → 父需意向排他)
 *       default → TREELOCK_NL  (NL/非法 → 不需要父锁)
 *
 * 此函数是 treelock_lock_path() 祖先模式推导的核心。
 * 覆盖: tree_path.c treelock_ancestor_mode_for() 全部 switch 分支
 * ========================================================================= */

/*
 * treelock_ancestor_mode_for 声明（内部函数，手动声明避免引入 tree_internal.h 的 cJSON 依赖）
 */
extern "C" {
treelock_mode_t treelock_ancestor_mode_for(treelock_mode_t child_mode);
}

/**
 * 测试目标: 全 6 种模式 + 非法值的祖先模式推导
 *
 * 运行路径: 参见上方 "Test 15" 块注释
 * 覆盖: tree_path.c treelock_ancestor_mode_for() — IS/S/IX/SIX/X/NL/default
 */
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
 *
 * 测试目标: 验证 treelock_lookup_path() 的边界处理 —
 *          树未加载、NULL 参数、根路径。
 *
 * 运行路径:
 *   lookup_path(tl, "/test", &id) — 树未加载
 *     → tree_data==NULL → ERR_INVAL
 *   lookup_path(tl, nullptr, &id) — NULL path
 *     → path==NULL → ERR_INVAL
 *   lookup_path(tl, "/root", nullptr) — NULL output
 *     → node_id==NULL → ERR_INVAL
 *   lookup_path(tl, "/root", &id)
 *     → resolve → path_ids=[1], path_len=1 → id=path_ids[0]
 *
 * 覆盖: tree_api.c treelock_lookup_path() 守卫检查
 * ========================================================================= */

/**
 * 测试目标: lookup_path 各种边界条件
 *
 * 运行路径: 参见上方 "Test 16" 块注释
 * 覆盖: tree_api.c treelock_lookup_path() — NULL 守卫 + 正常路径
 */
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
 *
 * 测试目标: 验证 treelock_get_parent() 的 NULL 安全、树未加载、
 *          不存在节点、根节点返回 0。
 *
 * 运行路径:
 *   get_parent(tl, 1, nullptr) → parent_id==NULL → ERR_INVAL
 *   get_parent(tl, 1, &pid) — 树未加载 → tree_data==NULL → ERR_INVAL
 *   get_parent(tl, 999, &pid) → tree_index_find(999) → NULL → ERR_INVAL
 *   get_parent(tl, 1, &pid) → node->parent_id → 0 (TREE_ROOT_PARENT)
 *
 * 覆盖: tree_api.c treelock_get_parent() 全部错误处理
 * ========================================================================= */

/**
 * 测试目标: get_parent 各种边界条件
 *
 * 运行路径: 参见上方 "Test 17" 块注释
 * 覆盖: tree_api.c treelock_get_parent() — NULL/未加载/不存在/根
 */
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
 *
 * 测试目标: 验证仅有一个根节点的树可以正常 lock/unlock，
 *          协议校验对根节点放行。
 *
 * 运行路径:
 *   register_node(tl, 1, 0, "/") → root_id=1
 *   treelock_lock(tl, 1, X)
 *     → _do_lock_core() → validate_protocol(tl, 1, X)
 *       → tree_get_parent(idx, 1) → TREE_ROOT_PARENT (0)
 *       → parent_id==0 → return OK (根节点不需要父锁)
 *     → grant X on node 1
 *
 * 覆盖: protocol.c treelock_validate_protocol() 根节点分支
 * ========================================================================= */

/**
 * 测试目标: 单节点树 lock 根节点通过协议校验
 *
 * 运行路径: 参见上方 "Test 18" 块注释
 * 覆盖: protocol.c — parent_id==0 根节点快速返回
 */
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
 *
 * 测试目标: 验证 treelock_tree_loaded(nullptr) 安全返回 FALSE。
 *
 * 运行路径:
 *   treelock_destroy(tl) → tree_data=NULL, tree_get_parent=NULL, free
 *   treelock_tree_loaded(nullptr)
 *     → tl==NULL → return FALSE (不访问成员)
 *
 * 覆盖: tree_api.c treelock_tree_loaded() NULL 守卫
 * ========================================================================= */

/**
 * 测试目标: destroy 后 tree_loaded(nullptr) 安全返回
 *
 * 运行路径: 参见上方 "Test 19" 块注释
 * 覆盖: tree_api.c treelock_tree_loaded() — tl==NULL 分支
 */
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

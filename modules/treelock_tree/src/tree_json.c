/*
 * TreeLocks — JSON 树结构解析
 *
 * 使用 cJSON 解析树定义文件，支持两种格式：
 *   1. 扁平格式: { "nodes": [{ "id":..., "parent":..., "label":...}, ...] }
 *   2. 嵌套格式: { "tree": { "id":..., "label":..., "children": [...] } }
 *
 * 解析结果转换为统一的节点列表，供后续校验和注册使用。
 *
 * 版本: 0.1.0
 * 日期: 2026-06-13
 */

#include "tree_internal.h"
#include "treelock_log.h"
#include <stdlib.h> /* malloc, free */
#include <string.h> /* strdup, strlen, strcmp */

/* =========================================================================
 * 解析中间结构
 * ========================================================================= */

/** 解析后的单个节点信息（临时数据，未校验） */
typedef struct {
    treelock_node_id_t  node_id;
    treelock_node_id_t  parent_id;
    CHAR               *label;     /* 堆分配，可为 NULL */
} parsed_node_t;

/** 解析上下文 */
typedef struct {
    parsed_node_t  *nodes;       /**< 节点数组（动态扩展） */
    UINT_64         count;       /**< 当前节点数           */
    UINT_64         capacity;    /**< 数组容量             */
    INT_32          error;       /**< 是否已出错           */
} parse_ctx_t;

/* =========================================================================
 * parse_ctx 辅助函数
 * ========================================================================= */

#define PARSE_CTX_INIT_CAP  (64)

/**
 * 函数名称：parse_ctx_init
 *
 * 功能描述：初始化解析上下文
 *
 * @param[INOUT] ctx - 上下文指针
 */
static VOID parse_ctx_init(
    INOUT parse_ctx_t *ctx)
{
    if (ctx == NULL) {
        return;
    }
    ctx->nodes    = NULL;
    ctx->count    = 0;
    ctx->capacity = 0;
    ctx->error    = FALSE;
}

/**
 * 函数名称：parse_ctx_destroy
 *
 * 功能描述：释放解析上下文中的所有资源
 *
 * @param[INOUT] ctx - 上下文指针
 */
static VOID parse_ctx_destroy(
    INOUT parse_ctx_t *ctx)
{
    UINT_64 i;
    if (ctx == NULL) {
        return;
    }
    if (ctx->nodes != NULL) {
        for (i = 0; i < ctx->count; i++) {
            free(ctx->nodes[i].label);
        }
        free(ctx->nodes);
        ctx->nodes = NULL;
    }
    ctx->count    = 0;
    ctx->capacity = 0;
}

/**
 * 函数名称：parse_ctx_add_node
 *
 * 功能描述：向解析上下文添加一个节点
 *
 * @param[INOUT] ctx       - 上下文指针
 * @param[IN]    node_id   - 节点 ID
 * @param[IN]    parent_id - 父节点 ID
 * @param[IN]    label     - 标签（内部 strdup）
 *
 * @return TREELOCK_OK 成功
 * @return TREELOCK_ERR_INVAL 内存不足
 */
static RET_CODE parse_ctx_add_node(
    INOUT parse_ctx_t       *ctx,
    IN    treelock_node_id_t node_id,
    IN    treelock_node_id_t parent_id,
    IN    CSTR_PTR           label)
{
    parsed_node_t *entry;
    UINT_64 new_cap;
    parsed_node_t *new_nodes;

    if (ctx == NULL || ctx->error) {
        return TREELOCK_ERR_INVAL;
    }

    /* 扩展数组 */
    if (ctx->count >= ctx->capacity) {
        new_cap = (ctx->capacity == 0)
                  ? PARSE_CTX_INIT_CAP
                  : ctx->capacity * 2;

        new_nodes = (parsed_node_t *)realloc(
            ctx->nodes, (size_t)(new_cap * sizeof(parsed_node_t)));
        if (new_nodes == NULL) {
            TREELOCK_LOG_ERROR("TREE",
                "parse_ctx: OOM expanding from %llu to %llu nodes",
                (unsigned long long)ctx->capacity,
                (unsigned long long)new_cap);
            ctx->error = TRUE;
            return TREELOCK_ERR_INVAL;
        }
        ctx->nodes    = new_nodes;
        ctx->capacity = new_cap;
    }

    entry = &ctx->nodes[ctx->count];
    entry->node_id   = node_id;
    entry->parent_id = parent_id;
    entry->label     = (label != NULL) ? strdup(label) : NULL;
    if (label != NULL && entry->label == NULL) {
        ctx->error = TRUE;
        return TREELOCK_ERR_INVAL;
    }
    ctx->count++;

    return TREELOCK_OK;
}

/* =========================================================================
 * JSON 值提取辅助函数
 * ========================================================================= */

/**
 * 函数名称：_json_get_uint64
 *
 * 功能描述：从 JSON 对象中提取 uint64_t 字段，缺失或类型错误返回默认值
 *
 * @param[IN] item          - cJSON 对象
 * @param[IN] field_name    - 字段名
 * @param[IN] default_value - 字段缺失时的默认值
 *
 * @return 字段值或默认值
 */
static treelock_node_id_t _json_get_uint64(
    IN const cJSON *item,
    IN CSTR_PTR     field_name,
    IN treelock_node_id_t default_value)
{
    const cJSON *field = cJSON_GetObjectItem(item, field_name);
    if (field == NULL || !cJSON_IsNumber(field)) {
        return default_value;
    }
    return (treelock_node_id_t)cJSON_GetNumberValue(field);
}

/**
 * 函数名称：_json_get_string
 *
 * 功能描述：从 JSON 对象中提取字符串字段
 *
 * @param[IN] item       - cJSON 对象
 * @param[IN] field_name - 字段名
 *
 * @return 字符串指针（cJSON 内部引用，不可 free），缺失返回 NULL
 */
static CSTR_PTR _json_get_string(
    IN const cJSON *item,
    IN CSTR_PTR     field_name)
{
    const cJSON *field = cJSON_GetObjectItem(item, field_name);
    if (field == NULL || !cJSON_IsString(field)) {
        return NULL;
    }
    return cJSON_GetStringValue(field);
}

/* =========================================================================
 * 嵌套格式解析（递归）
 * ========================================================================= */

/**
 * 函数名称：_parse_nested_tree
 *
 * 功能描述：递归解析嵌套格式的 JSON 树节点
 *
 * @param[INOUT] ctx       - 解析上下文
 * @param[IN]    json_node - cJSON 树节点对象
 * @param[IN]    parent_id - 父节点 ID（根节点传入 TREE_ROOT_PARENT）
 * @param[IN]    depth     - 当前递归深度
 *
 * @return TREELOCK_OK 成功
 * @return TREELOCK_ERR_INVAL 格式错误或超过最大深度
 */
static RET_CODE _parse_nested_tree(
    INOUT parse_ctx_t       *ctx,
    IN    const cJSON       *json_node,
    IN    treelock_node_id_t parent_id,
    IN    INT_32             depth)
{
    const cJSON *children;
    const cJSON *child;
    treelock_node_id_t node_id;
    CSTR_PTR label;

    if (ctx == NULL || json_node == NULL) {
        return TREELOCK_ERR_INVAL;
    }

    /* 深度限制 */
    if (depth > TREE_MAX_DEPTH) {
        TREELOCK_LOG_ERROR("TREE", "JSON tree exceeds max depth %d", TREE_MAX_DEPTH);
        return TREELOCK_ERR_INVAL;
    }

    /* 提取 id（必需，缺失视为 0，后续校验会捕获） */
    node_id = _json_get_uint64(json_node, "id", 0);

    /* 提取 label（可选） */
    label = _json_get_string(json_node, "label");

    /* 添加当前节点 */
    {
        RET_CODE rc = parse_ctx_add_node(ctx, node_id, parent_id, label);
        if (rc != TREELOCK_OK) {
            return rc;
        }
    }

    /* 递归处理子节点 */
    children = cJSON_GetObjectItem(json_node, "children");
    if (children == NULL || !cJSON_IsArray(children)) {
        /* 叶子节点，无子节点 */
        return TREELOCK_OK;
    }

    cJSON_ArrayForEach(child, children) {
        RET_CODE rc = _parse_nested_tree(ctx, child, node_id, depth + 1);
        if (rc != TREELOCK_OK) {
            return rc;
        }
    }

    return TREELOCK_OK;
}

/* =========================================================================
 * 扁平格式解析
 * ========================================================================= */

/**
 * 函数名称：_parse_flat_nodes
 *
 * 功能描述：解析扁平格式的节点列表
 *
 *          JSON 结构:
 *          {
 *            "nodes": [
 *              { "id": 1, "label": "/", "parent": 0 },
 *              { "id": 2, "label": "home", "parent": 1 }
 *            ]
 *          }
 *
 * @param[INOUT] ctx       - 解析上下文
 * @param[IN]    nodes_arr - cJSON 节点数组
 *
 * @return TREELOCK_OK 成功
 * @return TREELOCK_ERR_INVAL 格式错误
 */
static RET_CODE _parse_flat_nodes(
    INOUT parse_ctx_t *ctx,
    IN    const cJSON  *nodes_arr)
{
    const cJSON *item;
    INT_32 index = 0;

    if (ctx == NULL || nodes_arr == NULL || !cJSON_IsArray(nodes_arr)) {
        return TREELOCK_ERR_INVAL;
    }

    cJSON_ArrayForEach(item, nodes_arr) {
        treelock_node_id_t node_id;
        treelock_node_id_t parent_id;
        CSTR_PTR label;
        RET_CODE rc;

        if (!cJSON_IsObject(item)) {
            TREELOCK_LOG_ERROR("TREE", "nodes[%d] is not an object", index);
            return TREELOCK_ERR_INVAL;
        }

        /* id: 必需字段 */
        {
            const cJSON *id_field = cJSON_GetObjectItem(item, "id");
            if (id_field == NULL || !cJSON_IsNumber(id_field)) {
                TREELOCK_LOG_ERROR("TREE", "nodes[%d] missing 'id' field", index);
                return TREELOCK_ERR_INVAL;
            }
            node_id = (treelock_node_id_t)cJSON_GetNumberValue(id_field);
        }

        /* parent: 可选，默认 0（即根节点） */
        parent_id = _json_get_uint64(item, "parent", TREE_ROOT_PARENT);

        /* label: 可选 */
        label = _json_get_string(item, "label");

        rc = parse_ctx_add_node(ctx, node_id, parent_id, label);
        if (rc != TREELOCK_OK) {
            return rc;
        }
        index++;
    }

    return TREELOCK_OK;
}

/* =========================================================================
 * 公共 API — JSON 解析
 * ========================================================================= */

/**
 * 函数名称：_parse_tree_json
 *
 * 功能描述：解析树定义 JSON，自动识别扁平/嵌套格式，返回节点列表
 *
 *          识别规则：
 *          - JSON 中存在 "tree" 键 → 嵌套格式
 *          - JSON 中存在 "nodes" 键 → 扁平格式
 *          - 两者都不存在 → 错误
 *
 * @param[IN]  json_root   - cJSON 根对象
 * @param[OUT] out_nodes   - 输出：节点指针数组（调用者 free）
 * @param[OUT] out_count   - 输出：节点数量
 *
 * @return TREELOCK_OK 成功
 * @return TREELOCK_ERR_INVAL JSON 格式错误
 */
static RET_CODE _parse_tree_json(
    IN  const cJSON           *json_root,
    OUT treelock_tree_node_t ***out_nodes,
    OUT UINT_64               *out_count)
{
    parse_ctx_t ctx;
    RET_CODE    rc = TREELOCK_ERR_INVAL;
    const cJSON *tree_item;
    const cJSON *nodes_item;
    treelock_tree_node_t **result_nodes;
    UINT_64 i;

    if (json_root == NULL || out_nodes == NULL || out_count == NULL) {
        return TREELOCK_ERR_INVAL;
    }

    parse_ctx_init(&ctx);

    tree_item  = cJSON_GetObjectItem(json_root, "tree");
    nodes_item = cJSON_GetObjectItem(json_root, "nodes");

    if (tree_item != NULL && cJSON_IsObject(tree_item)) {
        /* ── 嵌套格式 ── */
        TREELOCK_LOG_INFO("TREE", "parsing nested tree format...");
        rc = _parse_nested_tree(&ctx, tree_item, TREE_ROOT_PARENT, 0);
    } else if (nodes_item != NULL && cJSON_IsArray(nodes_item)) {
        /* ── 扁平格式 ── */
        TREELOCK_LOG_INFO("TREE", "parsing flat nodes format...");
        rc = _parse_flat_nodes(&ctx, nodes_item);
    } else {
        TREELOCK_LOG_ERROR("TREE",
            "JSON must contain 'tree' (object) or 'nodes' (array) key");
        rc = TREELOCK_ERR_INVAL;
    }

    if (rc != TREELOCK_OK || ctx.error) {
        parse_ctx_destroy(&ctx);
        return TREELOCK_ERR_INVAL;
    }

    /*
     * 将 parse_ctx 转换为 treelock_tree_node_t 数组。
     * 不在 parse_ctx 中直接创建 tree_node 是因为校验步骤需要先检查所有节点。
     */
    /* 转换：为 parse_ctx 中每个节点创建 treelock_tree_node_t */
    result_nodes = (treelock_tree_node_t **)malloc(
        (size_t)(ctx.count * sizeof(treelock_tree_node_t *)));
    if (result_nodes == NULL) {
        TREELOCK_LOG_ERROR("TREE",
            "OOM allocating result array for %llu nodes",
            (unsigned long long)ctx.count);
        parse_ctx_destroy(&ctx);
        return TREELOCK_ERR_INVAL;
    }

    for (i = 0; i < ctx.count; i++) {
        parsed_node_t *p = &ctx.nodes[i];
        result_nodes[i] = tree_node_create(p->node_id, p->parent_id, p->label);
        if (result_nodes[i] == NULL) {
            UINT_64 j;
            for (j = 0; j < i; j++) {
                tree_node_destroy(result_nodes[j]);
            }
            free(result_nodes);
            parse_ctx_destroy(&ctx);
            return TREELOCK_ERR_INVAL;
        }
    }

    *out_nodes = result_nodes;
    *out_count = ctx.count;

    TREELOCK_LOG_INFO("TREE", "parsed %llu nodes from JSON", (unsigned long long)ctx.count);

    /* parse_ctx 的 label 字符串已被 tree_node_create strdup 拷贝，安全释放 */
    parse_ctx_destroy(&ctx);

    return TREELOCK_OK;
}

/**
 * 函数名称：treelock_load_tree_from_string_internal
 *
 * 功能描述：从 JSON 字符串解析并加载树结构到指定索引
 *
 *          此函数组合了解析 + 校验 + 注册三个步骤。
 *
 * @param[INOUT] idx         - 树索引指针
 * @param[IN]    json_string - JSON 字符串
 *
 * @return TREELOCK_OK 加载成功
 * @return TREELOCK_ERR_INVAL 解析/校验/注册失败
 */
RET_CODE treelock_load_tree_from_string_internal(
    INOUT treelock_tree_index_t *idx,
    IN    CSTR_PTR               json_string)
{
    cJSON *json_root;
    treelock_tree_node_t **nodes = NULL;
    UINT_64 count = 0;
    RET_CODE rc;

    if (idx == NULL || json_string == NULL) {
        return TREELOCK_ERR_INVAL;
    }

    /* ── 1. JSON 解析 ── */
    json_root = cJSON_Parse(json_string);
    if (json_root == NULL) {
        const CHAR *err_ptr = cJSON_GetErrorPtr();
        TREELOCK_LOG_ERROR("TREE", "JSON parse error near: %s",
                           err_ptr ? err_ptr : "(unknown)");
        return TREELOCK_ERR_INVAL;
    }

    if (!cJSON_IsObject(json_root)) {
        TREELOCK_LOG_ERROR("TREE", "JSON root must be an object");
        cJSON_Delete(json_root);
        return TREELOCK_ERR_INVAL;
    }

    rc = _parse_tree_json(json_root, &nodes, &count);
    cJSON_Delete(json_root);

    if (rc != TREELOCK_OK) {
        return rc;
    }

    /* ── 2. 校验（外部函数，见 tree_validate.c）── */
    rc = treelock_validate_tree_structure(nodes, count, idx);
    if (rc != TREELOCK_OK) {
        UINT_64 i;
        for (i = 0; i < count; i++) {
            tree_node_destroy(nodes[i]);
        }
        free(nodes);
        return rc;
    }

    /* ── 3. 注册到索引 ── */
    {
        UINT_64 i;
        treelock_tree_node_t *parent;

        for (i = 0; i < count; i++) {
            rc = tree_index_insert(idx, nodes[i]);
            if (rc != TREELOCK_OK) {
                /*
                 * 回滚：nodes[0..i-1] 已在 hash 表中（由 idx 接管），
                 * 只销毁 nodes[i..count-1]（尚未插入的节点）。
                 * 已插入的节点由调用者 tree_index_destroy(idx) 统一释放。
                 */
                UINT_64 j;
                for (j = i; j < count; j++) {
                    tree_node_destroy(nodes[j]);
                }
                free(nodes);
                return rc;
            }
        }

        /* 建立父子关系（第二轮遍历，因为此时所有节点都在 hash 表中） */
        for (i = 0; i < count; i++) {
            if (nodes[i]->parent_id != TREE_ROOT_PARENT) {
                parent = tree_index_find(idx, nodes[i]->parent_id);
                if (parent != NULL) {
                    rc = tree_node_add_child(parent, nodes[i]);
                    if (rc != TREELOCK_OK) {
                        /*
                         * OOM 致命错误：节点已在 hash 表中，
                         * 但子节点关系建立失败。
                         * 已插入的节点由调用者 tree_index_destroy(idx) 统一释放。
                         * 只释放 nodes 指针数组（节点本身由 idx 接管）。
                         */
                        free(nodes);
                        return rc;
                    }
                }
                /* parent 不存在的情况已在校验阶段捕获 */
            }
        }

        /* 设置根节点 ID */
        for (i = 0; i < count; i++) {
            if (nodes[i]->parent_id == TREE_ROOT_PARENT) {
                idx->root_id = nodes[i]->node_id;
                break;
            }
        }
    }

    idx->loaded = TRUE;

    TREELOCK_LOG_INFO("TREE",
        "tree structure fully loaded: %llu nodes, root_id=%llu",
        (unsigned long long)idx->node_count,
        (unsigned long long)idx->root_id);

    /* 释放临时节点指针数组（节点本身已由索引接管） */
    free(nodes);

    return TREELOCK_OK;
}

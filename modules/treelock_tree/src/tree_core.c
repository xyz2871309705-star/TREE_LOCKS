/*
 * TreeLocks — 树结构核心实现
 *
 * 提供树节点元数据管理、hash 索引（按 node_id）、
 * 基本 CRUD 操作和树索引生命周期管理。
 *
 * 版本: 0.1.0
 * 日期: 2026-06-13
 */

#include "tree_internal.h"
#include "treelock_log.h"
#include <stdlib.h> /* malloc, calloc, free, realloc */
#include <string.h> /* strcmp, strdup, strlen */

/* =========================================================================
 * 树节点管理
 * ========================================================================= */

/**
 * 函数名称：tree_node_create
 *
 * 功能描述：分配并初始化一个树节点元数据记录
 *
 * @param[IN] node_id   - 节点 ID
 * @param[IN] parent_id - 父节点 ID
 * @param[IN] label     - 标签字符串（内部 strdup，可为 NULL）
 *
 * @return 成功返回节点指针，失败返回 NULL
 */
treelock_tree_node_t *tree_node_create(
    IN treelock_node_id_t  node_id,
    IN treelock_node_id_t  parent_id,
    IN CSTR_PTR            label)
{
    treelock_tree_node_t *node;

    node = (treelock_tree_node_t *)calloc(1, sizeof(treelock_tree_node_t));
    if (node == NULL) {
        return NULL;
    }

    node->node_id   = node_id;
    node->parent_id = parent_id;
    node->child_count = 0;
    node->child_cap   = 0;
    node->children    = NULL;
    node->next        = NULL;

    /* 深拷贝 label */
    if (label != NULL) {
        node->label = strdup(label);
        if (node->label == NULL) {
            TREELOCK_LOG_ERROR("TREE",
                "tree_node_create: strdup failed for label='%s'", label);
            free(node);
            return NULL;
        }
    } else {
        node->label = NULL;
    }

    return node;
}

/**
 * 函数名称：tree_node_destroy
 *
 * 功能描述：释放单个树节点及其所有资源
 *
 *          注意：此函数不递归销毁子节点。
 *          递归销毁由 tree_index_destroy 负责。
 *
 * @param[IN] node - 树节点指针
 */
VOID tree_node_destroy(
    IN treelock_tree_node_t *node)
{
    if (node == NULL) {
        return;
    }

    free(node->label);
    node->label = NULL;

    free(node->children);
    node->children = NULL;

    free(node);
}

/**
 * 函数名称：tree_node_add_child
 *
 * 功能描述：向父节点的子节点数组添加子节点引用
 *
 *          自动扩展数组（初始容量 4，翻倍增长）。
 *
 * @param[IN] parent - 父节点
 * @param[IN] child  - 子节点
 *
 * @return TREELOCK_OK 成功
 * @return TREELOCK_ERR_INVAL 内存不足
 */
RET_CODE tree_node_add_child(
    IN treelock_tree_node_t *parent,
    IN treelock_tree_node_t *child)
{
    UINT_32 new_cap;
    treelock_tree_node_t **new_children;

    if (parent == NULL || child == NULL) {
        return TREELOCK_ERR_INVAL;
    }

    /* 扩展子节点数组 */
    if (parent->child_count >= parent->child_cap) {
        new_cap = (parent->child_cap == 0)
                  ? 4  /* 初始容量 */
                  : parent->child_cap * 2;

        new_children = (treelock_tree_node_t **)realloc(
            parent->children,
            (size_t)(new_cap * sizeof(treelock_tree_node_t *)));
        if (new_children == NULL) {
            TREELOCK_LOG_ERROR("TREE",
                "tree_node_add_child: OOM expanding children for node_id=%llu "
                "cap=%u→%u",
                (unsigned long long)parent->node_id,
                (unsigned int)parent->child_cap,
                (unsigned int)new_cap);
            return TREELOCK_ERR_INVAL;
        }
        parent->children = new_children;
        parent->child_cap = new_cap;
    }

    parent->children[parent->child_count] = child;
    parent->child_count++;

    return TREELOCK_OK;
}

/* =========================================================================
 * 树索引管理
 * ========================================================================= */

/**
 * 函数名称：tree_index_init
 *
 * 功能描述：初始化树结构索引（所有字段归零）
 *
 * @param[INOUT] idx - 树索引指针
 */
VOID tree_index_init(
    INOUT treelock_tree_index_t *idx)
{
    UINT_32 i;

    if (idx == NULL) {
        return;
    }

    for (i = 0; i < TREE_HASH_BUCKETS; i++) {
        idx->node_hash[i] = NULL;
    }
    idx->root_id    = TREE_ROOT_PARENT;
    idx->node_count = 0;
    idx->loaded     = FALSE;
}

/**
 * 函数名称：_tree_index_destroy_nodes
 *
 * 功能描述：递归销毁树节点及其子树
 *
 * @param[IN] node - 要销毁的节点（递归释放所有子节点）
 */
static VOID _tree_index_destroy_nodes(
    IN treelock_tree_node_t *node)
{
    UINT_32 i;

    if (node == NULL) {
        return;
    }

    /* 先递归销毁子节点 */
    for (i = 0; i < node->child_count; i++) {
        _tree_index_destroy_nodes(node->children[i]);
    }

    /* 再销毁自身（此时 children 数组可安全释放） */
    tree_node_destroy(node);
}

/**
 * 函数名称：tree_index_destroy
 *
 * 功能描述：销毁树结构索引，递归释放所有节点内存
 *
 *          从根节点开始递归释放整棵树。
 *          同时清理 hash 表中的所有指针，防止悬空指针。
 *
 * @param[INOUT] idx - 树索引指针
 */
VOID tree_index_destroy(
    INOUT treelock_tree_index_t *idx)
{
    UINT_32 i;

    if (idx == NULL) {
        return;
    }

    /*
     * 从根节点递归销毁整棵树（通过 children 链遍历所有可达节点）。
     */
    if (idx->root_id != TREE_ROOT_PARENT) {
        treelock_tree_node_t *root = tree_index_find(idx, idx->root_id);
        if (root != NULL) {
            _tree_index_destroy_nodes(root);
        }
    }

    /* 清理 hash 表 ——  所有从 root 可达的节点已释放，清空指针 */
    for (i = 0; i < TREE_HASH_BUCKETS; i++) {
        idx->node_hash[i] = NULL;
    }

    idx->root_id    = TREE_ROOT_PARENT;
    idx->node_count = 0;
    idx->loaded     = FALSE;
}

/**
 * 函数名称：tree_index_insert
 *
 * 功能描述：向树索引的 node_hash 中插入节点
 *
 *          唯一索引: node_id（不允许重复）
 *
 * @param[INOUT] idx  - 树索引指针
 * @param[IN]    node - 要插入的节点
 *
 * @return TREELOCK_OK 成功
 * @return TREELOCK_ERR_INVAL node_id 重复
 */
RET_CODE tree_index_insert(
    INOUT treelock_tree_index_t *idx,
    IN    treelock_tree_node_t  *node)
{
    UINT_32 bucket;

    if (idx == NULL || node == NULL) {
        return TREELOCK_ERR_INVAL;
    }

    /* node_id hash 查重 */
    if (tree_index_find(idx, node->node_id) != NULL) {
        TREELOCK_LOG_ERROR("TREE", "duplicate node_id=%llu",
                           (unsigned long long)node->node_id);
        return TREELOCK_ERR_INVAL;
    }

    /* 插入链头（头插法，热点节点查找更快） */
    bucket = tree_hash_node_id(node->node_id);
    node->next = idx->node_hash[bucket];
    idx->node_hash[bucket] = node;
    idx->node_count++;

    TREELOCK_LOG_TRACE("TREE", "inserted: node_id=%llu parent=%llu label=%s",
                       (unsigned long long)node->node_id,
                       (unsigned long long)node->parent_id,
                       node->label ? node->label : "(null)");

    return TREELOCK_OK;
}

/**
 * 函数名称：tree_index_find
 *
 * 功能描述：按节点 ID 在 node_hash 中查找节点
 *
 * @param[IN] idx     - 树索引指针
 * @param[IN] node_id - 节点 ID
 *
 * @return 找到返回节点指针，未找到返回 NULL
 */
treelock_tree_node_t *tree_index_find(
    IN treelock_tree_index_t *idx,
    IN treelock_node_id_t     node_id)
{
    UINT_32 bucket;
    treelock_tree_node_t *node;

    if (idx == NULL) {
        return NULL;
    }

    bucket = tree_hash_node_id(node_id);
    node   = idx->node_hash[bucket];

    while (node != NULL) {
        if (node->node_id == node_id) {
            return node;
        }
        node = node->next;
    }
    return NULL;
}

/**
 * 函数名称：tree_index_find_child_by_label
 *
 * 功能描述：在父节点的子节点列表中查找具有给定标签的子节点
 *
 *          用于路径解析（如 "/home/alice" → 逐级查找子节点）。
 *
 * @param[IN] parent - 父节点
 * @param[IN] label  - 子节点标签
 *
 * @return 找到返回子节点指针，未找到返回 NULL
 */
treelock_tree_node_t *tree_index_find_child_by_label(
    IN treelock_tree_node_t *parent,
    IN CSTR_PTR              label)
{
    UINT_32 i;

    if (parent == NULL || label == NULL) {
        return NULL;
    }

    for (i = 0; i < parent->child_count; i++) {
        treelock_tree_node_t *child = parent->children[i];
        if (child->label != NULL && strcmp(child->label, label) == EQUAL) {
            return child;
        }
    }
    return NULL;
}

/**
 * 函数名称：tree_index_collect_all
 *
 * 功能描述：收集树索引中所有节点的指针到数组中（用于校验遍历）
 *
 * @param[IN]  idx       - 树索引指针
 * @param[OUT] nodes     - 输出：节点指针数组（调用者 free）
 * @param[OUT] count     - 输出：节点数量
 *
 * @return TREELOCK_OK 成功
 * @return TREELOCK_ERR_INVAL 内存不足
 */
RET_CODE tree_index_collect_all(
    IN  treelock_tree_index_t  *idx,
    OUT treelock_tree_node_t ***nodes,
    OUT UINT_64                *count)
{
    UINT_32 i;
    UINT_64 collected = 0;
    treelock_tree_node_t **arr;

    if (idx == NULL || nodes == NULL || count == NULL) {
        return TREELOCK_ERR_INVAL;
    }

    if (idx->node_count == 0) {
        *nodes = NULL;
        *count = 0;
        return TREELOCK_OK;
    }

    arr = (treelock_tree_node_t **)malloc(
        (size_t)(idx->node_count * sizeof(treelock_tree_node_t *)));
    if (arr == NULL) {
        return TREELOCK_ERR_INVAL;
    }

    for (i = 0; i < TREE_HASH_BUCKETS; i++) {
        treelock_tree_node_t *node = idx->node_hash[i];
        while (node != NULL) {
            if (collected >= idx->node_count) {
                /* 防御性检查：不应发生 */
                break;
            }
            arr[collected] = node;
            collected++;
            node = node->next;
        }
    }

    *nodes = arr;
    *count = collected;
    return TREELOCK_OK;
}

/*
 * TreeLocks — 树结构校验
 *
 * 对已解析的节点列表执行完整性校验：
 *   1. 节点 ID 唯一性
 *   2. parent_id 引用有效性
 *   3. 单根节点检查
 *   4. 环路检测
 *
 * 版本: 0.1.0
 * 日期: 2026-06-13
 */

#include "tree_internal.h"
#include "treelock_log.h"
#include <stdlib.h> /* malloc, free */

/* =========================================================================
 * 辅助: ID 集合（用于快速查重和查找）
 * ========================================================================= */

#define VALIDATE_SET_BUCKETS  (256)

/** 校验用的简单 ID 集合（链式 hash） */
typedef struct validate_id_entry_s {
    treelock_node_id_t           node_id;
    struct validate_id_entry_s  *next;
} validate_id_entry_t;

/** 创建/销毁 ID 集合 */
static VOID validate_set_destroy(
    IN validate_id_entry_t *set[])
{
    UINT_32 i;
    if (set == NULL) {
        return;
    }
    for (i = 0; i < VALIDATE_SET_BUCKETS; i++) {
        validate_id_entry_t *entry = set[i];
        while (entry != NULL) {
            validate_id_entry_t *next = entry->next;
            free(entry);
            entry = next;
        }
        set[i] = NULL;
    }
}

/**
 * 向集合中插入 ID。
 *
 * @return TREELOCK_OK 插入成功
 * @return TREELOCK_ERR_INVAL ID 已存在（重复）
 */
static RET_CODE validate_set_insert(
    INOUT validate_id_entry_t  *set[],
    IN    treelock_node_id_t    id)
{
    UINT_32 bucket = (UINT_32)(id % VALIDATE_SET_BUCKETS);
    validate_id_entry_t *entry;

    /* 查重 */
    entry = set[bucket];
    while (entry != NULL) {
        if (entry->node_id == id) {
            return TREELOCK_ERR_INVAL; /* 重复 */
        }
        entry = entry->next;
    }

    /* 插入链头 */
    entry = (validate_id_entry_t *)malloc(sizeof(validate_id_entry_t));
    if (entry == NULL) {
        return TREELOCK_ERR_INVAL; /* OOM */
    }
    entry->node_id = id;
    entry->next    = set[bucket];
    set[bucket]    = entry;
    return TREELOCK_OK;
}

/**
 * 检查 ID 是否在集合中。
 *
 * @return TRUE 存在，FALSE 不存在
 */
static INT_32 validate_set_contains(
    IN validate_id_entry_t *set[],
    IN treelock_node_id_t   id)
{
    UINT_32 bucket = (UINT_32)(id % VALIDATE_SET_BUCKETS);
    validate_id_entry_t *entry = set[bucket];
    while (entry != NULL) {
        if (entry->node_id == id) {
            return TRUE;
        }
        entry = entry->next;
    }
    return FALSE;
}

/* =========================================================================
 * 公共校验函数
 * ========================================================================= */

/**
 * 函数名称：treelock_validate_tree_structure
 *
 * 功能描述：对解析出的节点列表执行完整结构校验
 *
 *          检查项：
 *          1. 节点 ID > 0（0 为非法节点 ID）
 *          2. 节点 ID 唯一（无重复）
 *          3. parent_id 指向存在的节点（或为 TREE_ROOT_PARENT）
 *          4. 有且仅有一个根节点（parent_id == TREE_ROOT_PARENT）
 *          5. 无环路（沿 parent 链追溯，不会回到自身）
 *
 * @param[IN]  nodes     - 节点指针数组
 * @param[IN]  count     - 节点数量
 * @param[OUT] idx       - 输出：校验通过后将 root_id 写入 idx->root_id
 *
 * @return TREELOCK_OK 校验通过
 * @return TREELOCK_ERR_INVAL 校验失败（通过日志输出具体原因）
 */
RET_CODE treelock_validate_tree_structure(
    IN    treelock_tree_node_t **nodes,
    IN    UINT_64                count,
    OUT   treelock_tree_index_t *idx)
{
    validate_id_entry_t *id_set[VALIDATE_SET_BUCKETS] = { NULL };
    UINT_64 i;
    UINT_64 root_count = 0;
    treelock_node_id_t root_id = TREE_ROOT_PARENT;
    INT_32 ok = TRUE;

    if (nodes == NULL || count == 0) {
        TREELOCK_LOG_ERROR("TREE", "validate: empty node list");
        return TREELOCK_ERR_INVAL;
    }

    /* ── 第一遍扫描: ID 唯一性 + 非零 + 统计根节点 ── */
    for (i = 0; i < count; i++) {
        treelock_node_id_t nid = nodes[i]->node_id;

        /* 1. 节点 ID 不能为 0 */
        if (nid == 0) {
            TREELOCK_LOG_ERROR("TREE", "validate: node %llu has invalid id=0",
                               (unsigned long long)i);
            ok = FALSE;
            continue;
        }

        /* 2. ID 唯一性 */
        if (validate_set_insert(id_set, nid) != TREELOCK_OK) {
            TREELOCK_LOG_ERROR("TREE", "validate: duplicate node_id=%llu",
                               (unsigned long long)nid);
            ok = FALSE;
        }

        /* 3. 统计根节点 */
        if (nodes[i]->parent_id == TREE_ROOT_PARENT) {
            root_count++;
            root_id = nid;
        }
    }

    if (!ok) {
        validate_set_destroy(id_set);
        return TREELOCK_ERR_INVAL;
    }

    /* ── 第二遍扫描: parent_id 有效性 ── */
    for (i = 0; i < count; i++) {
        treelock_node_id_t pid = nodes[i]->parent_id;

        if (pid != TREE_ROOT_PARENT) {
            if (!validate_set_contains(id_set, pid)) {
                TREELOCK_LOG_ERROR("TREE",
                    "validate: node_id=%llu has parent_id=%llu which does not exist",
                    (unsigned long long)nodes[i]->node_id,
                    (unsigned long long)pid);
                ok = FALSE;
            }
        }
    }

    if (!ok) {
        validate_set_destroy(id_set);
        return TREELOCK_ERR_INVAL;
    }

    /* ── 根节点检查 ── */
    if (root_count == 0) {
        TREELOCK_LOG_ERROR("TREE", "validate: no root node found (no node with parent=0)");
        validate_set_destroy(id_set);
        return TREELOCK_ERR_INVAL;
    }
    if (root_count > 1) {
        TREELOCK_LOG_ERROR("TREE", "validate: multiple root nodes (%llu)", (unsigned long long)root_count);
        validate_set_destroy(id_set);
        return TREELOCK_ERR_INVAL;
    }

    /* ── 环路检测 ── */
    /*
     * 算法: 对每个节点沿 parent 链向上追溯，如果在追溯过程中遇到自身，
     * 说明存在环。用步数限制防止无限循环。
     */
    {
        UINT_64 max_steps = count + 1; /* 路径长度不可能超过节点数 */

        for (i = 0; i < count; i++) {
            treelock_node_id_t current = nodes[i]->parent_id;
            UINT_64 steps = 0;

            while (current != TREE_ROOT_PARENT && steps < max_steps) {
                if (current == nodes[i]->node_id) {
                    TREELOCK_LOG_ERROR("TREE",
                        "validate: cycle detected at node_id=%llu",
                        (unsigned long long)nodes[i]->node_id);
                    validate_set_destroy(id_set);
                    return TREELOCK_ERR_INVAL;
                }

                /* 查找 current 的父节点 */
                {
                    UINT_64 j;
                    INT_32  found = FALSE;
                    for (j = 0; j < count; j++) {
                        if (nodes[j]->node_id == current) {
                            current = nodes[j]->parent_id;
                            found = TRUE;
                            break;
                        }
                    }
                    if (!found) {
                        /* 不应该到这里（前面已校验 parent_id 有效性） */
                        break;
                    }
                }
                steps++;
            }

            if (steps >= max_steps) {
                TREELOCK_LOG_ERROR("TREE",
                    "validate: potential cycle or too deep path from node_id=%llu",
                    (unsigned long long)nodes[i]->node_id);
                validate_set_destroy(id_set);
                return TREELOCK_ERR_INVAL;
            }
        }
    }

    /* 校验通过，设置 root_id */
    if (idx != NULL) {
        idx->root_id = root_id;
    }

    validate_set_destroy(id_set);

    TREELOCK_LOG_INFO("TREE", "validate: %llu nodes, root=%llu, structure OK",
                      (unsigned long long)count,
                      (unsigned long long)root_id);

    return TREELOCK_OK;
}

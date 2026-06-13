/*
 * TreeLocks — 路径操作
 *
 * 提供路径解析、路径加锁/解锁等功能。
 *
 * 版本: 0.1.0
 * 日期: 2026-06-13
 */

#include "tree_internal.h"
#include "treelock_log.h"
#include <stdlib.h> /* malloc, free */
#include <string.h> /* strlen, strchr */

/* =========================================================================
 * 路径解析
 * ========================================================================= */

/**
 * 函数名称：treelock_resolve_path
 *
 * 功能描述：按路径字符串解析，返回从根到目标节点的 node_id 序列
 *
 *          路径格式: "/label1/label2/.../target_label"
 *          示例: "/home/alice" → [root_id, home_id, alice_id]
 *
 *          特殊路径:
 *          - "/" 或 "/root_label" → 只有根节点
 *
 * @param[IN]  idx       - 树索引指针
 * @param[IN]  path      - 路径字符串
 * @param[OUT] path_ids  - 输出：路径上各节点 ID 数组（调用者 free）
 * @param[OUT] path_len  - 输出：路径上的节点数
 *
 * @return TREELOCK_OK 解析成功
 * @return TREELOCK_ERR_INVAL 树未加载或路径无效
 */
RET_CODE treelock_resolve_path(
    IN  treelock_tree_index_t  *idx,
    IN  CSTR_PTR                path,
    OUT treelock_node_id_t    **path_ids,
    OUT UINT_32                *path_len)
{
    treelock_tree_node_t *current;
    treelock_node_id_t *ids;
    UINT_32 capacity;
    UINT_32 count;
    CSTR_PTR p;
    CSTR_PTR seg_start;
    CSTR_PTR seg_end;
    CHAR   *seg_buf;
    UINT_32 seg_len;
    UINT_32 i;

    if (idx == NULL || path == NULL || path_ids == NULL || path_len == NULL) {
        return TREELOCK_ERR_INVAL;
    }

    if (!idx->loaded) {
        TREELOCK_LOG_ERROR("TREE", "resolve_path: tree not loaded");
        return TREELOCK_ERR_INVAL;
    }

    if (path[0] == '\0') {
        TREELOCK_LOG_ERROR("TREE", "resolve_path: empty path");
        return TREELOCK_ERR_INVAL;
    }

    /* 从根节点开始 */
    current = tree_index_find(idx, idx->root_id);
    if (current == NULL) {
        TREELOCK_LOG_ERROR("TREE", "resolve_path: root node %llu not found",
                           (unsigned long long)idx->root_id);
        return TREELOCK_ERR_INVAL;
    }

    /* 分配路径 ID 数组（初始容量 16，足够覆盖大多数场景） */
    capacity = 16;
    ids = (treelock_node_id_t *)malloc((size_t)(capacity * sizeof(treelock_node_id_t)));
    if (ids == NULL) {
        return TREELOCK_ERR_INVAL;
    }
    count = 0;

    /* 根节点总是在路径上 */
    ids[count++] = current->node_id;

    /* 跳过开头的 '/' */
    p = path;
    if (*p == '/') {
        p++;
    }

    /* 如果只剩空字符串（即路径就是 "/"），解析完成 */
    if (*p == '\0') {
        *path_ids = ids;
        *path_len = count;
        return TREELOCK_OK;
    }

    /*
     * 根节点的 label 可能出现在路径的第一个段中。
     * 例如：root 的 label="root", path="/root/a"
     *       → 第一个段 "root" 匹配 root.label，跳过
     *       → 然后继续解析 "a"
     *
     * 如果 path="/root"（只有 root label），则直接返回。
     */
    if (current->label != NULL) {
        UINT_32 root_label_len = (UINT_32)strlen(current->label);
        CSTR_PTR after_slash = p;
        if (strncmp(after_slash, current->label, root_label_len) == EQUAL) {
            CHAR next_char = after_slash[root_label_len];
            if (next_char == '\0') {
                /* 路径就是 "/<root_label>"，已完成 */
                *path_ids = ids;
                *path_len = count;
                return TREELOCK_OK;
            }
            if (next_char == '/') {
                /* 路径 "/<root_label>/..."，跳过 root label 段 */
                p = after_slash + root_label_len + 1; /* +1 跳过 '/' */
                if (*p == '\0') {
                    *path_ids = ids;
                    *path_len = count;
                    return TREELOCK_OK;
                }
            }
        }
    }

    /* 分配标签解析缓冲区 */
    seg_buf = (CHAR *)malloc(TREE_PATH_MAX);
    if (seg_buf == NULL) {
        free(ids);
        return TREELOCK_ERR_INVAL;
    }

    /* 逐段解析 */
    seg_start = p;
    while (*seg_start != '\0') {
        /* 找下一个 '/' 或 '\0' */
        seg_end = strchr(seg_start, '/');
        if (seg_end == NULL) {
            seg_len = (UINT_32)strlen(seg_start);
        } else {
            seg_len = (UINT_32)(seg_end - seg_start);
        }

        if (seg_len == 0) {
            /* 连续的 // 或末尾 /，跳过 */
            if (seg_end != NULL) {
                seg_start = seg_end + 1;
                continue;
            }
            break;
        }

        /* 检查缓冲区大小 */
        if (seg_len >= TREE_PATH_MAX) {
            TREELOCK_LOG_ERROR("TREE", "resolve_path: segment too long (max=%d)",
                               TREE_PATH_MAX - 1);
            free(seg_buf);
            free(ids);
            return TREELOCK_ERR_INVAL;
        }

        /* 拷贝标签段并终止 */
        for (i = 0; i < seg_len; i++) {
            seg_buf[i] = seg_start[i];
        }
        seg_buf[seg_len] = '\0';

        /* 在当前节点的子节点中查找 */
        {
            treelock_tree_node_t *child = tree_index_find_child_by_label(
                current, seg_buf);
            if (child == NULL) {
                TREELOCK_LOG_ERROR("TREE",
                    "resolve_path: child '%s' not found under node_id=%llu",
                    seg_buf, (unsigned long long)current->node_id);
                free(seg_buf);
                free(ids);
                return TREELOCK_ERR_INVAL;
            }

            /* 扩展 ids 数组 */
            if (count >= capacity) {
                capacity *= 2;
                treelock_node_id_t *new_ids = (treelock_node_id_t *)realloc(
                    ids, (size_t)(capacity * sizeof(treelock_node_id_t)));
                if (new_ids == NULL) {
                    free(seg_buf);
                    free(ids);
                    return TREELOCK_ERR_INVAL;
                }
                ids = new_ids;
            }

            ids[count++] = child->node_id;
            current = child;
        }

        /* 移到下一段 */
        if (seg_end == NULL) {
            break;
        }
        seg_start = seg_end + 1;
    }

    free(seg_buf);
    *path_ids = ids;
    *path_len = count;
    return TREELOCK_OK;
}

/* =========================================================================
 * 祖先锁模式推导
 * ========================================================================= */

/**
 * 函数名称：treelock_ancestor_mode_for
 *
 * 功能描述：获取在子节点加锁时祖先节点所需的推荐锁模式
 *
 *          IS / S    子锁 → 祖先需要 IS（允许子树中有共享锁）
 *          IX/SIX/X  子锁 → 祖先需要 IX（允许子树中有排他锁）
 *
 * @param[IN] child_mode - 目标节点的锁模式
 *
 * @return 祖先节点推荐的锁模式
 */
treelock_mode_t treelock_ancestor_mode_for(
    IN treelock_mode_t child_mode)
{
    switch (child_mode) {
    case TREELOCK_IS:
    case TREELOCK_S:
        return TREELOCK_IS;

    case TREELOCK_IX:
    case TREELOCK_SIX:
    case TREELOCK_X:
        return TREELOCK_IX;

    default:
        /* NL 或其他非法值 */
        return TREELOCK_NL;
    }
}

/*
 * TreeLocks — 树结构管理公共 API 实现
 *
 * 桥接 treelock_tree 内部实现与 treelock_core 客户端，
 * 提供树加载、路径加锁/解锁等公共接口。
 *
 * 版本: 0.1.0
 * 日期: 2026-06-13
 */

#include "treelock_tree.h"
#include "tree_internal.h"
#include "internal.h"         /* treelock_s 内部结构 */
#include "treelock_log.h"
#include <stdlib.h>           /* malloc, free */
#include <string.h>           /* strlen, memcpy */
#include <stdio.h>            /* fopen, fread, fclose */

/* =========================================================================
 * 桥接回调: 父节点查询（供 treelock_core 内部调用）
 * ========================================================================= */

/**
 * 函数名称：_tree_get_parent_cb
 *
 * 功能描述：供 treelock_core 内部调用的父节点查询回调
 *
 *          由 treelock_validate_protocol() 触发。
 *
 * @param[IN] tree_data - 不透明树索引指针（实际为 treelock_tree_index_t *）
 * @param[IN] node_id   - 节点 ID
 *
 * @return 父节点 ID，未找到返回 0
 */
static treelock_node_id_t _tree_get_parent_cb(
    IN PTR_VOID           tree_data,
    IN treelock_node_id_t node_id)
{
    treelock_tree_index_t *idx;
    treelock_tree_node_t  *node;

    if (tree_data == NULL) {
        return (treelock_node_id_t)0;
    }

    idx  = (treelock_tree_index_t *)tree_data;
    node = tree_index_find(idx, node_id);
    if (node == NULL) {
        return (treelock_node_id_t)0;
    }
    return node->parent_id;
}

/**
 * 函数名称：_tree_destroy_cb
 *
 * 功能描述：供 treelock_core 内部调用的树结构销毁回调
 *
 *          由 treelock_destroy() 触发，避免 treelock_core
 *          直接依赖 treelock_tree（保持模块解耦）。
 *
 * @param[IN] tree_data - 不透明树索引指针（实际为 treelock_tree_index_t *）
 */
static VOID _tree_destroy_cb(
    IN PTR_VOID tree_data)
{
    treelock_tree_index_t *idx;

    if (tree_data == NULL) {
        return;
    }

    idx = (treelock_tree_index_t *)tree_data;
    tree_index_destroy(idx);
    free(idx);
}

/* =========================================================================
 * 辅助: 文件读取
 * ========================================================================= */

/**
 * 函数名称：_read_file_to_string
 *
 * 功能描述：读取整个文件内容到堆分配的字符串
 *
 * @param[IN]  filepath - 文件路径
 * @param[OUT] out_str  - 输出：堆分配的字符串（调用者 free）
 *
 * @return TREELOCK_OK 成功
 * @return TREELOCK_ERR_INVAL 文件打开失败或内存不足
 */
static RET_CODE _read_file_to_string(
    IN  CSTR_PTR  filepath,
    OUT CHAR     **out_str)
{
    FILE   *fp;
    long    fsize;
    CHAR   *buf;
    size_t  read_size;

    if (filepath == NULL || out_str == NULL) {
        return TREELOCK_ERR_INVAL;
    }

    fp = fopen(filepath, "rb");
    if (fp == NULL) {
        TREELOCK_LOG_ERROR("TREE", "cannot open file: %s", filepath);
        return TREELOCK_ERR_INVAL;
    }

    /* 获取文件大小 */
    if (fseek(fp, 0, SEEK_END) != 0) {
        TREELOCK_LOG_ERROR("TREE", "fseek failed for file: %s", filepath);
        fclose(fp);
        return TREELOCK_ERR_INVAL;
    }
    fsize = ftell(fp);
    if (fsize < 0) {
        TREELOCK_LOG_ERROR("TREE", "ftell failed for file: %s", filepath);
        fclose(fp);
        return TREELOCK_ERR_INVAL;
    }
    rewind(fp);

    /* 分配缓冲区（多 1 字节给 '\0'） */
    buf = (CHAR *)malloc((size_t)(fsize + 1));
    if (buf == NULL) {
        TREELOCK_LOG_ERROR("TREE", "OOM allocating %ld bytes for file: %s",
                           fsize + 1, filepath);
        fclose(fp);
        return TREELOCK_ERR_INVAL;
    }

    read_size = fread(buf, 1, (size_t)fsize, fp);
    fclose(fp);

    if (read_size != (size_t)fsize) {
        free(buf);
        TREELOCK_LOG_ERROR("TREE", "read file failed: %s", filepath);
        return TREELOCK_ERR_INVAL;
    }

    buf[fsize] = '\0';
    *out_str = buf;
    return TREELOCK_OK;
}

/* =========================================================================
 * L1: 树结构加载
 * ========================================================================= */

/**
 * 函数名称：treelock_load_tree_from_file
 *
 * 功能描述：从 JSON 文件加载树结构定义
 *
 * @param[IN] tl       - 锁句柄
 * @param[IN] filepath - JSON 树定义文件路径
 *
 * @return TREELOCK_OK 加载成功
 * @return TREELOCK_ERR_INVAL 文件不存在 / JSON 解析失败 / 树结构不合法
 */
RET_CODE treelock_load_tree_from_file(
    IN treelock_t  *tl,
    IN CSTR_PTR     filepath)
{
    CHAR     *json_str;
    RET_CODE  rc;

    if (tl == NULL || filepath == NULL) {
        return TREELOCK_ERR_INVAL;
    }

    rc = _read_file_to_string(filepath, &json_str);
    if (rc != TREELOCK_OK) {
        return rc;
    }

    rc = treelock_load_tree_from_string(tl, json_str);
    free(json_str);
    return rc;
}

/**
 * 函数名称：treelock_load_tree_from_string
 *
 * 功能描述：从 JSON 字符串加载树结构定义
 *
 * @param[IN] tl          - 锁句柄
 * @param[IN] json_string - JSON 字符串
 *
 * @return TREELOCK_OK 加载成功
 * @return TREELOCK_ERR_INVAL 解析/校验/注册失败
 */
RET_CODE treelock_load_tree_from_string(
    IN treelock_t  *tl,
    IN CSTR_PTR     json_string)
{
    treelock_tree_index_t *idx;
    RET_CODE rc;

    if (tl == NULL || json_string == NULL) {
        return TREELOCK_ERR_INVAL;
    }

    /* 如果之前已加载过树，先清除 */
    if (tl->tree_data != NULL) {
        TREELOCK_LOG_INFO("TREE",
            "replacing existing tree in client '%s'",
            tl->config.client_id ? tl->config.client_id : "local");
        tree_index_destroy((treelock_tree_index_t *)tl->tree_data);
        free(tl->tree_data);
        tl->tree_data       = NULL;
        tl->tree_get_parent = NULL;
        tl->tree_destroy    = NULL;
    }

    /* 分配树索引 */
    idx = (treelock_tree_index_t *)malloc(sizeof(treelock_tree_index_t));
    if (idx == NULL) {
        TREELOCK_LOG_ERROR("TREE", "OOM allocating tree index");
        return TREELOCK_ERR_INVAL;
    }
    tree_index_init(idx);

    /* 解析 + 校验 + 注册 */
    rc = treelock_load_tree_from_string_internal(idx, json_string);
    if (rc != TREELOCK_OK) {
        tree_index_destroy(idx);
        free(idx);
        return rc;
    }

    /* 注册桥接到 treelock_core */
    tl->tree_data       = (PTR_VOID)idx;
    tl->tree_get_parent = _tree_get_parent_cb;
    tl->tree_destroy    = _tree_destroy_cb;

    TREELOCK_LOG_INFO("TREE", "tree loaded into client '%s': %llu nodes",
                      tl->config.client_id ? tl->config.client_id : "local",
                      (unsigned long long)idx->node_count);
    return TREELOCK_OK;
}

/**
 * 函数名称：treelock_register_node
 *
 * 功能描述：以编程方式手动注册单个树节点
 *
 * @param[IN] tl        - 锁句柄
 * @param[IN] node_id   - 节点 ID（必须 > 0）
 * @param[IN] parent_id - 父节点 ID，0 表示根
 * @param[IN] label     - 节点标签（可选）
 *
 * @return TREELOCK_OK 注册成功
 * @return TREELOCK_ERR_INVAL ID 重复 / parent 不存在 / 内存不足
 */
RET_CODE treelock_register_node(
    IN treelock_t         *tl,
    IN treelock_node_id_t  node_id,
    IN treelock_node_id_t  parent_id,
    IN CSTR_PTR            label)
{
    treelock_tree_index_t *idx;
    treelock_tree_node_t  *node;
    treelock_tree_node_t  *parent_node;
    RET_CODE rc;

    if (tl == NULL || node_id == 0) {
        return TREELOCK_ERR_INVAL;
    }

    /* 懒初始化树索引 */
    if (tl->tree_data == NULL) {
        idx = (treelock_tree_index_t *)malloc(sizeof(treelock_tree_index_t));
        if (idx == NULL) {
            TREELOCK_LOG_ERROR("TREE", "OOM allocating tree index for register_node");
            return TREELOCK_ERR_INVAL;
        }
        tree_index_init(idx);
        tl->tree_data       = (PTR_VOID)idx;
        tl->tree_get_parent = _tree_get_parent_cb;
        tl->tree_destroy    = _tree_destroy_cb;
        TREELOCK_LOG_DEBUG("TREE", "lazy-init tree index for register_node");
    } else {
        idx = (treelock_tree_index_t *)tl->tree_data;
    }

    /* 创建节点 */
    node = tree_node_create(node_id, parent_id, label);
    if (node == NULL) {
        TREELOCK_LOG_ERROR("TREE",
            "failed to create node: node_id=%llu parent=%llu",
            (unsigned long long)node_id, (unsigned long long)parent_id);
        return TREELOCK_ERR_INVAL;
    }

    /*
     * 先执行所有校验，通过后再插入 hash 表，
     * 避免插入后校验失败导致的内存泄漏。
     */

    /* 校验：父节点必须存在 */
    if (parent_id != TREE_ROOT_PARENT) {
        parent_node = tree_index_find(idx, parent_id);
        if (parent_node == NULL) {
            TREELOCK_LOG_ERROR("TREE",
                "register_node: parent_id=%llu does not exist",
                (unsigned long long)parent_id);
            tree_node_destroy(node);
            return TREELOCK_ERR_INVAL;
        }
    } else {
        /* 校验：根节点唯一 */
        if (idx->root_id != TREE_ROOT_PARENT) {
            TREELOCK_LOG_ERROR("TREE",
                "register_node: root already exists (id=%llu), cannot add second root",
                (unsigned long long)idx->root_id);
            tree_node_destroy(node);
            return TREELOCK_ERR_INVAL;
        }
    }

    /* 插入索引（自动查重） */
    rc = tree_index_insert(idx, node);
    if (rc != TREELOCK_OK) {
        tree_node_destroy(node);
        return rc;
    }

    /* 建立父子关系（插入 hash 后执行，此时 parent 可通过 tree_index_find 找到） */
    if (parent_id != TREE_ROOT_PARENT) {
        parent_node = tree_index_find(idx, parent_id);
        /* parent_node 已在校验阶段确认为非 NULL */
        rc = tree_node_add_child(parent_node, node);
        if (rc != TREELOCK_OK) {
            /* 回滚：从 hash 表中移除已插入的节点并释放 */
            tree_index_remove(idx, node_id);
            tree_node_destroy(node);
            return TREELOCK_ERR_INVAL;
        }
    } else {
        idx->root_id = node_id;
    }

    idx->loaded = TRUE;

    TREELOCK_LOG_INFO("TREE",
        "registered: node_id=%llu parent=%llu label=%s",
        (unsigned long long)node_id,
        (unsigned long long)parent_id,
        label ? label : "(null)");

    return TREELOCK_OK;
}

/**
 * 函数名称：treelock_tree_unload
 *
 * 功能描述：卸载当前加载的树结构，释放所有树节点内存
 *
 *          卸载后协议校验回退为宽松模式（不检查父节点锁）。
 *          未加载树时调用无副作用。
 *
 * @param[IN] tl - 锁句柄
 */
VOID treelock_tree_unload(
    IN treelock_t *tl)
{
    if (tl == NULL || tl->tree_data == NULL) {
        return;
    }

    TREELOCK_LOG_INFO("TREE", "unloading tree from client '%s'",
                      tl->config.client_id ? tl->config.client_id : "local");

    tree_index_destroy((treelock_tree_index_t *)tl->tree_data);
    free(tl->tree_data);
    tl->tree_data       = NULL;
    tl->tree_get_parent = NULL;
    tl->tree_destroy    = NULL;
}

/**
 * 函数名称：treelock_tree_loaded
 *
 * 功能描述：查询树结构是否已加载
 *
 * @param[IN] tl - 锁句柄
 *
 * @return TRUE 已加载，FALSE 未加载或 tl 为 NULL
 */
INT_32 treelock_tree_loaded(
    IN treelock_t *tl)
{
    if (tl == NULL || tl->tree_data == NULL) {
        return FALSE;
    }
    return ((treelock_tree_index_t *)tl->tree_data)->loaded;
}

/* =========================================================================
 * L2: 路径加锁 / 解锁
 * ========================================================================= */

/**
 * 函数名称：treelock_lock_path
 *
 * 功能描述：按路径加锁，自动沿路径逐级获取意向锁
 *
 * @param[IN] tl   - 锁句柄
 * @param[IN] path - 路径字符串
 * @param[IN] mode - 目标节点的锁模式
 *
 * @return TREELOCK_OK 成功
 * @return TREELOCK_ERR_* 失败
 */
RET_CODE treelock_lock_path(
    IN treelock_t     *tl,
    IN CSTR_PTR        path,
    IN treelock_mode_t mode)
{
    treelock_tree_index_t *idx;
    treelock_node_id_t    *path_ids;
    UINT_32                path_len;
    treelock_mode_t        ancestor_mode;
    UINT_32                i;
    RET_CODE               rc;

    if (tl == NULL || path == NULL) {
        return TREELOCK_ERR_INVAL;
    }

    if (mode <= TREELOCK_NL || mode > TREELOCK_MODE_MAX) {
        return TREELOCK_ERR_INVAL;
    }

    if (tl->tree_data == NULL) {
        TREELOCK_LOG_ERROR("TREE", "lock_path: tree not loaded");
        return TREELOCK_ERR_INVAL;
    }

    idx = (treelock_tree_index_t *)tl->tree_data;

    /* 解析路径 */
    rc = treelock_resolve_path(idx, path, &path_ids, &path_len);
    if (rc != TREELOCK_OK) {
        return rc;
    }

    /* 推导祖先锁模式 */
    ancestor_mode = treelock_ancestor_mode_for(mode);

    TREELOCK_LOG_DEBUG("TREE",
        "lock_path: path=%s target_mode=%s ancestor_mode=%s path_len=%u",
        path, treelock_mode_name(mode),
        treelock_mode_name(ancestor_mode), (unsigned int)path_len);

    /*
     * 自顶向下加锁：
     *   路径[0] = 根节点 → ancestor_mode (IS/IX)
     *   路径[1..n-2] = 中间节点 → ancestor_mode (IS/IX)
     *   路径[n-1] = 目标节点 → mode
     */
    for (i = 0; i < path_len; i++) {
        treelock_mode_t lock_mode = (i < path_len - 1) ? ancestor_mode : mode;
        rc = treelock_lock(tl, path_ids[i], lock_mode);
        if (rc != TREELOCK_OK) {
            /* 部分加锁失败 → 释放已加锁的节点 */
            UINT_32 j;
            TREELOCK_LOG_ERROR("TREE",
                "lock_path failed at step %u/%u: node=%llu mode=%s rc=%d",
                (unsigned int)i, (unsigned int)path_len,
                (unsigned long long)path_ids[i],
                treelock_mode_name(lock_mode), rc);
            for (j = 0; j < i; j++) {
                treelock_unlock(tl, path_ids[j]);
            }
            free(path_ids);
            return rc;
        }
    }

    free(path_ids);
    return TREELOCK_OK;
}

/**
 * 函数名称：treelock_unlock_path
 *
 * 功能描述：按路径解锁，自底向上释放路径上所有锁
 *
 * @param[IN] tl   - 锁句柄
 * @param[IN] path - 路径字符串
 *
 * @return TREELOCK_OK 成功
 * @return TREELOCK_ERR_* 失败
 */
RET_CODE treelock_unlock_path(
    IN treelock_t  *tl,
    IN CSTR_PTR     path)
{
    treelock_tree_index_t *idx;
    treelock_node_id_t    *path_ids;
    UINT_32                path_len;
    INT_32                 i;
    RET_CODE               rc;
    RET_CODE               last_err = TREELOCK_OK;

    if (tl == NULL || path == NULL) {
        return TREELOCK_ERR_INVAL;
    }

    if (tl->tree_data == NULL) {
        TREELOCK_LOG_ERROR("TREE", "unlock_path: tree not loaded");
        return TREELOCK_ERR_INVAL;
    }

    idx = (treelock_tree_index_t *)tl->tree_data;

    /* 解析路径 */
    rc = treelock_resolve_path(idx, path, &path_ids, &path_len);
    if (rc != TREELOCK_OK) {
        return rc;
    }

    TREELOCK_LOG_DEBUG("TREE", "unlock_path: path=%s path_len=%u",
                       path, (unsigned int)path_len);

    /* 自底向上释放 */
    for (i = (INT_32)(path_len - 1); i >= 0; i--) {
        rc = treelock_unlock(tl, path_ids[i]);
        if (rc != TREELOCK_OK) {
            TREELOCK_LOG_WARN("TREE",
                "unlock_path: unlock node=%llu failed rc=%d",
                (unsigned long long)path_ids[i], rc);
            /* 记录第一个错误，但继续释放其余锁 */
            if (last_err == TREELOCK_OK) {
                last_err = rc;
            }
        }
    }

    free(path_ids);
    return last_err;
}

/* =========================================================================
 * 查询
 * ========================================================================= */

/**
 * 函数名称：treelock_get_parent
 *
 * 功能描述：查询节点的父节点 ID
 *
 * @param[IN]  tl         - 锁句柄
 * @param[IN]  node_id    - 节点 ID
 * @param[OUT] parent_id  - 输出：父节点 ID
 *
 * @return TREELOCK_OK 成功
 * @return TREELOCK_ERR_INVAL 树未加载或节点不存在
 */
RET_CODE treelock_get_parent(
    IN  treelock_t         *tl,
    IN  treelock_node_id_t  node_id,
    OUT treelock_node_id_t *parent_id)
{
    treelock_tree_index_t *idx;
    treelock_tree_node_t  *node;

    if (tl == NULL || parent_id == NULL) {
        return TREELOCK_ERR_INVAL;
    }

    if (tl->tree_data == NULL) {
        return TREELOCK_ERR_INVAL;
    }

    idx  = (treelock_tree_index_t *)tl->tree_data;
    node = tree_index_find(idx, node_id);
    if (node == NULL) {
        return TREELOCK_ERR_INVAL;
    }

    *parent_id = node->parent_id;
    return TREELOCK_OK;
}

/**
 * 函数名称：treelock_lookup_path
 *
 * 功能描述：根据路径字符串查找对应的节点 ID
 *
 * @param[IN]  tl       - 锁句柄
 * @param[IN]  path     - 路径字符串
 * @param[OUT] node_id  - 输出：节点 ID
 *
 * @return TREELOCK_OK 找到节点
 * @return TREELOCK_ERR_INVAL 树未加载或路径不存在
 */
RET_CODE treelock_lookup_path(
    IN  treelock_t         *tl,
    IN  CSTR_PTR            path,
    OUT treelock_node_id_t *node_id)
{
    treelock_tree_index_t *idx;
    treelock_node_id_t    *path_ids;
    UINT_32                path_len;
    RET_CODE               rc;

    if (tl == NULL || path == NULL || node_id == NULL) {
        return TREELOCK_ERR_INVAL;
    }

    if (tl->tree_data == NULL) {
        return TREELOCK_ERR_INVAL;
    }

    idx = (treelock_tree_index_t *)tl->tree_data;

    rc = treelock_resolve_path(idx, path, &path_ids, &path_len);
    if (rc != TREELOCK_OK) {
        return rc;
    }

    /* 返回路径上最后一个节点 ID（目标节点） */
    *node_id = path_ids[path_len - 1];
    free(path_ids);
    return TREELOCK_OK;
}

/*
 * TreeLocks — 树结构管理公共 API
 *
 * 提供树拓扑加载、路径加锁/解锁和节点查询功能。
 *
 * 使用流程:
 *   1. treelock_create()             — 创建客户端
 *   2. treelock_load_tree_from_file() — 加载树结构（JSON 文件）
 *   3. treelock_lock_path()           — 按路径加锁
 *   4. treelock_unlock_path()         — 按路径解锁
 *   5. treelock_destroy()             — 销毁客户端
 *
 * 版本: 0.1.0
 * 日期: 2026-06-13
 */

#ifndef TREELOCK_TREE_H
#define TREELOCK_TREE_H

#include "treelock.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * L1: 树结构加载
 * ========================================================================= */

/**
 * 函数名称：treelock_load_tree_from_file
 *
 * 功能描述：从 JSON 文件加载树结构定义
 *
 *          支持两种 JSON 格式：
 *          1. 嵌套格式: { "tree": { "id":1, "label":"/", "children":[...] } }
 *          2. 扁平格式: { "nodes": [{ "id":1, "label":"/", "parent":0 }, ...] }
 *
 *          加载后自动启用协议校验：lock 时将检查是否持有父节点的意向锁。
 *          如果 tl 之前已加载过树，会先清除旧树结构再加载新树。
 *
 * @param[IN] tl       - 锁句柄
 * @param[IN] filepath - JSON 树定义文件路径
 *
 * @return TREELOCK_OK 加载成功
 * @return TREELOCK_ERR_INVAL 文件不存在 / JSON 解析失败 / 树结构不合法
 */
RET_CODE treelock_load_tree_from_file(
    IN treelock_t  *tl,
    IN CSTR_PTR     filepath
);

/**
 * 函数名称：treelock_load_tree_from_string
 *
 * 功能描述：从 JSON 字符串加载树结构定义
 *
 *          适用于嵌入式场景或将配置直接编译进代码的情况。
 *
 * @param[IN] tl          - 锁句柄
 * @param[IN] json_string - JSON 字符串（以 '\0' 结尾）
 *
 * @return TREELOCK_OK 加载成功
 * @return TREELOCK_ERR_INVAL JSON 解析失败或树结构不合法
 */
RET_CODE treelock_load_tree_from_string(
    IN treelock_t  *tl,
    IN CSTR_PTR     json_string
);

/**
 * 函数名称：treelock_register_node
 *
 * 功能描述：以编程方式手动注册单个树节点
 *
 *          适用于动态构建树或不需要 JSON 文件的场景。
 *          parent_id 为 0 表示根节点。
 *          label 可为 NULL（不使用路径查找时）。
 *
 * @param[IN] tl        - 锁句柄
 * @param[IN] node_id   - 节点 ID（必须 > 0）
 * @param[IN] parent_id - 父节点 ID，0 表示根
 * @param[IN] label     - 节点标签（可选，用于路径查找）
 *
 * @return TREELOCK_OK 注册成功
 * @return TREELOCK_ERR_INVAL ID 重复 / parent 不存在 / 内存不足
 */
RET_CODE treelock_register_node(
    IN treelock_t         *tl,
    IN treelock_node_id_t  node_id,
    IN treelock_node_id_t  parent_id,
    IN CSTR_PTR            label
);

/**
 * 函数名称：treelock_tree_unload
 *
 * 功能描述：卸载当前加载的树结构，释放所有树节点内存
 *
 *          卸载后协议校验自动回退为宽松模式（不检查父节点锁）。
 *          未加载树时调用无副作用。
 *
 * @param[IN] tl - 锁句柄
 */
VOID treelock_tree_unload(
    IN treelock_t *tl
);

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
    IN treelock_t *tl
);

/* =========================================================================
 * L2: 路径加锁 / 解锁
 * ========================================================================= */

/**
 * 函数名称：treelock_lock_path
 *
 * 功能描述：按路径加锁，自动沿路径逐级获取意向锁
 *
 *          示例: treelock_lock_path(tl, "/home/alice", TREELOCK_X)
 *                → lock(/, IS) → lock(/home, IX) → lock(/home/alice, X)
 *
 *          祖先节点锁模式自动推导：
 *            - 目标 mode ∈ {IS, S}      → 祖先使用 IS
 *            - 目标 mode ∈ {IX, SIX, X} → 祖先使用 IX
 *
 *          树未加载时，此函数返回 TREELOCK_ERR_INVAL。
 *
 * @param[IN] tl   - 锁句柄
 * @param[IN] path - 以 '/' 分隔的节点 label 路径（如 "/home/alice"）
 * @param[IN] mode - 目标节点的锁模式
 *
 * @return TREELOCK_OK 整条路径加锁成功
 * @return TREELOCK_ERR_INVAL 树未加载 / 路径无效 / 参数无效
 * @return TREELOCK_ERR_PROTOCOL 路径中某节点违反锁协议
 * @return TREELOCK_ERR_TIMEOUT 超时
 */
RET_CODE treelock_lock_path(
    IN treelock_t     *tl,
    IN CSTR_PTR        path,
    IN treelock_mode_t mode
);

/**
 * 函数名称：treelock_unlock_path
 *
 * 功能描述：按路径解锁，自底向上释放路径上所有锁
 *
 *          与 treelock_lock_path 配对使用。
 *          树未加载时，此函数返回 TREELOCK_ERR_INVAL。
 *
 * @param[IN] tl   - 锁句柄
 * @param[IN] path - 路径字符串（与 lock_path 相同）
 *
 * @return TREELOCK_OK 释放成功
 * @return TREELOCK_ERR_INVAL 树未加载 / 路径无效
 */
RET_CODE treelock_unlock_path(
    IN treelock_t  *tl,
    IN CSTR_PTR     path
);

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
 * @param[OUT] parent_id  - 输出：父节点 ID，根节点返回 0
 *
 * @return TREELOCK_OK 查询成功
 * @return TREELOCK_ERR_INVAL 树未加载或节点不存在
 */
RET_CODE treelock_get_parent(
    IN  treelock_t         *tl,
    IN  treelock_node_id_t  node_id,
    OUT treelock_node_id_t *parent_id
);

/**
 * 函数名称：treelock_lookup_path
 *
 * 功能描述：根据路径字符串查找对应的节点 ID
 *
 * @param[IN]  tl       - 锁句柄
 * @param[IN]  path     - 路径字符串（如 "/home/alice"）
 * @param[OUT] node_id  - 输出：节点 ID
 *
 * @return TREELOCK_OK 找到节点
 * @return TREELOCK_ERR_INVAL 树未加载或路径不存在
 */
RET_CODE treelock_lookup_path(
    IN  treelock_t         *tl,
    IN  CSTR_PTR            path,
    OUT treelock_node_id_t *node_id
);

#ifdef __cplusplus
}
#endif

#endif /* TREELOCK_TREE_H */

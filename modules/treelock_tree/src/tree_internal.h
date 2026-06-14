/*
 * TreeLocks — 树结构管理内部头文件
 *
 * 定义树节点元数据、树索引和内部操作接口。
 * 不对外暴露，仅供 treelock_tree 模块内部使用。
 *
 * 版本: 0.1.0
 * 日期: 2026-06-13
 */

#ifndef TREELOCK_TREE_INTERNAL_H
#define TREELOCK_TREE_INTERNAL_H

#include "treelock.h"
#include "treelock_types.h"
#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * 内部常量
 * ========================================================================= */

/** Hash 表桶数（按 node_id 索引） */
#define TREE_HASH_BUCKETS  (256)

/** JSON 树结构最大深度（防止递归栈溢出） */
#define TREE_MAX_DEPTH     (256)

/** 路径字符串最大长度 */
#define TREE_PATH_MAX      (4096)

/** 未找到节点时的哨兵值 */
#define TREE_NODE_NOT_FOUND  ((treelock_node_id_t)(-1))

/** 根节点的父 ID */
#define TREE_ROOT_PARENT    ((treelock_node_id_t)0)

/* =========================================================================
 * 树节点元数据
 * ========================================================================= */

/** 单个树节点的元数据（父子关系、标签、子节点） */
typedef struct treelock_tree_node_s {
    treelock_node_id_t              node_id;    /**< 节点 ID                    */
    treelock_node_id_t              parent_id;  /**< 父节点 ID，TREE_ROOT_PARENT 表示根 */
    CHAR                           *label;      /**< 节点标签（堆分配，可空）     */
    UINT_32                         child_count;/**< 直接子节点数量              */
    UINT_32                         child_cap;  /**< 子节点数组容量              */
    struct treelock_tree_node_s   **children;   /**< 子节点指针数组              */
    struct treelock_tree_node_s    *next;       /**< hash 冲突链表指针           */
} treelock_tree_node_t;

/* =========================================================================
 * 树结构索引
 * ========================================================================= */

/** 树结构索引（挂载在 treelock_s.tree_index） */
typedef struct {
    treelock_tree_node_t  *node_hash[TREE_HASH_BUCKETS];   /**< 按 node_id 索引   */
    treelock_node_id_t     root_id;                        /**< 根节点 ID         */
    UINT_64                node_count;                     /**< 总节点数          */
    INT_32                 loaded;                         /**< TRUE 已加载树结构 */
} treelock_tree_index_t;

/* =========================================================================
 * Hash 表操作
 * ========================================================================= */

/**
 * 函数名称：tree_hash_node_id
 *
 * 功能描述：计算节点 ID 的 hash 桶索引
 *
 * @param[IN] node_id - 节点 ID
 *
 * @return 桶索引 [0, TREE_HASH_BUCKETS)
 */
static inline UINT_32 tree_hash_node_id(
    IN treelock_node_id_t node_id)
{
    return (UINT_32)(node_id % TREE_HASH_BUCKETS);
}

/* =========================================================================
 * 内部 API — 树节点管理
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
    IN CSTR_PTR            label
);

/**
 * 函数名称：tree_node_destroy
 *
 * 功能描述：释放单个树节点及其所有资源
 *
 * @param[IN] node - 树节点指针
 */
VOID tree_node_destroy(
    IN treelock_tree_node_t *node
);

/**
 * 函数名称：tree_node_add_child
 *
 * 功能描述：向父节点添加子节点引用
 *
 * @param[IN] parent - 父节点
 * @param[IN] child  - 子节点
 *
 * @return TREELOCK_OK 成功
 * @return TREELOCK_ERR_INVAL 内存不足
 */
RET_CODE tree_node_add_child(
    IN treelock_tree_node_t *parent,
    IN treelock_tree_node_t *child
);

/* =========================================================================
 * 内部 API — 树索引管理
 * ========================================================================= */

/**
 * 函数名称：tree_index_init
 *
 * 功能描述：初始化树结构索引（所有字段归零）
 *
 * @param[INOUT] idx - 树索引指针
 */
VOID tree_index_init(
    INOUT treelock_tree_index_t *idx
);

/**
 * 函数名称：tree_index_destroy
 *
 * 功能描述：销毁树结构索引，递归释放所有节点内存
 *
 * @param[INOUT] idx - 树索引指针
 */
VOID tree_index_destroy(
    INOUT treelock_tree_index_t *idx
);

/**
 * 函数名称：tree_index_insert
 *
 * 功能描述：向树索引中插入一个节点（同时更新 node_hash 和 label_hash）
 *
 * @param[INOUT] idx  - 树索引指针
 * @param[IN]    node - 要插入的节点
 *
 * @return TREELOCK_OK 成功
 * @return TREELOCK_ERR_INVAL node_id 重复
 */
RET_CODE tree_index_insert(
    INOUT treelock_tree_index_t *idx,
    IN    treelock_tree_node_t  *node
);

/**
 * 函数名称：tree_index_find
 *
 * 功能描述：按节点 ID 查找树节点
 *
 * @param[IN] idx     - 树索引指针
 * @param[IN] node_id - 节点 ID
 *
 * @return 找到返回节点指针，未找到返回 NULL
 */
treelock_tree_node_t *tree_index_find(
    IN treelock_tree_index_t *idx,
    IN treelock_node_id_t     node_id
);

/**
 * 函数名称：tree_index_remove
 *
 * 功能描述：从树索引 hash 表中移除指定节点（不释放内存）
 *
 *          调用者负责释放节点内存。用于回滚操作。
 *
 * @param[INOUT] idx     - 树索引指针
 * @param[IN]    node_id - 要移除的节点 ID
 *
 * @return TRUE 移除成功，FALSE 节点不存在
 */
INT_32 tree_index_remove(
    INOUT treelock_tree_index_t *idx,
    IN    treelock_node_id_t     node_id
);

/**
 * 函数名称：tree_index_find_child_by_label
 *
 * 功能描述：在指定父节点的子节点中查找具有给定标签的子节点
 *
 * @param[IN] parent - 父节点
 * @param[IN] label  - 子节点标签
 *
 * @return 找到返回子节点指针，未找到返回 NULL
 */
treelock_tree_node_t *tree_index_find_child_by_label(
    IN treelock_tree_node_t *parent,
    IN CSTR_PTR              label
);

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
    OUT UINT_64                *count
);

/* =========================================================================
 * 内部 API — JSON 解析（tree_json.c）
 * ========================================================================= */

/**
 * 函数名称：treelock_load_tree_from_string_internal
 *
 * 功能描述：从 JSON 字符串解析并加载树结构到指定索引
 *
 *          组合了解析 + 校验 + 注册三个步骤。
 *
 * @param[INOUT] idx         - 树索引指针
 * @param[IN]    json_string - JSON 字符串
 *
 * @return TREELOCK_OK 加载成功
 * @return TREELOCK_ERR_INVAL 解析/校验/注册失败
 */
RET_CODE treelock_load_tree_from_string_internal(
    INOUT treelock_tree_index_t *idx,
    IN    CSTR_PTR               json_string
);

/* =========================================================================
 * 内部 API — 校验（tree_validate.c）
 * ========================================================================= */

/**
 * 函数名称：treelock_validate_tree_structure
 *
 * 功能描述：对解析出的节点列表执行完整结构校验
 *
 *          检查项：ID 非零、ID 唯一、parent 有效、单根、无环
 *
 * @param[IN]  nodes     - 节点指针数组
 * @param[IN]  count     - 节点数量
 * @param[OUT] idx       - 输出：校验通过后将 root_id 写入 idx->root_id
 *
 * @return TREELOCK_OK 校验通过
 * @return TREELOCK_ERR_INVAL 校验失败
 */
RET_CODE treelock_validate_tree_structure(
    IN    treelock_tree_node_t **nodes,
    IN    UINT_64                count,
    OUT   treelock_tree_index_t *idx
);

/* =========================================================================
 * 内部 API — 路径操作（tree_path.c）
 * ========================================================================= */

/**
 * 函数名称：treelock_resolve_path
 *
 * 功能描述：按路径字符串解析节点 ID 列表（从根到目标）
 *
 * @param[IN]  idx       - 树索引指针
 * @param[IN]  path      - 路径字符串（如 "/home/alice"）
 * @param[OUT] path_ids  - 输出：路径上各节点 ID 数组（调用者 free）
 * @param[OUT] path_len  - 输出：路径长度
 *
 * @return TREELOCK_OK 解析成功
 * @return TREELOCK_ERR_INVAL 树未加载或路径无效
 */
RET_CODE treelock_resolve_path(
    IN  treelock_tree_index_t  *idx,
    IN  CSTR_PTR                path,
    OUT treelock_node_id_t    **path_ids,
    OUT UINT_32                *path_len
);

/**
 * 函数名称：treelock_ancestor_mode_for
 *
 * 功能描述：获取在子节点加锁时祖先节点所需的推荐锁模式
 *
 *          目标 mode ∈ {IS, S}      → 祖先用 IS
 *          目标 mode ∈ {IX, SIX, X} → 祖先用 IX
 *
 * @param[IN] child_mode - 目标节点的锁模式
 *
 * @return 祖先节点推荐的锁模式
 */
treelock_mode_t treelock_ancestor_mode_for(
    IN treelock_mode_t child_mode
);

#ifdef __cplusplus
}
#endif

#endif /* TREELOCK_TREE_INTERNAL_H */

/*
 * TreeLocks - 内部数据结构
 *
 * 此头文件不对外暴露，仅供库内部使用。
 *
 * 版本: 0.1.0
 * 日期: 2026-06-12
 */

#ifndef TREELOCK_INTERNAL_H
#define TREELOCK_INTERNAL_H

#include "treelock.h"
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * 内部常量
 * ========================================================================= */

/** 锁模式总数 */
#define TREELOCK_MODE_COUNT  (TREELOCK_MODE_MAX + 1)

/** 客户端标识最大长度（含 '\0'） */
#define TREELOCK_CLIENT_ID_MAX  (64)

/** 默认租约 TTL（毫秒） */
#define TREELOCK_DEFAULT_LEASE_MS  (30000)

/** 默认心跳间隔（毫秒） */
#define TREELOCK_DEFAULT_HEARTBEAT_MS (10000)

/** 等待队列最大长度 */
#define TREELOCK_WAIT_QUEUE_MAX (1024)

/** 已持有锁列表初始容量 */
#define TREELOCK_HELD_INIT_CAP  (8)

/** 授权锁列表初始容量 */
#define TREELOCK_GRANT_INIT_CAP (4)

/** 等待队列初始容量 */
#define TREELOCK_WAIT_INIT_CAP  (4)

/* =========================================================================
 * 兼容矩阵
 * ========================================================================= */

/**
 * 函数名称：treelock_mode_compatible
 *
 * 功能描述：检查两种锁模式是否兼容
 *
 *          兼容矩阵：
 *                   NL  IS  IX   S  SIX  X
 *            NL  |   Y   Y   Y   Y   Y   Y
 *            IS  |   Y   Y   Y   Y   Y   N
 *            IX  |   Y   Y   Y   N   N   N
 *            S   |   Y   Y   N   Y   N   N
 *            SIX |   Y   Y   N   N   N   N
 *            X   |   Y   N   N   N   N   N
 *
 * @param[IN] existing  - 已持有的锁模式
 * @param[IN] requested - 请求的锁模式
 *
 * @return TRUE 兼容，FALSE 不兼容
 */
INT_32 treelock_mode_compatible(
    IN treelock_mode_t existing,
    IN treelock_mode_t requested
);

/**
 * 函数名称：treelock_required_parent_mode
 *
 * 功能描述：获取在子节点获取指定锁模式时，父节点所需的最小锁模式
 *
 *          请求模式  → 要求父节点模式
 *          IS / S    → IS（或更强）
 *          IX/SIX/X  → IX（或更强）
 *          NL        → NL
 *
 * @param[IN] mode - 要在子节点获取的锁模式
 *
 * @return 父节点所需的最小锁模式
 */
treelock_mode_t treelock_required_parent_mode(
    IN treelock_mode_t mode
);

/**
 * 函数名称：treelock_escalate_valid
 *
 * 功能描述：检查锁升级路径是否合法
 *
 * @param[IN] old_mode - 当前锁模式
 * @param[IN] new_mode - 目标锁模式
 *
 * @return TRUE 升级路径合法，FALSE 不合法
 */
INT_32 treelock_escalate_valid(
    IN treelock_mode_t old_mode,
    IN treelock_mode_t new_mode
);

/**
 * 函数名称：treelock_downgrade_valid
 *
 * 功能描述：检查锁降级路径是否合法
 *
 * @param[IN] old_mode - 当前锁模式
 * @param[IN] new_mode - 目标锁模式
 *
 * @return TRUE 降级路径合法，FALSE 不合法
 */
INT_32 treelock_downgrade_valid(
    IN treelock_mode_t old_mode,
    IN treelock_mode_t new_mode
);

/* =========================================================================
 * 锁条目结构
 * ========================================================================= */

/** 单个锁授予记录 */
typedef struct {
    CHAR             client_id[TREELOCK_CLIENT_ID_MAX]; /**< 客户端标识             */
    treelock_mode_t  mode;                              /**< 锁模式                 */
    TIMESTAMP_MS     lease_expire_at;                   /**< 租约过期时间（ms），-1=永不过期 */
} treelock_grant_t;

/** 等待队列条目 */
typedef struct {
    CHAR             client_id[TREELOCK_CLIENT_ID_MAX]; /**< 客户端标识             */
    treelock_mode_t  requested_mode;                    /**< 请求的锁模式           */
    TIMESTAMP_MS     enqueue_time;                      /**< 入队时间（Unix ms）    */
    pthread_cond_t   cond;                              /**< 条件变量（用于唤醒等待者） */
} treelock_wait_entry_t;

/** 单个节点的锁状态 */
typedef struct treelock_node_s {
    treelock_node_id_t      node_id;         /**< 节点 ID                   */
    treelock_grant_t       *grants;          /**< 已授予的锁列表（动态数组） */
    UINT_64                 grant_count;     /**< 已授予的锁数量             */
    UINT_64                 grant_capacity;  /**< 锁列表容量                 */
    treelock_wait_entry_t  *wait_queue;      /**< 等待队列（动态数组）       */
    UINT_64                 wait_count;      /**< 等待队列当前长度           */
    UINT_64                 wait_capacity;   /**< 等待队列容量               */
    pthread_mutex_t         mutex;           /**< 节点级互斥锁               */
    struct treelock_node_s *next;            /**< 哈希表链表指针             */
} treelock_node_t;

/* =========================================================================
 * 客户端持有的锁记录
 * ========================================================================= */

/** 客户端持有的单个锁记录 */
typedef struct {
    treelock_node_id_t  node_id;       /**< 节点 ID                   */
    treelock_mode_t     mode;          /**< 锁模式                     */
    TIMESTAMP_MS        acquired_at;   /**< 获取时间（Unix ms）        */
    UINT_32             ref_count;     /**< 引用计数（同客户端重入次数） */
} treelock_held_lock_t;

/* =========================================================================
 * 客户端主结构
 * ========================================================================= */

/** 树结构父节点查询回调类型（由 treelock_tree 模块注册） */
typedef treelock_node_id_t (*treelock_tree_get_parent_fn)(
    PTR_VOID              tree_data,
    treelock_node_id_t    node_id
);

/** 树结构销毁回调类型（由 treelock_tree 模块注册） */
typedef VOID (*treelock_tree_destroy_fn)(
    PTR_VOID              tree_data
);

struct treelock_s {
    /* ── 配置 ── */
    treelock_config_t   config;        /**< 客户端配置                 */

    /* ── 锁表 ── */
    treelock_node_t    *lock_table;    /**< 锁表（链表，按 node_id 索引） */
    pthread_mutex_t     table_mutex;   /**< 锁表全局互斥锁             */

    /* ── 已持有的锁 ── */
    treelock_held_lock_t *held_locks;  /**< 已持有锁列表（动态数组）   */
    UINT_64              held_count;   /**< 已持有锁数量               */
    UINT_64              held_capacity;/**< 已持有锁列表容量           */
    pthread_mutex_t      held_mutex;   /**< 持有锁列表互斥锁           */

    /* ── 回调 ── */
    treelock_lost_cb    lost_cb;       /**< 锁丢失回调函数             */
    PTR_VOID            lost_cb_data;  /**< 回调用户数据               */

    /* ── 树结构桥接（由 treelock_tree 模块注入）── */
    PTR_VOID                     tree_data;        /**< 不透明树索引指针    */
    treelock_tree_get_parent_fn  tree_get_parent;  /**< 父节点查询回调      */
    treelock_tree_destroy_fn     tree_destroy;     /**< 树结构销毁回调      */

    /* ── 状态 ── */
    INT_32              connected;     /**< 连接状态（阶段一始终为 TRUE） */
    INT_32              destroyed;     /**< 是否已销毁                 */
};

/* =========================================================================
 * 内部函数声明 — protocol.c
 * ========================================================================= */

/**
 * 函数名称：treelock_validate_protocol
 *
 * 功能描述：验证加锁协议，检查是否违反自顶向下规则
 *
 *          规则 1：获取 S/IS 前，必须持有父节点的 IS/IX 或更强锁
 *          规则 2：获取 X/IX/SIX 前，必须持有父节点的 IX/SIX 或更强锁
 *
 * @param[IN] tl      - 锁句柄
 * @param[IN] node_id - 目标节点 ID
 * @param[IN] mode    - 请求的锁模式
 *
 * @return TREELOCK_OK 协议验证通过
 * @return TREELOCK_ERR_PROTOCOL 协议违反
 */
RET_CODE treelock_validate_protocol(
    IN treelock_t         *tl,
    IN treelock_node_id_t  node_id,
    IN treelock_mode_t     mode
);

/* =========================================================================
 * 内部函数声明 — lock_table.c
 * ========================================================================= */

/**
 * 函数名称：treelock_table_find
 *
 * 功能描述：按节点 ID 查找锁表节点
 *
 * @param[IN] tl      - 锁句柄
 * @param[IN] node_id - 节点 ID
 *
 * @return 找到返回节点指针，未找到返回 NULL
 */
treelock_node_t *treelock_table_find(
    IN treelock_t         *tl,
    IN treelock_node_id_t  node_id
);

/**
 * 函数名称：treelock_table_get_or_create
 *
 * 功能描述：按节点 ID 查找锁表节点，若不存在则创建新节点
 *
 * @param[IN] tl      - 锁句柄
 * @param[IN] node_id - 节点 ID
 *
 * @return 成功返回节点指针，失败（OOM）返回 NULL
 */
treelock_node_t *treelock_table_get_or_create(
    IN treelock_t         *tl,
    IN treelock_node_id_t  node_id
);

/**
 * 函数名称：treelock_table_check_conflict
 *
 * 功能描述：检查请求锁模式与节点上已授予锁是否存在兼容冲突
 *
 * @param[IN] node      - 锁表节点
 * @param[IN] client_id - 请求客户端标识字符串
 * @param[IN] mode      - 请求的锁模式
 *
 * @return TRUE 无冲突可授予，FALSE 存在冲突
 */
INT_32 treelock_table_check_conflict(
    IN treelock_node_t *node,
    IN CSTR_PTR         client_id,
    IN treelock_mode_t  mode
);

/**
 * 函数名称：treelock_table_grant_lock
 *
 * 功能描述：向节点授予锁，将授权记录添加到 grant 列表
 *
 * @param[IN] node      - 锁表节点
 * @param[IN] client_id - 客户端标识字符串
 * @param[IN] mode      - 锁模式
 * @param[IN] lease_ms  - 租约时长（毫秒），<=0 表示永不过期
 *
 * @return TREELOCK_OK 授予成功
 * @return TREELOCK_ERR_INVAL 内存分配失败
 */
RET_CODE treelock_table_grant_lock(
    IN treelock_node_t *node,
    IN CSTR_PTR         client_id,
    IN treelock_mode_t  mode,
    IN INT_64           lease_ms
);

/**
 * 函数名称：treelock_table_release_lock
 *
 * 功能描述：释放节点上指定客户端的锁授权记录
 *
 * @param[IN] node      - 锁表节点
 * @param[IN] client_id - 客户端标识字符串
 * @param[IN] mode      - 要释放的锁模式
 *
 * @return TREELOCK_OK 释放成功
 * @return TREELOCK_ERR_INVAL 未找到对应的锁
 */
RET_CODE treelock_table_release_lock(
    IN treelock_node_t *node,
    IN CSTR_PTR         client_id,
    IN treelock_mode_t  mode
);

/**
 * 函数名称：treelock_table_wake_waiters
 *
 * 功能描述：遍历等待队列，唤醒所有当前可以获取锁的等待者（FIFO 顺序）
 *
 * @param[IN] node - 锁表节点
 */
VOID treelock_table_wake_waiters(
    IN treelock_node_t *node
);

/**
 * 函数名称：treelock_table_add_waiter
 *
 * 功能描述：将请求者加入节点的等待队列
 *
 * @param[IN] node      - 锁表节点
 * @param[IN] client_id - 客户端标识字符串
 * @param[IN] mode      - 请求的锁模式
 *
 * @return TREELOCK_OK 加入成功
 * @return TREELOCK_ERR_INVAL 队列已满或内存不足
 */
RET_CODE treelock_table_add_waiter(
    IN treelock_node_t *node,
    IN CSTR_PTR         client_id,
    IN treelock_mode_t  mode
);

#ifdef __cplusplus
}
#endif

#endif /* TREELOCK_INTERNAL_H */

/*
 * TreeLocks - 分布式多粒度树状锁管理器
 *
 * 公共 API 头文件
 *
 * 版本: 0.1.0
 * 日期: 2026-06-12
 */

#ifndef TREELOCK_H
#define TREELOCK_H

#include "treelock_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * 锁模式枚举
 * ========================================================================= */

/** 锁模式 */
typedef enum {
    TREELOCK_NL  = 0,  /**< No Lock                     无锁          */
    TREELOCK_IS  = 1,  /**< Intention Shared            意向共享      */
    TREELOCK_IX  = 2,  /**< Intention Exclusive         意向排他      */
    TREELOCK_S   = 3,  /**< Shared                      共享          */
    TREELOCK_SIX = 4,  /**< Shared + Intention Exclusive 共享意向排他  */
    TREELOCK_X   = 5,  /**< Exclusive                   排他          */
    TREELOCK_MODE_MAX = TREELOCK_X  /**< 锁模式最大值 */
} treelock_mode_t;

/** 锁模式在存储和传输中的类型 */
typedef INT_32 treelock_mode_int_t;

/** 树节点标识（应用自定义，例如路径哈希或 UUID） */
typedef TREE_NODE_ID treelock_node_id_t;

/** 锁句柄（不透明指针） */
typedef struct treelock_s treelock_t;

/** 客户端配置 */
typedef struct {
    CSTR_PTR     server_addr;  /**< 服务端地址 "ip:port"（阶段二/三使用） */
    INT_32       timeout_ms;   /**< 默认超时（毫秒）                      */
    CSTR_PTR     client_id;    /**< 客户端标识                            */
} treelock_config_t;

/* =========================================================================
 * 返回值（错误码）
 * ========================================================================= */

#define TREELOCK_OK            (0)   /**< 成功                              */
#define TREELOCK_ERR_TIMEOUT   (-1)  /**< 超时未获取到锁                     */
#define TREELOCK_ERR_CONFLICT  (-2)  /**< 兼容冲突                          */
#define TREELOCK_ERR_PROTOCOL  (-3)  /**< 违反加锁协议                      */
#define TREELOCK_ERR_NETWORK   (-4)  /**< 网络/连接错误                     */
#define TREELOCK_ERR_STALE     (-5)  /**< 租约过期，锁已丢失                */
#define TREELOCK_ERR_INVAL     (-6)  /**< 参数无效                          */

/* =========================================================================
 * 回调类型
 * ========================================================================= */

/**
 * 函数名称：treelock_lost_cb
 *
 * 功能描述：锁被抢占时的回调函数（租约过期等异常情况触发）
 *
 * @param[IN] node_id   - 被抢占的节点 ID，TREE_NODE_ID 类型
 * @param[IN] held_mode - 原来持有的锁模式
 * @param[IN] user_data - 用户自定义数据指针
 */
typedef VOID (*treelock_lost_cb)(
    IN  treelock_node_id_t  node_id,
    IN  treelock_mode_t     held_mode,
    IN  PTR_VOID            user_data
);

/* =========================================================================
 * 生命周期
 * ========================================================================= */

/**
 * 函数名称：treelock_create
 *
 * 功能描述：创建锁客户端实例，初始化内部数据结构
 *
 * @param[IN] config - 客户端配置指针（阶段一单机版可为 NULL）
 *
 * @return 成功返回锁句柄指针，失败返回 NULL
 */
treelock_t *treelock_create(
    IN const treelock_config_t *config
);

/**
 * 函数名称：treelock_destroy
 *
 * 功能描述：销毁客户端实例，释放所有持有的锁及内部资源
 *
 * @param[IN] tl - 锁句柄（由 treelock_create 创建）
 */
VOID treelock_destroy(
    IN treelock_t *tl
);

/* =========================================================================
 * 锁操作
 * ========================================================================= */

/**
 * 函数名称：treelock_lock
 *
 * 功能描述：阻塞方式获取指定节点上的锁，直到成功或发生错误
 *
 * @param[IN] tl      - 锁句柄
 * @param[IN] node_id - 目标节点 ID
 * @param[IN] mode    - 请求的锁模式
 *
 * @return TREELOCK_OK 成功获取锁
 * @return TREELOCK_ERR_INVAL 参数无效
 * @return TREELOCK_ERR_CONFLICT 兼容冲突
 * @return TREELOCK_ERR_PROTOCOL 违反加锁协议
 */
RET_CODE treelock_lock(
    IN treelock_t         *tl,
    IN treelock_node_id_t  node_id,
    IN treelock_mode_t     mode
);

/**
 * 函数名称：treelock_try_lock
 *
 * 功能描述：尝试获取锁，在指定超时时间内未获取到则返回超时错误
 *
 * @param[IN] tl         - 锁句柄
 * @param[IN] node_id    - 目标节点 ID
 * @param[IN] mode       - 请求的锁模式
 * @param[IN] timeout_ms - 超时时间（毫秒），0 表示永不超时
 *
 * @return TREELOCK_OK 成功获取锁
 * @return TREELOCK_ERR_TIMEOUT 超时
 * @return TREELOCK_ERR_INVAL 参数无效
 * @return TREELOCK_ERR_CONFLICT 兼容冲突
 */
RET_CODE treelock_try_lock(
    IN treelock_t         *tl,
    IN treelock_node_id_t  node_id,
    IN treelock_mode_t     mode,
    IN INT_32              timeout_ms
);

/**
 * 函数名称：treelock_unlock
 *
 * 功能描述：释放指定节点上的锁，并唤醒等待队列中的下一个兼容请求者
 *
 * @param[IN] tl      - 锁句柄
 * @param[IN] node_id - 目标节点 ID
 *
 * @return TREELOCK_OK 成功释放
 * @return TREELOCK_ERR_INVAL 参数无效或未持有该节点锁
 */
RET_CODE treelock_unlock(
    IN treelock_t         *tl,
    IN treelock_node_id_t  node_id
);

/**
 * 函数名称：treelock_unlock_all
 *
 * 功能描述：释放当前客户端持有的所有锁（按获取的逆序释放）
 *
 * @param[IN] tl - 锁句柄
 *
 * @return TREELOCK_OK 全部释放成功
 * @return TREELOCK_ERR_INVAL 参数无效
 */
RET_CODE treelock_unlock_all(
    IN treelock_t *tl
);

/* =========================================================================
 * 锁升级 / 降级
 * ========================================================================= */

/**
 * 函数名称：treelock_escalate
 *
 * 功能描述：将节点上已有锁升级为更强的锁模式（阻塞等待升级完成）
 *
 *          允许的升级路径：
 *            NL → IS → S
 *            NL → IS → IX → X
 *            NL → IS → IX → SIX → X
 *            IS → IX（需先释放 S）
 *            S  → SIX → X
 *            IX → SIX → X
 *
 * @param[IN] tl       - 锁句柄
 * @param[IN] node_id  - 目标节点 ID
 * @param[IN] new_mode - 新锁模式（必须比当前模式更强）
 *
 * @return TREELOCK_OK 升级成功
 * @return TREELOCK_ERR_INVAL 参数无效
 * @return TREELOCK_ERR_PROTOCOL 升级路径不合法
 */
RET_CODE treelock_escalate(
    IN treelock_t         *tl,
    IN treelock_node_id_t  node_id,
    IN treelock_mode_t     new_mode
);

/**
 * 函数名称：treelock_downgrade
 *
 * 功能描述：将节点上已有锁降级为更弱的锁模式
 *
 *          允许的降级路径：
 *            X → SIX → S 或 IX
 *            X → IS → NL
 *            SIX → S 或 IX
 *
 * @param[IN] tl       - 锁句柄
 * @param[IN] node_id  - 目标节点 ID
 * @param[IN] new_mode - 新锁模式（必须比当前模式更弱）
 *
 * @return TREELOCK_OK 降级成功
 * @return TREELOCK_ERR_INVAL 参数无效
 * @return TREELOCK_ERR_PROTOCOL 降级路径不合法
 */
RET_CODE treelock_downgrade(
    IN treelock_t         *tl,
    IN treelock_node_id_t  node_id,
    IN treelock_mode_t     new_mode
);

/* =========================================================================
 * 查询
 * ========================================================================= */

/**
 * 函数名称：treelock_get_mode
 *
 * 功能描述：查询当前客户端在指定节点上持有的锁模式
 *
 * @param[IN] tl      - 锁句柄
 * @param[IN] node_id - 目标节点 ID
 *
 * @return 当前持有的锁模式，未持有则返回 TREELOCK_NL
 */
treelock_mode_t treelock_get_mode(
    IN treelock_t         *tl,
    IN treelock_node_id_t  node_id
);

/**
 * 函数名称：treelock_query_node
 *
 * 功能描述：查询指定节点上所有客户端持有的锁信息（JSON 格式，调试用）
 *
 * @param[IN]  tl          - 锁句柄
 * @param[IN]  node_id     - 目标节点 ID
 * @param[OUT] json_result - 输出 JSON 字符串（调用者负责 free）
 *
 * @return TREELOCK_OK 查询成功
 * @return TREELOCK_ERR_INVAL 参数无效
 */
RET_CODE treelock_query_node(
    IN  treelock_t         *tl,
    IN  treelock_node_id_t  node_id,
    OUT CHAR               **json_result
);

/* =========================================================================
 * 回调设置
 * ========================================================================= */

/**
 * 函数名称：treelock_set_lost_callback
 *
 * 功能描述：设置锁丢失时的回调函数
 *
 * @param[IN] tl        - 锁句柄
 * @param[IN] cb        - 回调函数指针
 * @param[IN] user_data - 用户自定义数据（回调时透传）
 *
 * @return TREELOCK_OK 设置成功
 * @return TREELOCK_ERR_INVAL 参数无效
 */
RET_CODE treelock_set_lost_callback(
    IN treelock_t        *tl,
    IN treelock_lost_cb   cb,
    IN PTR_VOID           user_data
);

/* =========================================================================
 * 工具函数
 * ========================================================================= */

/**
 * 函数名称：treelock_mode_name
 *
 * 功能描述：获取锁模式的字符串名称
 *
 * @param[IN] mode - 锁模式
 *
 * @return 字符串字面量（如 "IS"、"X" 等），非法值返回 "UNKNOWN"
 */
CSTR_PTR treelock_mode_name(
    IN treelock_mode_t mode
);

/**
 * 函数名称：treelock_strerror
 *
 * 功能描述：将错误码转换为可读的错误描述字符串
 *
 * @param[IN] err - 错误码（TREELOCK_OK 或各类 TREELOCK_ERR_*）
 *
 * @return 错误描述字符串
 */
CSTR_PTR treelock_strerror(
    IN RET_CODE err
);

#ifdef __cplusplus
}
#endif

#endif /* TREELOCK_H */

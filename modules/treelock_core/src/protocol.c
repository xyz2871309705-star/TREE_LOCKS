/*
 * TreeLocks - 协议层实现
 *
 * 包含兼容矩阵、模式转换、锁升级/降级路径验证等纯逻辑。
 *
 * 版本: 0.1.0
 * 日期: 2026-06-12
 */

#include "internal.h"
#include <string.h> /* strcmp, strlen */

/* =========================================================================
 * 静态数据 — 兼容矩阵
 *
 * 兼容矩阵 treelock_compat_matrix[已持有][请求]：
 *   1 = 兼容，0 = 不兼容
 *
 *               NL  IS  IX   S  SIX  X
 *      NL   |   1   1   1   1   1   1
 *      IS   |   1   1   1   1   1   0
 *      IX   |   1   1   1   0   0   0
 *      S    |   1   1   0   1   0   0
 *      SIX  |   1   1   0   0   0   0
 *      X    |   1   0   0   0   0   0
 * ========================================================================= */

static const INT_32 g_compat_matrix[TREELOCK_MODE_COUNT][TREELOCK_MODE_COUNT] = {
    /* 请求方 →   NL   IS   IX    S  SIX    X   ↓ 已持有方 */
    /* NL  */ {   1,   1,   1,   1,   1,   1 },
    /* IS  */ {   1,   1,   1,   1,   1,   0 },
    /* IX  */ {   1,   1,   1,   0,   0,   0 },
    /* S   */ {   1,   1,   0,   1,   0,   0 },
    /* SIX */ {   1,   1,   0,   0,   0,   0 },
    /* X   */ {   1,   0,   0,   0,   0,   0 },
};

/*
 * 在节点 N 上获取指定锁模式时，需要在父节点持有的最小锁模式：
 *
 *   请求模式       →  要求的父节点模式
 *   NL             →  NL
 *   IS / S         →  IS（或更强）
 *   IX / X / SIX   →  IX（或更强）
 */
static const treelock_mode_t g_required_parent[TREELOCK_MODE_COUNT] = {
    TREELOCK_NL,   /* NL  → NL   */
    TREELOCK_IS,   /* IS  → IS   */
    TREELOCK_IX,   /* IX  → IX   */
    TREELOCK_IS,   /* S   → IS   */
    TREELOCK_IX,   /* SIX → IX   */
    TREELOCK_IX,   /* X   → IX   */
};

/*
 * 锁升级合法路径表 g_upgrade_valid[旧模式][新模式]：
 *   1 = 允许升级，0 = 不允许
 *
 *   NL → IS, NL → IX, NL → S, NL → SIX, NL → X
 *   IS → S, IS → IX, IS → SIX, IS → X
 *   IX → SIX, IX → X
 *   S → SIX, S → X
 *   SIX → X
 */
static const INT_32 g_upgrade_valid[TREELOCK_MODE_COUNT][TREELOCK_MODE_COUNT] = {
    /* 新 →     NL   IS   IX    S  SIX    X   ↓ 旧 */
    /* NL  */ { 0,   1,   1,   1,   1,   1 },
    /* IS  */ { 0,   0,   1,   1,   1,   1 },
    /* IX  */ { 0,   0,   0,   0,   1,   1 },
    /* S   */ { 0,   0,   0,   0,   1,   1 },
    /* SIX */ { 0,   0,   0,   0,   0,   1 },
    /* X   */ { 0,   0,   0,   0,   0,   0 },
};

/*
 * 锁降级合法路径表 g_downgrade_valid[旧模式][新模式]：
 *   1 = 允许降级，0 = 不允许
 */
static const INT_32 g_downgrade_valid[TREELOCK_MODE_COUNT][TREELOCK_MODE_COUNT] = {
    /* 新 →     NL   IS   IX    S  SIX    X   ↓ 旧 */
    /* NL  */ { 0,   0,   0,   0,   0,   0 },
    /* IS  */ { 1,   0,   0,   0,   0,   0 },
    /* IX  */ { 1,   1,   0,   0,   0,   0 },
    /* S   */ { 1,   1,   0,   0,   0,   0 },
    /* SIX */ { 1,   1,   1,   1,   0,   0 },
    /* X   */ { 1,   1,   1,   1,   1,   0 },
};

/* =========================================================================
 * 公共函数实现
 * ========================================================================= */

/**
 * 函数名称：treelock_mode_compatible
 *
 * 功能描述：检查两种锁模式是否兼容
 *
 * @param[IN] existing  - 已持有的锁模式
 * @param[IN] requested - 请求的锁模式
 *
 * @return TRUE 兼容，FALSE 不兼容或参数越界
 */
INT_32 treelock_mode_compatible(
    IN treelock_mode_t existing,
    IN treelock_mode_t requested)
{
    if (existing > TREELOCK_MODE_MAX || requested > TREELOCK_MODE_MAX) {
        return FALSE;
    }
    return g_compat_matrix[existing][requested];
}

/**
 * 函数名称：treelock_required_parent_mode
 *
 * 功能描述：获取在子节点获取指定锁模式时，父节点所需的最小锁模式
 *
 * @param[IN] mode - 要在子节点获取的锁模式
 *
 * @return 父节点所需的最小锁模式
 */
treelock_mode_t treelock_required_parent_mode(
    IN treelock_mode_t mode)
{
    if (mode > TREELOCK_MODE_MAX) {
        return TREELOCK_NL;
    }
    return g_required_parent[mode];
}

/**
 * 函数名称：treelock_escalate_valid
 *
 * 功能描述：检查锁升级路径是否合法
 *
 *          相同模式不需要升级，直接返回 FALSE。
 *
 * @param[IN] old_mode - 当前锁模式
 * @param[IN] new_mode - 目标锁模式
 *
 * @return TRUE 升级路径合法，FALSE 不合法或参数越界
 */
INT_32 treelock_escalate_valid(
    IN treelock_mode_t old_mode,
    IN treelock_mode_t new_mode)
{
    if (old_mode > TREELOCK_MODE_MAX || new_mode > TREELOCK_MODE_MAX) {
        return FALSE;
    }
    if (old_mode == new_mode) {
        return FALSE; /* 相同模式不需要升级 */
    }
    return g_upgrade_valid[old_mode][new_mode];
}

/**
 * 函数名称：treelock_downgrade_valid
 *
 * 功能描述：检查锁降级路径是否合法
 *
 *          相同模式不需要降级，直接返回 FALSE。
 *
 * @param[IN] old_mode - 当前锁模式
 * @param[IN] new_mode - 目标锁模式
 *
 * @return TRUE 降级路径合法，FALSE 不合法或参数越界
 */
INT_32 treelock_downgrade_valid(
    IN treelock_mode_t old_mode,
    IN treelock_mode_t new_mode)
{
    if (old_mode > TREELOCK_MODE_MAX || new_mode > TREELOCK_MODE_MAX) {
        return FALSE;
    }
    if (old_mode == new_mode) {
        return FALSE; /* 相同模式不需要降级 */
    }
    return g_downgrade_valid[old_mode][new_mode];
}

/**
 * 函数名称：treelock_mode_name
 *
 * 功能描述：获取锁模式的字符串名称
 *
 * @param[IN] mode - 锁模式
 *
 * @return 字符串字面量指针，非法值返回 "UNKNOWN"
 */
CSTR_PTR treelock_mode_name(
    IN treelock_mode_t mode)
{
    static const CHAR *names[] = {
        "NL", "IS", "IX", "S", "SIX", "X"
    };
    if (mode > TREELOCK_MODE_MAX) {
        return "UNKNOWN";
    }
    return names[mode];
}

/**
 * 函数名称：treelock_strerror
 *
 * 功能描述：将错误码转换为可读的错误描述字符串
 *
 * @param[IN] err - 错误码（TREELOCK_OK 或 TREELOCK_ERR_*）
 *
 * @return 错误描述字符串字面量
 */
CSTR_PTR treelock_strerror(
    IN RET_CODE err)
{
    switch (err) {
    case TREELOCK_OK:            return "Success";
    case TREELOCK_ERR_TIMEOUT:   return "Timeout waiting for lock";
    case TREELOCK_ERR_CONFLICT:  return "Lock mode conflict";
    case TREELOCK_ERR_PROTOCOL:  return "Lock protocol violation";
    case TREELOCK_ERR_NETWORK:   return "Network/connection error";
    case TREELOCK_ERR_STALE:     return "Lease expired, lock lost";
    case TREELOCK_ERR_INVAL:     return "Invalid argument";
    default:                     return "Unknown error";
    }
}

/**
 * 函数名称：treelock_validate_protocol
 *
 * 功能描述：验证加锁协议，检查是否违反自顶向下规则
 *
 *          阶段一简化实现：仅对 node_id == 0（约定根节点）做特殊处理。
 *          未来版本将通过 tree_map 查找 parent_id 并验证祖先锁。
 *
 * @param[IN] tl      - 锁句柄
 * @param[IN] node_id - 目标节点 ID
 * @param[IN] mode    - 请求的锁模式
 *
 * @return TREELOCK_OK 协议验证通过
 */
RET_CODE treelock_validate_protocol(
    IN treelock_t         *tl,
    IN treelock_node_id_t  node_id,
    IN treelock_mode_t     mode)
{
    UNUSED_PARAM(tl);
    UNUSED_PARAM(mode);

    /* 根节点（node_id == 0 约定）无需父节点锁 */
    if (node_id == 0) {
        return TREELOCK_OK;
    }

    /*
     * TODO(阶段二): 通过 tree_map 查找 parent_id，验证是否持有父节点的意向锁。
     * 阶段一由调用者保证加锁顺序，库仅做基本检查。
     */
    return TREELOCK_OK;
}

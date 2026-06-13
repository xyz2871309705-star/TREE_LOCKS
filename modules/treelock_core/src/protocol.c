/*
 * TreeLocks - 协议层实现
 *
 * 包含兼容矩阵、模式转换、锁升级/降级路径验证等纯逻辑。
 *
 * 版本: 0.1.0
 * 日期: 2026-06-12
 */

#include "internal.h"
#include "treelock_log.h"
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
 *          规则 1：获取 S/IS 前，必须持有父节点的 IS 或更强锁
 *          规则 2：获取 X/IX/SIX 前，必须持有父节点的 IX 或更强锁
 *
 *          如果未加载树结构（tree_data == NULL），则跳过校验，向后兼容。
 *
 * @param[IN] tl      - 锁句柄
 * @param[IN] node_id - 目标节点 ID
 * @param[IN] mode    - 请求的锁模式
 *
 * @return TREELOCK_OK 协议验证通过
 * @return TREELOCK_ERR_PROTOCOL 违反加锁协议
 */
RET_CODE treelock_validate_protocol(
    IN treelock_t         *tl,
    IN treelock_node_id_t  node_id,
    IN treelock_mode_t     mode)
{
    treelock_node_id_t parent_id;
    treelock_mode_t    required;
    treelock_mode_t    held;

    if (tl == NULL) {
        return TREELOCK_ERR_INVAL;
    }

    /* 未加载树结构 → 跳过校验（向后兼容） */
    if (tl->tree_data == NULL || tl->tree_get_parent == NULL) {
        return TREELOCK_OK;
    }

    /* 查询父节点 */
    parent_id = tl->tree_get_parent(tl->tree_data, node_id);

    /* 根节点无需父节点锁 */
    if (parent_id == (treelock_node_id_t)0) {
        return TREELOCK_OK;
    }

    /* 获取父节点所需的最小锁模式 */
    required = treelock_required_parent_mode(mode);
    if (required == TREELOCK_NL) {
        return TREELOCK_OK;
    }

    /* 检查父节点是否持有足够的锁 */
    held = treelock_get_mode(tl, parent_id);

    /*
     * 父节点锁强度检查：
     *   要求 IS → 父节点持有 NL 以外任何锁即可
     *   要求 IX → 父节点必须持有 IX / SIX / X
     */
    if (required == TREELOCK_IS) {
        if (held == TREELOCK_NL) {
            TREELOCK_LOG_WARN("PROTO",
                "protocol violation: lock node=%llu (mode=%s) requires "
                "parent=%llu to hold IS or stronger, but parent holds NL",
                (unsigned long long)node_id, treelock_mode_name(mode),
                (unsigned long long)parent_id);
            return TREELOCK_ERR_PROTOCOL;
        }
    } else if (required == TREELOCK_IX) {
        if (held != TREELOCK_IX && held != TREELOCK_SIX && held != TREELOCK_X) {
            TREELOCK_LOG_WARN("PROTO",
                "protocol violation: lock node=%llu (mode=%s) requires "
                "parent=%llu to hold IX or stronger, but parent holds %s",
                (unsigned long long)node_id, treelock_mode_name(mode),
                (unsigned long long)parent_id, treelock_mode_name(held));
            return TREELOCK_ERR_PROTOCOL;
        }
    }

    return TREELOCK_OK;
}

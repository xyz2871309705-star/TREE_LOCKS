/*
 * TreeLocks - 锁表实现
 *
 * 管理按节点索引的锁状态表、等待队列、锁授予与释放逻辑。
 * 阶段一使用内存链表（单链表），后续可平滑升级为 uthash。
 *
 * 版本: 0.1.0
 * 日期: 2026-06-12
 */

#include "internal.h"
#include "treelock_log.h"
#include "treelock_platform.h"
#include <stdlib.h> /* malloc, free, realloc */
#include <string.h> /* strncpy */

/* =========================================================================
 * 内部辅助函数
 * ========================================================================= */

/**
 * 函数名称：_current_time_ms
 *
 * 功能描述：获取当前 Unix 时间戳（毫秒精度）
 *
 *          委托平台抽象层：Windows → GetSystemTimeAsFileTime,
 *                         Linux   → gettimeofday
 *
 * @return 当前毫秒时间戳
 */
static TIMESTAMP_MS _current_time_ms(VOID)
{
    return treelock_platform_time_ms();
}

/**
 * 函数名称：_str_copy_safe
 *
 * 功能描述：安全拷贝字符串到固定长度缓冲区，确保 '\0' 结尾
 *
 * @param[OUT] dest      - 目标缓冲区，长度 TREELOCK_CLIENT_ID_MAX
 * @param[IN]  src       - 源字符串
 * @param[IN]  dest_size - 目标缓冲区大小
 */
static VOID _str_copy_safe(
    OUT CHAR   *dest,
    IN  CSTR_PTR src,
    IN  UINT_32  dest_size)
{
    if (dest == NULL || src == NULL || dest_size == 0) {
        return;
    }
    strncpy(dest, src, dest_size - 1);
    dest[dest_size - 1] = '\0';
}

/* =========================================================================
 * 公共函数实现
 * ========================================================================= */

/**
 * 函数名称：treelock_table_find
 *
 * 功能描述：按节点 ID 在锁表链表中查找节点
 *
 * @param[IN] tl      - 锁句柄
 * @param[IN] node_id - 节点 ID
 *
 * @return 找到返回节点指针，未找到返回 NULL
 */
treelock_node_t *treelock_table_find(
    IN treelock_t         *tl,
    IN treelock_node_id_t  node_id)
{
    treelock_node_t *node;

    if (tl == NULL) {
        return NULL;
    }

    node = tl->lock_table;
    while (node != NULL) {
        if (node->node_id == node_id) {
            return node;
        }
        node = node->next;
    }
    return NULL;
}

/**
 * 函数名称：treelock_table_get_or_create
 *
 * 功能描述：按节点 ID 查找锁表节点，若不存在则分配并初始化新节点
 *
 *          新节点插入链表头部以加速热点访问。
 *
 * @param[IN] tl      - 锁句柄
 * @param[IN] node_id - 节点 ID
 *
 * @return 成功返回节点指针，内存不足返回 NULL
 */
treelock_node_t *treelock_table_get_or_create(
    IN treelock_t         *tl,
    IN treelock_node_id_t  node_id)
{
    treelock_node_t *node;

    if (tl == NULL) {
        return NULL;
    }

    node = treelock_table_find(tl, node_id);
    if (node != NULL) {
        return node;
    }

    /* 分配新节点并零初始化 */
    node = (treelock_node_t *)calloc(1, sizeof(treelock_node_t));
    if (node == NULL) {
        TREELOCK_LOG_ERROR("TABLE",
            "failed to allocate node for node_id=%llu (OOM)",
            (unsigned long long)node_id);
        return NULL;
    }

    node->node_id = node_id;

    /* 初始化节点级互斥锁 */
    if (pthread_mutex_init(&node->mutex, NULL) != 0) {
        TREELOCK_LOG_ERROR("TABLE",
            "failed to init mutex for node_id=%llu",
            (unsigned long long)node_id);
        free(node);
        return NULL;
    }

    /* 插入链表头部 */
    node->next = tl->lock_table;
    tl->lock_table = node;

    TREELOCK_LOG_DEBUG("TABLE", "new node created: node_id=%llu",
                       (unsigned long long)node_id);
    return node;
}

/**
 * 函数名称：treelock_table_check_conflict
 *
 * 功能描述：检查请求锁模式是否与节点上所有已授予锁兼容
 *
 *          遍历规则：
 *          - 跳过同一客户端的已授予锁（自身不冲突）
 *          - 跳过已过期的租约（视为无效锁）
 *          - 检查兼容矩阵
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
    IN treelock_mode_t  mode)
{
    UINT_64 i;

    if (node == NULL || client_id == NULL) {
        return FALSE;
    }

    for (i = 0; i < node->grant_count; i++) {
        treelock_grant_t *grant = &node->grants[i];

        /* 跳过同一客户端 */
        if (strcmp(grant->client_id, client_id) == EQUAL) {
            continue;
        }

        /* 检查租约过期（永不过期 = -1） */
        if (grant->lease_expire_at > 0 &&
            _current_time_ms() >= grant->lease_expire_at) {
            TREELOCK_LOG_TRACE("TABLE",
                "expired lease ignored: node=%llu mode=%s client=%s",
                (unsigned long long)node->node_id,
                treelock_mode_name(grant->mode),
                grant->client_id);
            continue; /* 过期锁视为不存在 */
        }

        /* 兼容性检查 */
        if (!treelock_mode_compatible(grant->mode, mode)) {
            TREELOCK_LOG_TRACE("TABLE",
                "conflict: existing=%s requested=%s client=%s on node=%llu",
                treelock_mode_name(grant->mode),
                treelock_mode_name(mode),
                grant->client_id,
                (unsigned long long)node->node_id);
            return FALSE; /* 冲突 */
        }
    }
    return TRUE; /* 无冲突 */
}

/**
 * 函数名称：treelock_table_grant_lock
 *
 * 功能描述：将锁授权记录添加到节点的 grant 列表中
 *
 *          自动扩展动态数组（初始容量 4，翻倍增长）。
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
    IN INT_64           lease_ms)
{
    if (node == NULL || client_id == NULL) {
        return TREELOCK_ERR_INVAL;
    }

    /* 扩展 grants 数组 */
    if (node->grant_count >= node->grant_capacity) {
        UINT_64 new_cap;
        treelock_grant_t *new_grants;

        new_cap = (node->grant_capacity == 0)
                  ? TREELOCK_GRANT_INIT_CAP
                  : node->grant_capacity * 2;

        new_grants = (treelock_grant_t *)realloc(
            node->grants, (size_t)(new_cap * sizeof(treelock_grant_t)));
        if (new_grants == NULL) {
            TREELOCK_LOG_ERROR("TABLE",
                "failed to expand grants: node=%llu cap=%llu→%llu (OOM)",
                (unsigned long long)node->node_id,
                (unsigned long long)node->grant_capacity,
                (unsigned long long)new_cap);
            return TREELOCK_ERR_INVAL; /* OOM */
        }
        node->grants = new_grants;
        node->grant_capacity = new_cap;
    }

    /* 填充授权记录 */
    {
        treelock_grant_t *grant = &node->grants[node->grant_count];
        _str_copy_safe(grant->client_id, client_id, TREELOCK_CLIENT_ID_MAX);
        grant->mode = mode;
        grant->lease_expire_at = (lease_ms > 0)
                                 ? (_current_time_ms() + lease_ms)
                                 : (TIMESTAMP_MS)(-1);
    }

    node->grant_count++;

    TREELOCK_LOG_TRACE("TABLE", "grant: node=%llu mode=%s client=%s",
                       (unsigned long long)node->node_id,
                       treelock_mode_name(mode), client_id);
    return TREELOCK_OK;
}

/**
 * 函数名称：treelock_table_release_lock
 *
 * 功能描述：从节点的 grant 列表中移除指定客户端的锁授权
 *
 *          使用 swap-remove 策略（将最后一个元素移到删除位置）以提高效率。
 *
 * @param[IN] node      - 锁表节点
 * @param[IN] client_id - 客户端标识字符串
 * @param[IN] mode      - 要释放的锁模式
 *
 * @return TREELOCK_OK 释放成功
 * @return TREELOCK_ERR_INVAL 未找到匹配的锁
 */
RET_CODE treelock_table_release_lock(
    IN treelock_node_t *node,
    IN CSTR_PTR         client_id,
    IN treelock_mode_t  mode)
{
    UINT_64 i;

    if (node == NULL || client_id == NULL) {
        return TREELOCK_ERR_INVAL;
    }

    for (i = 0; i < node->grant_count; i++) {
        treelock_grant_t *grant = &node->grants[i];

        if (strcmp(grant->client_id, client_id) == EQUAL &&
            grant->mode == mode) {
            /* swap-remove：将最后一个元素移到当前位置 */
            if (i < node->grant_count - 1) {
                node->grants[i] = node->grants[node->grant_count - 1];
            }
            node->grant_count--;
            TREELOCK_LOG_TRACE("TABLE",
                "lock released from grants: node=%llu mode=%s client=%s "
                "(grant_count=%llu)",
                (unsigned long long)node->node_id,
                treelock_mode_name(mode), client_id,
                (unsigned long long)node->grant_count);
            return TREELOCK_OK;
        }
    }
    return TREELOCK_ERR_INVAL; /* 未找到该锁 */
}

/**
 * 函数名称：treelock_table_add_waiter
 *
 * 功能描述：将请求者加入节点的等待队列
 *
 *          自动扩展动态数组（初始容量 4，翻倍增长）。
 *          初始化条件变量用于后续唤醒。
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
    IN treelock_mode_t  mode)
{
    if (node == NULL || client_id == NULL) {
        return TREELOCK_ERR_INVAL;
    }

    /* 扩展队列 */
    if (node->wait_count >= node->wait_capacity) {
        UINT_64 new_cap;
        treelock_wait_entry_t *new_queue;

        new_cap = (node->wait_capacity == 0)
                  ? TREELOCK_WAIT_INIT_CAP
                  : node->wait_capacity * 2;

        new_queue = (treelock_wait_entry_t *)realloc(
            node->wait_queue,
            (size_t)(new_cap * sizeof(treelock_wait_entry_t)));
        if (new_queue == NULL) {
            TREELOCK_LOG_ERROR("TABLE",
                "failed to expand wait queue: node=%llu cap=%llu→%llu (OOM)",
                (unsigned long long)node->node_id,
                (unsigned long long)node->wait_capacity,
                (unsigned long long)new_cap);
            return TREELOCK_ERR_INVAL;
        }
        node->wait_queue = new_queue;
        node->wait_capacity = new_cap;
    }

    /* 添加等待者 */
    {
        treelock_wait_entry_t *entry = &node->wait_queue[node->wait_count];
        _str_copy_safe(entry->client_id, client_id, TREELOCK_CLIENT_ID_MAX);
        entry->requested_mode = mode;
        entry->enqueue_time   = _current_time_ms();

        if (pthread_cond_init(&entry->cond, NULL) != 0) {
            TREELOCK_LOG_ERROR("TABLE",
                "failed to init cond for waiter: node=%llu client=%s",
                (unsigned long long)node->node_id, client_id);
            return TREELOCK_ERR_INVAL;
        }
    }

    node->wait_count++;
    TREELOCK_LOG_TRACE("TABLE",
        "waiter added: node=%llu mode=%s client=%s "
        "(wait_count=%llu wait_capacity=%llu)",
        (unsigned long long)node->node_id,
        treelock_mode_name(mode), client_id,
        (unsigned long long)node->wait_count,
        (unsigned long long)node->wait_capacity);
    return TREELOCK_OK;
}

/**
 * 函数名称：treelock_table_wake_waiters
 *
 * 功能描述：遍历等待队列，唤醒所有当前无冲突的等待者
 *
 *          使用 FIFO 遍历策略：
 *          - 从头扫描，对每个 waiter 检查冲突
 *          - 无冲突 → 授予锁 + 条件变量 signal + swap-remove
 *          - 有冲突 → 保留在队列中继续扫描下一个
 *
 * @param[IN] node - 锁表节点
 */
VOID treelock_table_wake_waiters(
    IN treelock_node_t *node)
{
    UINT_64 i;

    if (node == NULL) {
        return;
    }

    for (i = 0; i < node->wait_count; /* 循环内更新 */) {
        treelock_wait_entry_t *entry = &node->wait_queue[i];

        if (treelock_table_check_conflict(node, entry->client_id,
                                           entry->requested_mode)) {
            /* 无冲突 → 授予锁 */
            treelock_table_grant_lock(node, entry->client_id,
                                       entry->requested_mode,
                                       TREELOCK_DEFAULT_LEASE_MS);

            /* 唤醒等待者 */
            TREELOCK_LOG_TRACE("TABLE",
                "waking waiter: node=%llu mode=%s client=%s",
                (unsigned long long)node->node_id,
                treelock_mode_name(entry->requested_mode),
                entry->client_id);
            pthread_cond_signal(&entry->cond);
            /*
             * 注意：不在此处 pthread_cond_destroy，因为被唤醒的线程
             * 可能尚未从 pthread_cond_wait 完全返回（仍在等待重新获取
             * node->mutex）。cond 由 _do_lock_core 中被唤醒的线程在
             * 成功获取锁后负责销毁（或超时路径自行销毁）。
             * 未使用的 cond 在 treelock_destroy 的清理循环中释放。
             */

            /* swap-remove 从队列中移除（逐字段拷贝，避免 pthread_cond_t 整体赋值） */
            if (i < node->wait_count - 1) {
                /*
                 * 销毁队列末尾将被移出的条目的 cond ——
                 * 其数据将被拷贝到位置 i，原 cond 不再被任何 waiter 使用。
                 * 不销毁会导致下次 add_waiter 在该槽位重复 pthread_cond_init（UB）。
                 */
                pthread_cond_destroy(
                    &node->wait_queue[node->wait_count - 1].cond);
                memcpy(node->wait_queue[i].client_id,
                       node->wait_queue[node->wait_count - 1].client_id,
                       TREELOCK_CLIENT_ID_MAX);
                node->wait_queue[i].requested_mode =
                    node->wait_queue[node->wait_count - 1].requested_mode;
                node->wait_queue[i].enqueue_time =
                    node->wait_queue[node->wait_count - 1].enqueue_time;
                /*
                 * cond 不拷贝：保留位置 i 原有的 cond（已由 _do_lock_core
                 * 中被唤醒的线程负责销毁，或由 treelock_destroy 清理）。
                 */
            }
            node->wait_count--;
            /* 不递增 i：当前位置已替换为新元素，需重新检查 */
        } else {
            i++; /* 保留在队列中，继续扫描下一个 */
        }
    }
}

/*
 * TreeLocks - 客户端 API 实现
 *
 * 提供线程安全的锁操作接口，管理锁表交互和客户端本地状态。
 *
 * 版本: 0.1.0
 * 日期: 2026-06-12
 */

#include "internal.h"
#include "treelock_log.h"
#include "treelock_platform.h"
#include <stdlib.h> /* malloc, free, realloc */
#include <string.h> /* strncpy */
#include <stdio.h>  /* snprintf */
#include <errno.h>  /* ETIMEDOUT */

/* =========================================================================
 * 内部辅助 — 时间
 * ========================================================================= */

/**
 * 函数名称：_current_time_ms
 *
 * 功能描述：获取当前 Unix 时间戳（毫秒精度）
 *
 * @return 当前毫秒时间戳
 */
static TIMESTAMP_MS _current_time_ms(VOID)
{
    return treelock_platform_time_ms();
}

/* =========================================================================
 * 内部辅助 — 已持有锁管理
 * ========================================================================= */

/**
 * 函数名称：_find_held_lock
 *
 * 功能描述：在客户端已持有锁列表中查找指定节点的锁记录
 *
 *          调用前必须已持有 held_mutex。
 *
 * @param[IN] tl      - 锁句柄
 * @param[IN] node_id - 节点 ID
 *
 * @return 找到返回记录指针，未找到返回 NULL
 */
static treelock_held_lock_t *_find_held_lock(
    IN treelock_t         *tl,
    IN treelock_node_id_t  node_id)
{
    UINT_64 i;

    if (tl == NULL) {
        return NULL;
    }

    for (i = 0; i < tl->held_count; i++) {
        if (tl->held_locks[i].node_id == node_id) {
            return &tl->held_locks[i];
        }
    }
    return NULL;
}

/**
 * 函数名称：_add_held_lock
 *
 * 功能描述：向已持有锁列表中添加一条记录
 *
 *          自动扩展动态数组（初始容量 8，翻倍增长）。
 *          调用者负责外围同步。
 *
 * @param[IN] tl      - 锁句柄
 * @param[IN] node_id - 节点 ID
 * @param[IN] mode    - 锁模式
 *
 * @return TREELOCK_OK 添加成功
 * @return TREELOCK_ERR_INVAL 内存不足
 */
static RET_CODE _add_held_lock(
    IN treelock_t         *tl,
    IN treelock_node_id_t  node_id,
    IN treelock_mode_t     mode)
{
    if (tl == NULL) {
        return TREELOCK_ERR_INVAL;
    }

    pthread_mutex_lock(&tl->held_mutex);

    /* 扩展数组 */
    if (tl->held_count >= tl->held_capacity) {
        UINT_64 new_cap;
        treelock_held_lock_t *new_held;

        new_cap = (tl->held_capacity == 0)
                  ? TREELOCK_HELD_INIT_CAP
                  : tl->held_capacity * 2;

        new_held = (treelock_held_lock_t *)realloc(
            tl->held_locks,
            (size_t)(new_cap * sizeof(treelock_held_lock_t)));
        if (new_held == NULL) {
            pthread_mutex_unlock(&tl->held_mutex);
            TREELOCK_LOG_ERROR("CORE",
                "failed to expand held_locks from %llu to %llu (OOM)",
                (unsigned long long)tl->held_capacity,
                (unsigned long long)new_cap);
            return TREELOCK_ERR_INVAL;
        }
        tl->held_locks = new_held;
        tl->held_capacity = new_cap;
    }

    tl->held_locks[tl->held_count].node_id     = node_id;
    tl->held_locks[tl->held_count].mode        = mode;
    tl->held_locks[tl->held_count].acquired_at = _current_time_ms();
    tl->held_locks[tl->held_count].ref_count   = 1;
    tl->held_count++;

    pthread_mutex_unlock(&tl->held_mutex);
    return TREELOCK_OK;
}

/**
 * 函数名称：_remove_held_lock
 *
 * 功能描述：释放已持有锁的一次引用（引用计数 -1）
 *
 *          当引用计数归零时，从列表中移除该锁记录。
 *          此设计支持同客户端重入加锁。
 *
 * @param[IN] tl      - 锁句柄
 * @param[IN] node_id - 节点 ID
 *
 * @return TREELOCK_OK 移除/减计数成功
 * @return TREELOCK_ERR_INVAL 未找到该节点
 */
static RET_CODE _remove_held_lock(
    IN treelock_t         *tl,
    IN treelock_node_id_t  node_id)
{
    UINT_64 i;

    if (tl == NULL) {
        return TREELOCK_ERR_INVAL;
    }

    pthread_mutex_lock(&tl->held_mutex);

    for (i = 0; i < tl->held_count; i++) {
        if (tl->held_locks[i].node_id == node_id) {
            /* 引用计数减 1，仅当归零时才真正移除 */
            if (tl->held_locks[i].ref_count > 1) {
                tl->held_locks[i].ref_count--;
                pthread_mutex_unlock(&tl->held_mutex);
                return TREELOCK_OK;
            }

            /* 引用计数 = 1 → 彻底移除：swap-remove */
            if (i < tl->held_count - 1) {
                tl->held_locks[i] = tl->held_locks[tl->held_count - 1];
            }
            tl->held_count--;
            pthread_mutex_unlock(&tl->held_mutex);
            return TREELOCK_OK;
        }
    }

    pthread_mutex_unlock(&tl->held_mutex);
    return TREELOCK_ERR_INVAL;
}

/**
 * 函数名称：_get_held_mode
 *
 * 功能描述：获取已持有锁的模式
 *
 * @param[IN]  tl      - 锁句柄
 * @param[IN]  node_id - 节点 ID
 * @param[OUT] mode    - 输出：锁模式（仅当返回 TREELOCK_OK 时有效）
 *
 * @return TREELOCK_OK 获取成功
 * @return TREELOCK_ERR_INVAL 未持有该节点锁
 */
static RET_CODE _get_held_mode(
    IN  treelock_t         *tl,
    IN  treelock_node_id_t  node_id,
    OUT treelock_mode_t    *mode)
{
    RET_CODE rc = TREELOCK_ERR_INVAL;

    if (tl == NULL || mode == NULL) {
        return TREELOCK_ERR_INVAL;
    }

    pthread_mutex_lock(&tl->held_mutex);
    {
        treelock_held_lock_t *held = _find_held_lock(tl, node_id);
        if (held != NULL) {
            *mode = held->mode;
            rc = TREELOCK_OK;
        }
    }
    pthread_mutex_unlock(&tl->held_mutex);

    return rc;
}

/* =========================================================================
 * 内部辅助 — 锁操作核心逻辑
 * ========================================================================= */

/**
 * 函数名称：_do_lock_core
 *
 * 功能描述：执行锁获取的核心逻辑（共享于 lock / try_lock / escalate）
 *
 *          流程：
 *          1. 参数校验
 *          2. 检查是否已持有（是 → 升级或直接返回）
 *          3. 获取或创建锁表节点
 *          4. 冲突检查
 *          5a. 无冲突 → 直接授予
 *          5b. 有冲突 → 加入等待队列 → 阻塞/超时等待
 *          6. 记录到已持有锁列表
 *
 * @param[IN] tl         - 锁句柄
 * @param[IN] node_id    - 目标节点 ID
 * @param[IN] mode       - 请求的锁模式
 * @param[IN] timeout_ms - 超时（ms），0=阻塞，>0=超时等待
 *
 * @return TREELOCK_OK 成功获取
 * @return TREELOCK_ERR_* 各类错误
 */
static RET_CODE _do_lock_core(
    IN treelock_t         *tl,
    IN treelock_node_id_t  node_id,
    IN treelock_mode_t     mode,
    IN INT_32              timeout_ms)
{
    treelock_node_t  *node;
    CSTR_PTR          cid;
    RET_CODE          rc;

    /* ── 1. 参数校验 ── */
    if (tl == NULL || tl->destroyed) {
        return TREELOCK_ERR_INVAL;
    }
    if (mode <= TREELOCK_NL || mode > TREELOCK_MODE_MAX) {
        return TREELOCK_ERR_INVAL;
    }

    /* ── 2. 已持有检查与升级 ── */
    /*
     * is_new_lock 标记：TRUE = 全新获取锁，FALSE = 从已有模式升级。
     * 用于步骤 4 中决定是否启用并发重复授予检查。
     * 升级路径不应跳过 grant，因为旧周期的 grant 可能残留。
     */
    INT_32 is_new_lock = TRUE;
    {
        treelock_mode_t existing_mode;
        rc = _get_held_mode(tl, node_id, &existing_mode);
        if (rc == TREELOCK_OK) {
            is_new_lock = FALSE;
            /* 已持有相同模式 → 增加引用计数（支持重入） */
            if (existing_mode == mode) {
                pthread_mutex_lock(&tl->held_mutex);
                {
                    treelock_held_lock_t *held = _find_held_lock(tl, node_id);
                    if (held != NULL) {
                        held->ref_count++;
                        TREELOCK_LOG_DEBUG("CORE",
                            "lock re-entrant: node=%llu mode=%s ref=%u client=%s",
                            (unsigned long long)node_id,
                            treelock_mode_name(mode),
                            (unsigned int)(held->ref_count),
                            tl->config.client_id);
                    }
                }
                pthread_mutex_unlock(&tl->held_mutex);
                return TREELOCK_OK;
            }
            /* 检查升级路径 */
            if (!treelock_escalate_valid(existing_mode, mode)) {
                TREELOCK_LOG_WARN("CORE",
                    "escalate not allowed: node=%llu %s→%s client=%s",
                    (unsigned long long)node_id,
                    treelock_mode_name(existing_mode),
                    treelock_mode_name(mode),
                    tl->config.client_id);
                return TREELOCK_ERR_PROTOCOL;
            }
        }
    }

    cid = (tl->config.client_id != NULL) ? tl->config.client_id : "local";

    /* ── 3. 获取或创建锁表节点 ── */
    pthread_mutex_lock(&tl->table_mutex);
    node = treelock_table_get_or_create(tl, node_id);
    pthread_mutex_unlock(&tl->table_mutex);

    if (node == NULL) {
        TREELOCK_LOG_ERROR("CORE",
            "failed to get or create lock table node for node_id=%llu",
            (unsigned long long)node_id);
        return TREELOCK_ERR_INVAL;
    }

    /* ── 3.5 协议校验（树结构管理）── */
    rc = treelock_validate_protocol(tl, node_id, mode);
    if (rc != TREELOCK_OK) {
        return rc;
    }

    /* ── 4 & 5. 冲突检查 → 授予或等待 ── */
    pthread_mutex_lock(&node->mutex);

    if (!treelock_table_check_conflict(node, cid, mode)) {
        /* 存在冲突 → 加入等待队列 */
        treelock_wait_entry_t *entry;

        TREELOCK_LOG_DEBUG("CORE",
            "lock conflict: node=%llu mode=%s client=%s, joining wait queue "
            "(grant_count=%llu wait_count=%llu)",
            (unsigned long long)node_id, treelock_mode_name(mode), cid,
            (unsigned long long)node->grant_count,
            (unsigned long long)node->wait_count);

        rc = treelock_table_add_waiter(node, cid, mode);
        if (rc != TREELOCK_OK) {
            pthread_mutex_unlock(&node->mutex);
            TREELOCK_LOG_ERROR("CORE",
                "failed to add waiter: node=%llu mode=%s client=%s",
                (unsigned long long)node_id, treelock_mode_name(mode), cid);
            return rc;
        }

        /* 获取刚加入的等待条目（指针） */
        entry = node->wait_queue[node->wait_count - 1];

        if (timeout_ms > 0) {
            /* ── 带超时等待 ── */
            struct timespec ts;
            INT_64 expire = _current_time_ms() + timeout_ms;
            ts.tv_sec  = (time_t)(expire / 1000);
            ts.tv_nsec = (long)((expire % 1000) * 1000000L);

            INT_32 wait_rc = pthread_cond_timedwait(
                &entry->cond, &node->mutex, &ts);
            if (wait_rc == ETIMEDOUT) {
                /* 超时：从等待队列中移除自身 */
                UINT_64 j;
                for (j = 0; j < node->wait_count; j++) {
                    if (node->wait_queue[j] == entry) {
                        pthread_cond_destroy(&entry->cond);
                        free(entry);
                        /* swap-remove：移动最后一个指针到位置 j */
                        if (j < node->wait_count - 1) {
                            node->wait_queue[j] =
                                node->wait_queue[node->wait_count - 1];
                        }
                        node->wait_count--;
                        break;
                    }
                }
                pthread_mutex_unlock(&node->mutex);
                TREELOCK_LOG_WARN("CORE",
                    "lock timeout: node=%llu mode=%s client=%s timeout=%dms",
                    (unsigned long long)node_id,
                    treelock_mode_name(mode), cid, timeout_ms);
                return TREELOCK_ERR_TIMEOUT;
            }
        } else {
            /* ── 阻塞等待 ── */
            TREELOCK_LOG_TRACE("CORE",
                "blocking wait: node=%llu mode=%s client=%s",
                (unsigned long long)node_id,
                treelock_mode_name(mode), cid);
            while (TRUE) {
                INT_32 wait_rc = pthread_cond_wait(&entry->cond, &node->mutex);
                if (wait_rc == 0) {
                    break; /* 被正常唤醒 */
                }
                /* 假唤醒 → 继续等待 */
            }
        }

        /* ── 被唤醒后验证是否已被授予 ── */
        {
            UINT_64 k;
            INT_32   granted = FALSE;
            for (k = 0; k < node->grant_count; k++) {
                if (strcmp(node->grants[k].client_id, cid) == EQUAL &&
                    node->grants[k].mode == mode) {
                    granted = TRUE;
                    break;
                }
            }
            if (!granted) {
                /*
                 * 未能获取锁（spurious wake 后 grant 已被他人抢占）。
                 * 从等待队列中移除自身，销毁 cond 并释放条目。
                 */
                UINT_64 j;
                for (j = 0; j < node->wait_count; j++) {
                    if (node->wait_queue[j] == entry) {
                        if (j < node->wait_count - 1) {
                            node->wait_queue[j] =
                                node->wait_queue[node->wait_count - 1];
                        }
                        node->wait_count--;
                        break;
                    }
                }
                pthread_cond_destroy(&entry->cond);
                free(entry);
                pthread_mutex_unlock(&node->mutex);
                TREELOCK_LOG_WARN("CORE",
                    "lock stale after wake: node=%llu mode=%s client=%s",
                    (unsigned long long)node_id,
                    treelock_mode_name(mode), cid);
                return TREELOCK_ERR_STALE;
            }
            /*
             * 成功获取锁 → 销毁自己的 cond 并释放条目。
             * wake_waiters 已将本条目 swap-remove 出队列，
             * entry 不再被队列引用。
             */
            pthread_cond_destroy(&entry->cond);
            free(entry);
            TREELOCK_LOG_TRACE("CORE",
                "woken and granted: node=%llu mode=%s client=%s",
                (unsigned long long)node_id,
                treelock_mode_name(mode), cid);
        }
    } else {
        /*
         * ── 防竞争重复授予检查（仅全新锁，不含 escalate）──
         *
         * 竞争窗口：_get_held_mode() (步骤 2) 与 _add_held_lock() (步骤 6)
         * 之间，另一个线程可能已为同一 (client_id, mode) 授予了锁。
         * 若直接 grant_lock，将导致 grant 列表中出现重复条目，
         * 进而第一个线程 unlock 时释放 grant，第二个线程 unlock
         * 时找不到 grant → TREELOCK_ERR_INVAL。
         *
         * 限制 is_new_lock 范围：escalate 场景下 grant 列表中可能有
         * 旧周期残留的同模式记录（如 lock→escalate→unlock→lock 后
         * grant 已清空但仍有其他模式），不应跳过授予。
         *
         * 修复：在 node->mutex 保护下扫描 grant 列表，若发现已有
         * 匹配条目则跳过授予，仅处理 held_locks 引用计数。
         */
        if (is_new_lock) {
            UINT_64 gi;
            INT_32  already_granted = FALSE;
            for (gi = 0; gi < node->grant_count; gi++) {
                if (strcmp(node->grants[gi].client_id, cid) == EQUAL &&
                    node->grants[gi].mode == mode) {
                    already_granted = TRUE;
                    break;
                }
            }

            if (already_granted) {
                /* 并发线程已授予 → 释放 node->mutex 后补建 held 引用 */
                pthread_mutex_unlock(&node->mutex);

                /*
                 * 补充 held_locks 引用：并发线程可能尚未调用
                 * _add_held_lock（仍在 node->mutex 临界区内），
                 * 也可能已经调用。同时处理两种情况。
                 */
                pthread_mutex_lock(&tl->held_mutex);
                {
                    treelock_held_lock_t *held = _find_held_lock(tl, node_id);
                    if (held != NULL && held->mode == mode) {
                        /* 并发线程已完成 _add_held_lock → 仅增引用 */
                        held->ref_count++;
                        TREELOCK_LOG_DEBUG("CORE",
                            "lock re-entrant (concurrent grant): "
                            "node=%llu mode=%s ref=%u client=%s",
                            (unsigned long long)node_id,
                            treelock_mode_name(mode),
                            (unsigned int)(held->ref_count), cid);
                    } else {
                        /*
                         * 并发线程尚未完成 _add_held_lock 或 held
                         * entry 不存在 → 代为创建 held 记录。
                         * 不能调用 _add_held_lock (会重复获取 held_mutex)，
                         * 内联其逻辑。
                         */
                        if (tl->held_count >= tl->held_capacity) {
                            UINT_64 new_cap;
                            treelock_held_lock_t *new_h;
                            new_cap = (tl->held_capacity == 0)
                                      ? TREELOCK_HELD_INIT_CAP
                                      : tl->held_capacity * 2;
                            new_h = (treelock_held_lock_t *)realloc(
                                tl->held_locks,
                                (size_t)(new_cap * sizeof(treelock_held_lock_t)));
                            if (new_h != NULL) {
                                tl->held_locks    = new_h;
                                tl->held_capacity = new_cap;
                            }
                        }
                        if (tl->held_count < tl->held_capacity) {
                            tl->held_locks[tl->held_count].node_id     = node_id;
                            tl->held_locks[tl->held_count].mode        = mode;
                            tl->held_locks[tl->held_count].acquired_at = _current_time_ms();
                            tl->held_locks[tl->held_count].ref_count   = 1;
                            tl->held_count++;
                            TREELOCK_LOG_DEBUG("CORE",
                                "lock held (concurrent grant): "
                                "node=%llu mode=%s client=%s",
                                (unsigned long long)node_id,
                                treelock_mode_name(mode), cid);
                        } else {
                            /*
                             * 致命错误：realloc 失败且数组已满，
                             * 无法记录 held entry。锁表中 grant 已存在，
                             * 但客户端无法追踪该锁。
                             * 保守策略：返回错误而非静默丢失追踪记录。
                             */
                            TREELOCK_LOG_ERROR("CORE",
                                "FATAL: cannot record held lock after concurrent grant: "
                                "node=%llu mode=%s client=%s (held_count=%llu capacity=%llu)",
                                (unsigned long long)node_id,
                                treelock_mode_name(mode), cid,
                                (unsigned long long)tl->held_count,
                                (unsigned long long)tl->held_capacity);
                            pthread_mutex_unlock(&tl->held_mutex);
                            return TREELOCK_ERR_INVAL;
                        }
                    }
                }
                pthread_mutex_unlock(&tl->held_mutex);

                TREELOCK_LOG_DEBUG("CORE",
                    "lock acquired (via concurrent): node=%llu mode=%s client=%s",
                    (unsigned long long)node_id,
                    treelock_mode_name(mode), cid);
                return TREELOCK_OK;
            }
        }

        /* 无冲突 → 直接授予 */
        TREELOCK_LOG_TRACE("CORE",
            "direct grant: node=%llu mode=%s client=%s",
            (unsigned long long)node_id,
            treelock_mode_name(mode), cid);
        rc = treelock_table_grant_lock(node, cid, mode,
                                        TREELOCK_DEFAULT_LEASE_MS);
        if (rc != TREELOCK_OK) {
            pthread_mutex_unlock(&node->mutex);
            TREELOCK_LOG_ERROR("CORE",
                "grant lock failed: node=%llu mode=%s client=%s",
                (unsigned long long)node_id,
                treelock_mode_name(mode), cid);
            return rc;
        }
    }

    pthread_mutex_unlock(&node->mutex);

    /* ── 6. 记录到已持有锁列表 ── */
    {
        rc = _add_held_lock(tl, node_id, mode);
        if (rc != TREELOCK_OK) {
            /* 回滚：释放刚获取的锁 */
            pthread_mutex_lock(&node->mutex);
            treelock_table_release_lock(node, cid, mode);
            treelock_table_wake_waiters(node);
            pthread_mutex_unlock(&node->mutex);
            TREELOCK_LOG_ERROR("CORE",
                "lock node=%llu mode=%s failed: cannot record held lock",
                (unsigned long long)node_id, treelock_mode_name(mode));
            return rc;
        }
    }

    TREELOCK_LOG_DEBUG("CORE", "lock acquired: node=%llu mode=%s client=%s",
                       (unsigned long long)node_id,
                       treelock_mode_name(mode), cid);
    return TREELOCK_OK;
}

/* =========================================================================
 * 生命周期
 * ========================================================================= */

/**
 * 函数名称：treelock_create
 *
 * 功能描述：创建锁客户端实例
 *
 *          分配并初始化 treelock_t 结构体，初始化两个互斥锁（table_mutex,
 *          held_mutex）。阶段一单机版 connected 始终为 TRUE。
 *
 * @param[IN] config - 客户端配置（可为 NULL，使用默认配置）
 *
 * @return 成功返回锁句柄指针，失败（OOM 或锁初始化失败）返回 NULL
 */
treelock_t *treelock_create(
    IN const treelock_config_t *config)
{
    treelock_t *tl = (treelock_t *)calloc(1, sizeof(treelock_t));
    if (tl == NULL) {
        return NULL;
    }

    /* 初始化配置 */
    if (config != NULL) {
        tl->config = *config;
    } else {
        /* 阶段一默认配置 */
        memset(&tl->config, 0, sizeof(tl->config));
        tl->config.timeout_ms = 0;      /* 默认阻塞 */
        tl->config.client_id  = "local"; /* 默认标识 */
    }

    /* 初始化互斥锁 */
    if (pthread_mutex_init(&tl->table_mutex, NULL) != 0) {
        TREELOCK_LOG_ERROR("CORE", "failed to init table_mutex");
        free(tl);
        return NULL;
    }
    if (pthread_mutex_init(&tl->held_mutex, NULL) != 0) {
        TREELOCK_LOG_ERROR("CORE", "failed to init held_mutex");
        pthread_mutex_destroy(&tl->table_mutex);
        free(tl);
        return NULL;
    }

    tl->connected = TRUE;
    tl->destroyed = FALSE;

    TREELOCK_LOG_INFO("CORE", "client '%s' created",
                      tl->config.client_id);
    return tl;
}

/**
 * 函数名称：treelock_destroy
 *
 * 功能描述：销毁客户端实例
 *
 *          执行顺序：
 *          1. 释放所有持有的锁（treelock_unlock_all）
 *          2. 遍历锁表链表，释放所有节点及其内部资源
 *          3. 释放已持有锁列表内存
 *          4. 销毁互斥锁
 *          5. 释放 treelock_t 自身
 *
 * @param[IN] tl - 锁句柄
 */
VOID treelock_destroy(
    IN treelock_t *tl)
{
    UINT_64 i;

    if (tl == NULL) {
        return;
    }

    TREELOCK_LOG_INFO("CORE", "client '%s' destroying, %llu locks held",
                      tl->config.client_id,
                      (unsigned long long)tl->held_count);

    /* 释放所有持有的锁 */
    treelock_unlock_all(tl);
    tl->destroyed = TRUE;

    /*
     * 同步屏障：lock+unlock 每个节点的 mutex，确保被 treelock_unlock_all
     * 唤醒的线程已完全退出 pthread_cond_wait 并释放了 node->mutex。
     * 这不能防御调用者在线程仍在使用句柄时调用 destroy 的情况，
     * 但可以消除 unlock_all 与被唤醒线程之间的竞态窗口。
     */
    pthread_mutex_lock(&tl->table_mutex);
    {
        treelock_node_t *barrier_node, *barrier_tmp;
        HASH_ITER(hh, tl->lock_table, barrier_node, barrier_tmp) {
            pthread_mutex_lock(&barrier_node->mutex);
            pthread_mutex_unlock(&barrier_node->mutex);
        }
    }
    pthread_mutex_unlock(&tl->table_mutex);

    /* 卸载树结构（通过回调委托 treelock_tree 模块释放内存） */
    if (tl->tree_data != NULL && tl->tree_destroy != NULL) {
        tl->tree_destroy(tl->tree_data);
    }
    tl->tree_data       = NULL;
    tl->tree_get_parent = NULL;
    tl->tree_destroy    = NULL;

    /* 清理锁表（哈希表迭代） */
    pthread_mutex_lock(&tl->table_mutex);
    {
        treelock_node_t *cleanup_node, *cleanup_tmp;
        HASH_ITER(hh, tl->lock_table, cleanup_node, cleanup_tmp) {
            /* 释放动态数组 */
            free(cleanup_node->grants);
            cleanup_node->grants = NULL;

            /* 销毁等待队列中已初始化的条目（仅 wait_count 个已使用） */
            for (i = 0; i < cleanup_node->wait_count; i++) {
                pthread_cond_destroy(
                    &cleanup_node->wait_queue[i]->cond);
                free(cleanup_node->wait_queue[i]);
            }
            free(cleanup_node->wait_queue);
            cleanup_node->wait_queue = NULL;

            pthread_mutex_destroy(&cleanup_node->mutex);

            /* 从哈希表中移除后释放节点内存 */
            HASH_DEL(tl->lock_table, cleanup_node);
            free(cleanup_node);
        }
        tl->lock_table = NULL;
    }
    pthread_mutex_unlock(&tl->table_mutex);

    /* 释放已持有锁列表 */
    pthread_mutex_lock(&tl->held_mutex);
    free(tl->held_locks);
    tl->held_locks    = NULL;
    tl->held_count    = 0;
    tl->held_capacity = 0;
    pthread_mutex_unlock(&tl->held_mutex);

    /* 销毁互斥锁 */
    pthread_mutex_destroy(&tl->table_mutex);
    pthread_mutex_destroy(&tl->held_mutex);

    free(tl);
}

/* =========================================================================
 * 锁操作 — 公共 API
 * ========================================================================= */

/**
 * 函数名称：treelock_lock
 *
 * 功能描述：阻塞方式获取指定节点的锁
 *
 *          与 treelock_try_lock(tl, node_id, mode, 0) 等效。
 *
 * @param[IN] tl      - 锁句柄
 * @param[IN] node_id - 目标节点 ID
 * @param[IN] mode    - 请求的锁模式
 *
 * @return TREELOCK_OK 成功
 * @return TREELOCK_ERR_* 错误码
 */
RET_CODE treelock_lock(
    IN treelock_t         *tl,
    IN treelock_node_id_t  node_id,
    IN treelock_mode_t     mode)
{
    return _do_lock_core(tl, node_id, mode, 0); /* 阻塞模式 */
}

/**
 * 函数名称：treelock_try_lock
 *
 * 功能描述：尝试获取锁，设置超时时间
 *
 * @param[IN] tl         - 锁句柄
 * @param[IN] node_id    - 目标节点 ID
 * @param[IN] mode       - 请求的锁模式
 * @param[IN] timeout_ms - 超时时间（毫秒），0=阻塞，>0=超时等待
 *
 * @return TREELOCK_OK 成功
 * @return TREELOCK_ERR_TIMEOUT 超时
 * @return TREELOCK_ERR_* 其他错误码
 */
RET_CODE treelock_try_lock(
    IN treelock_t         *tl,
    IN treelock_node_id_t  node_id,
    IN treelock_mode_t     mode,
    IN INT_32              timeout_ms)
{
    return _do_lock_core(tl, node_id, mode, timeout_ms);
}

/**
 * 函数名称：treelock_unlock
 *
 * 功能描述：释放指定节点上的锁
 *
 *          流程：
 *          1. 查找持有的锁模式
 *          2. 从锁表中移除授权记录
 *          3. 唤醒等待队列中兼容的请求者
 *          4. 从已持有锁列表中删除记录
 *
 * @param[IN] tl      - 锁句柄
 * @param[IN] node_id - 目标节点 ID
 *
 * @return TREELOCK_OK 释放成功
 * @return TREELOCK_ERR_INVAL 未持有该节点锁
 */
RET_CODE treelock_unlock(
    IN treelock_t         *tl,
    IN treelock_node_id_t  node_id)
{
    treelock_node_t *node;
    treelock_mode_t  held_mode;
    CSTR_PTR         cid;
    RET_CODE         rc;

    if (tl == NULL || tl->destroyed) {
        return TREELOCK_ERR_INVAL;
    }

    /*
     * 引用计数检查：同客户端重入加锁时，每次 unlock 只减计数，
     * 仅当引用计数归零时才真正从锁表中释放。
     */
    pthread_mutex_lock(&tl->held_mutex);
    {
        treelock_held_lock_t *held = _find_held_lock(tl, node_id);
        if (held == NULL) {
            pthread_mutex_unlock(&tl->held_mutex);
            return TREELOCK_ERR_INVAL;
        }

        if (held->ref_count > 1) {
            /* 还有别的重入引用 → 只减计数，不释放锁表 */
            held->ref_count--;
            held_mode = held->mode;
            pthread_mutex_unlock(&tl->held_mutex);
            TREELOCK_LOG_DEBUG("CORE",
                "lock ref_count decremented: node=%llu mode=%s ref=%u client=%s",
                (unsigned long long)node_id,
                treelock_mode_name(held_mode),
                (unsigned int)(held->ref_count),
                tl->config.client_id);
            return TREELOCK_OK;
        }

        /* ref_count == 1 → 最后一次释放 */
        held_mode = held->mode;
    }
    pthread_mutex_unlock(&tl->held_mutex);

    cid = (tl->config.client_id != NULL) ? tl->config.client_id : "local";

    /* 从锁表中查找节点 */
    pthread_mutex_lock(&tl->table_mutex);
    node = treelock_table_find(tl, node_id);
    pthread_mutex_unlock(&tl->table_mutex);

    if (node == NULL) {
        TREELOCK_LOG_WARN("CORE",
            "unlock stale: node=%llu not in lock table (held by client '%s')",
            (unsigned long long)node_id, cid);
        _remove_held_lock(tl, node_id);
        return TREELOCK_ERR_STALE;
    }

    /* 最后一次释放：从锁表中移除授权记录并唤醒等待者 */
    pthread_mutex_lock(&node->mutex);
    rc = treelock_table_release_lock(node, cid, held_mode);
    if (rc == TREELOCK_OK) {
        treelock_table_wake_waiters(node);
    }
    pthread_mutex_unlock(&node->mutex);

    if (rc == TREELOCK_OK) {
        _remove_held_lock(tl, node_id);
        TREELOCK_LOG_DEBUG("CORE", "lock released: node=%llu mode=%s client=%s",
                           (unsigned long long)node_id,
                           treelock_mode_name(held_mode), cid);
    } else {
        /*
         * grant 未找到：可能是并发 escalate 已经代为释放了旧模式。
         * 必须重新检查 held entry 的模式是否已变更：
         * - 若 mode 已变（escalate 更新了 held entry）→ 不删除，返回 OK
         * - 若 mode 未变（其他原因）→ 清理悬空 held entry
         */
        BOOL should_remove = TRUE;
        pthread_mutex_lock(&tl->held_mutex);
        {
            treelock_held_lock_t *held = _find_held_lock(tl, node_id);
            if (held != NULL && held->mode != held_mode) {
                /*
                 * escalate 已将 held entry 从 old_mode 更新为 new_mode，
                 * 此次 unlock 对应的是已被 escalate 替换的旧锁周期。
                 * 不删除 held entry —— 它现在代表了 escalate 后的新模式。
                 */
                should_remove = FALSE;
                TREELOCK_LOG_DEBUG("CORE",
                    "lock mode changed by escalate: node=%llu "
                    "unlock_mode=%s held_mode=%s client=%s — skip held removal",
                    (unsigned long long)node_id,
                    treelock_mode_name(held_mode),
                    treelock_mode_name(held->mode), cid);
            }
        }
        pthread_mutex_unlock(&tl->held_mutex);

        if (should_remove) {
            TREELOCK_LOG_DEBUG("CORE",
                "lock already released by concurrent thread: "
                "node=%llu mode=%s client=%s",
                (unsigned long long)node_id,
                treelock_mode_name(held_mode), cid);
            _remove_held_lock(tl, node_id);
        }
        rc = TREELOCK_OK;
    }

    return rc;
}

/**
 * 函数名称：treelock_unlock_all
 *
 * 功能描述：释放当前客户端持有的所有锁
 *
 *          按获取的逆序（自底向上）释放，遵循锁协议规则 3。
 *          使用 while 循环逐一释放，每次释放 held_locks 的最后一个元素。
 *
 * @param[IN] tl - 锁句柄
 *
 * @return TREELOCK_OK 全部释放成功
 * @return TREELOCK_ERR_INVAL 参数无效
 */
RET_CODE treelock_unlock_all(
    IN treelock_t *tl)
{
    CSTR_PTR cid;
    UINT_64 released = 0;

    if (tl == NULL || tl->destroyed) {
        return TREELOCK_ERR_INVAL;
    }

    cid = (tl->config.client_id != NULL) ? tl->config.client_id : "local";

    TREELOCK_LOG_INFO("CORE", "unlock_all: client='%s' releasing %llu locks",
                      cid, (unsigned long long)tl->held_count);

    pthread_mutex_lock(&tl->held_mutex);

    while (tl->held_count > 0) {
        treelock_node_id_t node_id;
        treelock_mode_t    mode;
        treelock_node_t   *node;

        /* 取最后一个（逆序释放） */
        node_id = tl->held_locks[tl->held_count - 1].node_id;
        mode    = tl->held_locks[tl->held_count - 1].mode;
        tl->held_count--;

        pthread_mutex_unlock(&tl->held_mutex);

        /* 在锁表中释放 */
        pthread_mutex_lock(&tl->table_mutex);
        node = treelock_table_find(tl, node_id);
        pthread_mutex_unlock(&tl->table_mutex);

        if (node != NULL) {
            pthread_mutex_lock(&node->mutex);
            treelock_table_release_lock(node, cid, mode);
            treelock_table_wake_waiters(node);
            pthread_mutex_unlock(&node->mutex);
            released++;
        }

        pthread_mutex_lock(&tl->held_mutex);
    }

    pthread_mutex_unlock(&tl->held_mutex);

    TREELOCK_LOG_INFO("CORE", "unlock_all: client='%s' released %llu locks",
                      cid, (unsigned long long)released);
    return TREELOCK_OK;
}

/* =========================================================================
 * 锁升级 / 降级
 * ========================================================================= */

/**
 * 函数名称：treelock_escalate
 *
 * 功能描述：将节点已有锁升级为更强的锁模式
 *
 *          升级策略：
 *          1. 验证升级路径合法性
 *          2. 获取新锁（_do_lock_core）
 *          3. 释放旧锁授权记录
 *
 * @param[IN] tl       - 锁句柄
 * @param[IN] node_id  - 目标节点 ID
 * @param[IN] new_mode - 新锁模式（必须比当前模式更强）
 *
 * @return TREELOCK_OK 升级成功
 * @return TREELOCK_ERR_PROTOCOL 升级路径不合法
 * @return TREELOCK_ERR_* 其他错误码
 */
RET_CODE treelock_escalate(
    IN treelock_t         *tl,
    IN treelock_node_id_t  node_id,
    IN treelock_mode_t     new_mode)
{
    treelock_mode_t  old_mode;
    treelock_node_t *node;
    CSTR_PTR         cid;
    RET_CODE         rc;

    if (tl == NULL || tl->destroyed) {
        return TREELOCK_ERR_INVAL;
    }

    /* 获取旧模式 */
    rc = _get_held_mode(tl, node_id, &old_mode);
    if (rc != TREELOCK_OK) {
        return TREELOCK_ERR_INVAL;
    }

    if (!treelock_escalate_valid(old_mode, new_mode)) {
        return TREELOCK_ERR_PROTOCOL;
    }

    /*
     * 协议校验（树结构管理）：
     * 升级可能改变子节点锁模式，从而改变对父节点锁的要求。
     * 例如 IS→X：子节点 IS 只需父节点 IS，但子节点 X 需要父节点 IX。
     */
    rc = treelock_validate_protocol(tl, node_id, new_mode);
    if (rc != TREELOCK_OK) {
        return rc;
    }

    cid = (tl->config.client_id != NULL) ? tl->config.client_id : "local";

    /*
     * 升级策略（直接操作锁表 + 原地更新 held entry，避免双重条目）：
     * 1. 获取或创建锁表节点
     * 2. 授予新模式到锁表
     * 3. 释放旧模式从锁表
     * 4. 更新 held entry 的 mode 字段
     */
    pthread_mutex_lock(&tl->table_mutex);
    node = treelock_table_get_or_create(tl, node_id);
    pthread_mutex_unlock(&tl->table_mutex);

    if (node == NULL) {
        return TREELOCK_ERR_INVAL;
    }

    pthread_mutex_lock(&node->mutex);

    /* 冲突检查（跳过自身旧锁） */
    if (!treelock_table_check_conflict(node, cid, new_mode)) {
        pthread_mutex_unlock(&node->mutex);
        return TREELOCK_ERR_CONFLICT;
    }

    /* 授予新模式 */
    rc = treelock_table_grant_lock(node, cid, new_mode,
                                    TREELOCK_DEFAULT_LEASE_MS);
    if (rc != TREELOCK_OK) {
        pthread_mutex_unlock(&node->mutex);
        return rc;
    }

    /* 释放旧模式 */
    treelock_table_release_lock(node, cid, old_mode);
    treelock_table_wake_waiters(node);
    pthread_mutex_unlock(&node->mutex);

    /* 原地更新 held entry 的模式 */
    pthread_mutex_lock(&tl->held_mutex);
    {
        treelock_held_lock_t *held = _find_held_lock(tl, node_id);
        if (held != NULL) {
            held->mode = new_mode;
        }
    }
    pthread_mutex_unlock(&tl->held_mutex);

    TREELOCK_LOG_DEBUG("CORE", "lock escalated: node=%llu %s→%s client=%s",
                       (unsigned long long)node_id,
                       treelock_mode_name(old_mode),
                       treelock_mode_name(new_mode), cid);

    return TREELOCK_OK;
}

/**
 * 函数名称：treelock_downgrade
 *
 * 功能描述：将节点已有锁降级为更弱的锁模式
 *
 *          降级策略：
 *          1. 验证降级路径合法性
 *          2. 先获取更弱的锁
 *          3. 释放旧锁
 *          （两步操作期间短暂持有两个锁，安全性：更强→更弱 方向）
 *
 * @param[IN] tl       - 锁句柄
 * @param[IN] node_id  - 目标节点 ID
 * @param[IN] new_mode - 新锁模式（必须比当前模式更弱）
 *
 * @return TREELOCK_OK 降级成功
 * @return TREELOCK_ERR_PROTOCOL 降级路径不合法
 * @return TREELOCK_ERR_* 其他错误码
 */
RET_CODE treelock_downgrade(
    IN treelock_t         *tl,
    IN treelock_node_id_t  node_id,
    IN treelock_mode_t     new_mode)
{
    treelock_mode_t  old_mode;
    treelock_node_t *node;
    CSTR_PTR         cid;
    RET_CODE         rc;

    if (tl == NULL || tl->destroyed) {
        return TREELOCK_ERR_INVAL;
    }

    rc = _get_held_mode(tl, node_id, &old_mode);
    if (rc != TREELOCK_OK) {
        return TREELOCK_ERR_INVAL;
    }

    if (!treelock_downgrade_valid(old_mode, new_mode)) {
        return TREELOCK_ERR_PROTOCOL;
    }

    cid = (tl->config.client_id != NULL) ? tl->config.client_id : "local";

    /*
     * 降级策略（直接操作锁表 + 原地更新 held entry）：
     * 1. 授予新模式到锁表
     * 2. 释放旧模式
     * 3. 更新 held entry 的 mode
     */
    pthread_mutex_lock(&tl->table_mutex);
    node = treelock_table_get_or_create(tl, node_id);
    pthread_mutex_unlock(&tl->table_mutex);

    if (node == NULL) {
        return TREELOCK_ERR_INVAL;
    }

    pthread_mutex_lock(&node->mutex);

    rc = treelock_table_grant_lock(node, cid, new_mode,
                                    TREELOCK_DEFAULT_LEASE_MS);
    if (rc != TREELOCK_OK) {
        pthread_mutex_unlock(&node->mutex);
        return rc;
    }

    treelock_table_release_lock(node, cid, old_mode);
    treelock_table_wake_waiters(node);
    pthread_mutex_unlock(&node->mutex);

    /* 原地更新 held entry */
    pthread_mutex_lock(&tl->held_mutex);
    {
        treelock_held_lock_t *held = _find_held_lock(tl, node_id);
        if (held != NULL) {
            held->mode = new_mode;
        }
    }
    pthread_mutex_unlock(&tl->held_mutex);

    TREELOCK_LOG_DEBUG("CORE", "lock downgraded: node=%llu %s→%s client=%s",
                       (unsigned long long)node_id,
                       treelock_mode_name(old_mode),
                       treelock_mode_name(new_mode), cid);

    return TREELOCK_OK;
}

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
 * @return 持有的锁模式，未持有返回 TREELOCK_NL，tl 为 NULL 返回 TREELOCK_NL
 */
treelock_mode_t treelock_get_mode(
    IN treelock_t         *tl,
    IN treelock_node_id_t  node_id)
{
    treelock_mode_t mode = TREELOCK_NL;

    if (tl == NULL) {
        return TREELOCK_NL;
    }

    pthread_mutex_lock(&tl->held_mutex);
    {
        treelock_held_lock_t *held = _find_held_lock(tl, node_id);
        if (held != NULL) {
            mode = held->mode;
        }
    }
    pthread_mutex_unlock(&tl->held_mutex);

    return mode;
}

/**
 * 函数名称：treelock_query_node
 *
 * 功能描述：查询指定节点上所有客户端持有的锁信息
 *
 *          构造简要 JSON 格式的输出：{"node_id":..., "grants":[...], "wait_queue_len":...}
 *
 * @param[IN]  tl          - 锁句柄
 * @param[IN]  node_id     - 目标节点 ID
 * @param[OUT] json_result - 输出 JSON 字符串（调用者负责 free），空结果返回 "{}"
 *
 * @return TREELOCK_OK 查询成功
 * @return TREELOCK_ERR_INVAL 参数无效或内存不足
 */
RET_CODE treelock_query_node(
    IN  treelock_t         *tl,
    IN  treelock_node_id_t  node_id,
    OUT CHAR               **json_result)
{
    treelock_node_t *node;
    CHAR             buf[4096];
    INT_32           offset;
    UINT_64          i;

    if (tl == NULL || json_result == NULL || tl->destroyed) {
        return TREELOCK_ERR_INVAL;
    }

    pthread_mutex_lock(&tl->table_mutex);
    node = treelock_table_find(tl, node_id);
    pthread_mutex_unlock(&tl->table_mutex);

    if (node == NULL) {
        *json_result = strdup("{}");
        return (*json_result != NULL) ? TREELOCK_OK : TREELOCK_ERR_INVAL;
    }

    pthread_mutex_lock(&node->mutex);

    offset = 0;
    offset += snprintf(buf + offset, sizeof(buf) - (size_t)offset,
                       "{\"node_id\":%llu,\"grants\":[",
                       (unsigned long long)node->node_id);

    for (i = 0; i < node->grant_count; i++) {
        /* 防止缓冲区溢出：剩余空间不足时提前截断 */
        if ((size_t)offset >= sizeof(buf) - 2) {
            break;
        }
        if (i > 0) {
            offset += snprintf(buf + offset, sizeof(buf) - (size_t)offset, ",");
        }
        offset += snprintf(buf + offset, sizeof(buf) - (size_t)offset,
                          "{\"client\":\"%s\",\"mode\":\"%s\"}",
                          node->grants[i].client_id,
                          treelock_mode_name(node->grants[i].mode));
    }

    /* 确保 offset 不越界再追加尾部 */
    if ((size_t)offset < sizeof(buf) - 1) {
        offset += snprintf(buf + offset, sizeof(buf) - (size_t)offset,
                           "],\"wait_queue_len\":%zu}",
                           (size_t)node->wait_count);
    }

    pthread_mutex_unlock(&node->mutex);

    *json_result = strdup(buf);
    return (*json_result != NULL) ? TREELOCK_OK : TREELOCK_ERR_INVAL;
}

/* =========================================================================
 * 回调
 * ========================================================================= */

/**
 * 函数名称：treelock_set_lost_callback
 *
 * 功能描述：设置锁丢失时的回调函数
 *
 *          当锁因租约过期、网络异常等原因丢失时，库将调用此回调通知应用。
 *          阶段一单机版中此回调不会被触发，保留供后续阶段使用。
 *
 * @param[IN] tl        - 锁句柄
 * @param[IN] cb        - 回调函数指针
 * @param[IN] user_data - 用户自定义数据（回调时透传）
 *
 * @return TREELOCK_OK 设置成功
 * @return TREELOCK_ERR_INVAL 参数无效
 */
RET_CODE treelock_set_lost_callback(
    IN treelock_t      *tl,
    IN treelock_lost_cb cb,
    IN PTR_VOID         user_data)
{
    if (tl == NULL) {
        return TREELOCK_ERR_INVAL;
    }

    tl->lost_cb      = cb;
    tl->lost_cb_data = user_data;

    return TREELOCK_OK;
}

/*
 * TreeLocks - 服务端模块
 *
 * 提供锁管理器服务端功能：锁表持久化、租约管理、Raft 一致性。
 *
 * 版本: 0.1.0
 * 日期: 2026-06-13
 * 状态: 阶段三实现
 */

#ifndef TREELOCK_SERVER_H
#define TREELOCK_SERVER_H

#include "treelock.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * 服务端配置
 * ========================================================================= */

typedef struct {
    CSTR_PTR listen_addr;       /**< 监听地址 "ip:port"           */
    CSTR_PTR raft_peer_addrs;   /**< Raft 集群节点列表（逗号分隔） */
    INT_32   lease_ttl_ms;      /**< 租约 TTL（毫秒）             */
    INT_32   heartbeat_interval_ms; /**< 心跳检查间隔（毫秒）     */
} treelock_server_config_t;

/* =========================================================================
 * 服务端句柄（不透明指针）
 * ========================================================================= */

typedef struct treelock_server_s treelock_server_t;

/* =========================================================================
 * TODO: 阶段三实现以下 API
 *
 * treelock_server_t *treelock_server_create(IN treelock_server_config_t *cfg);
 * VOID               treelock_server_destroy(IN treelock_server_t *srv);
 * RET_CODE           treelock_server_start(IN treelock_server_t *srv);
 * RET_CODE           treelock_server_stop(IN treelock_server_t *srv);
 * ========================================================================= */

#ifdef __cplusplus
}
#endif

#endif /* TREELOCK_SERVER_H */

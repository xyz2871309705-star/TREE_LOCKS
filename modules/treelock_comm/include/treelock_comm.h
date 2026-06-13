/*
 * TreeLocks - 通信层模块
 *
 * 提供客户端↔服务端的消息编解码和网络传输能力。
 *
 * 版本: 0.1.0
 * 日期: 2026-06-13
 * 状态: 阶段二/三实现
 */

#ifndef TREELOCK_COMM_H
#define TREELOCK_COMM_H

#include "treelock.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * 通信层状态码
 * ========================================================================= */

#define TREELOCK_COMM_OK           (0)
#define TREELOCK_COMM_ERR_CONNECT  (-1)
#define TREELOCK_COMM_ERR_SEND     (-2)
#define TREELOCK_COMM_ERR_RECV     (-3)
#define TREELOCK_COMM_ERR_CODEC    (-4)

/* =========================================================================
 * 连接句柄（不透明指针）
 * ========================================================================= */

typedef struct treelock_comm_s treelock_comm_t;

/* =========================================================================
 * TODO: 阶段二/三实现以下 API
 *
 * treelock_comm_t *treelock_comm_create(IN CSTR_PTR server_addr);
 * VOID             treelock_comm_destroy(IN treelock_comm_t *comm);
 * RET_CODE         treelock_comm_connect(IN treelock_comm_t *comm);
 * RET_CODE         treelock_comm_send_lock_request(...);
 * RET_CODE         treelock_comm_send_unlock_request(...);
 * RET_CODE         treelock_comm_send_heartbeat(...);
 * ========================================================================= */

#ifdef __cplusplus
}
#endif

#endif /* TREELOCK_COMM_H */

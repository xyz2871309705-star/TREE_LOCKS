# ============================================================================
# 模块: treelock_server — 锁管理服务端
#
# 职责:
#   - gRPC/RPC 服务实现
#   - 服务端锁表（持久化锁状态）
#   - 租约管理器（TTL 倒计时、过期回收）
#   - Raft 一致性集成（锁表状态机）
#
# 状态: 阶段三实现，当前为占位
# 依赖: treelock_core, treelock_comm, protobuf-c, libuv, Raft
# ============================================================================

set(MODULE_NAME treelock_server)

# ── 阶段三启用时取消注释以下内容 ──
#
# set(MODULE_SOURCES
#     src/main.c
#     src/lock_service.c
#     src/lock_table_svr.c
#     src/lease_mgr.c
#     src/raft_integration.c
# )
#
# set(MODULE_PUBLIC_HEADERS
#     include/treelock_server.h
# )
#
# add_executable(${MODULE_NAME}
#     ${MODULE_SOURCES}
#     ${MODULE_PUBLIC_HEADERS}
# )
#
# target_include_directories(${MODULE_NAME}
#     PRIVATE
#         ${CMAKE_CURRENT_SOURCE_DIR}/include
#         ${CMAKE_CURRENT_SOURCE_DIR}/src
# )
#
# target_link_libraries(${MODULE_NAME}
#     PRIVATE
#         treelock_core
#         treelock_comm
#         protobuf-c
#         uv
# )

message(STATUS "Module '${MODULE_NAME}' is a placeholder (Phase 3)")

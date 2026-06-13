# ============================================================================
# TreeLocks 服务端（阶段三：自研服务版）
#
# 当前为占位文件，阶段三实现以下内容：
#   - main.c              入口
#   - lock_service.c      gRPC 服务实现
#   - lock_table_svr.c    服务端锁表
#   - lease_mgr.c         租约管理
#   - raft_integration.c  Raft 集成
# ============================================================================

# 阶段三启用时取消注释以下内容：

# set(SERVER_SOURCES
#     main.c
#     lock_service.c
#     lock_table_svr.c
#     lease_mgr.c
#     raft_integration.c
# )
#
# add_executable(treelock-server ${SERVER_SOURCES})
#
# target_link_libraries(treelock-server
#     PRIVATE
#         treelock
#         protobuf-c
#         uv
# )
#
# target_include_directories(treelock-server
#     PRIVATE
#         ${CMAKE_SOURCE_DIR}/src
#         ${CMAKE_BINARY_DIR}/proto
# )

message(STATUS "Server build is disabled (Phase 3 feature)")

# ============================================================================
# 模块: treelock_comm — 通信层
#
# 职责:
#   - 客户端↔服务端消息编解码（protobuf / 自定义协议）
#   - TCP 长连接管理
#   - 请求-响应匹配与超时
#
# 状态: 阶段二/三实现，当前为占位
# 依赖: treelock_core, protobuf-c (阶段三), libuv (阶段三)
# ============================================================================

set(MODULE_NAME treelock_comm)

# ── 阶段三启用时取消注释以下内容 ──
#
# set(MODULE_SOURCES
#     src/rpc_client.c
#     src/message_codec.c
#     src/connection_mgr.c
# )
#
# set(MODULE_PUBLIC_HEADERS
#     include/treelock_comm.h
# )
#
# add_library(${MODULE_NAME} STATIC
#     ${MODULE_SOURCES}
#     ${MODULE_PUBLIC_HEADERS}
# )
#
# target_include_directories(${MODULE_NAME}
#     PUBLIC
#         ${CMAKE_CURRENT_SOURCE_DIR}/include
#     PRIVATE
#         ${CMAKE_CURRENT_SOURCE_DIR}/src
# )
#
# target_link_libraries(${MODULE_NAME}
#     PUBLIC
#         treelock_core
#     PRIVATE
#         protobuf-c
#         uv
# )

message(STATUS "Module '${MODULE_NAME}' is a placeholder (Phase 2/3)")

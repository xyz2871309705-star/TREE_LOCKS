# ============================================================================
# TreeLocks 示例程序
#
# 示例可执行文件依赖 treelock_core 模块（自动传递 treelock_log 依赖）。
# ============================================================================

# ── 示例 1：基础使用 ──
add_executable(basic_usage src/basic_usage.c)
target_link_libraries(basic_usage PRIVATE treelock_core)

# ── 示例 2：日志回调注册 ──
add_executable(log_callback_demo src/log_callback_demo.c)
target_link_libraries(log_callback_demo PRIVATE treelock_log treelock_core)

# ── 示例 3：树结构管理 ──
add_executable(tree_usage src/tree_usage.c)
target_link_libraries(tree_usage PRIVATE treelock_tree)

# 复制 JSON 树定义文件到构建根目录（tree_usage 示例从 build/ 运行） */
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/json/filesystem_tree.json"
    "${CMAKE_BINARY_DIR}/json/filesystem_tree.json"
    COPYONLY
)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/json/filesystem_tree_flat.json"
    "${CMAKE_BINARY_DIR}/json/filesystem_tree_flat.json"
    COPYONLY
)

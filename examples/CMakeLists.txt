# ============================================================================
# TreeLocks 示例程序
#
# 示例可执行文件依赖 treelock_core 模块（自动传递 treelock_log 依赖）。
# ============================================================================

# ── 示例 1：基础使用 ──
add_executable(basic_usage basic_usage.c)
target_link_libraries(basic_usage PRIVATE treelock_core)

# ── 示例 2：日志回调注册 ──
add_executable(log_callback_demo log_callback_demo.c)
target_link_libraries(log_callback_demo PRIVATE treelock_log treelock_core)

# ── 示例 3：树结构管理 ──
add_executable(tree_usage tree_usage.c)
target_link_libraries(tree_usage PRIVATE treelock_tree)

# ============================================================================
# TreeLocks 示例程序
#
# 示例可执行文件依赖 treelock_core 模块。
# ============================================================================

add_executable(basic_usage basic_usage.c)

target_link_libraries(basic_usage
    PRIVATE
        treelock_core
)

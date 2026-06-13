# ============================================================================
# TreeLocks 示例
# ============================================================================

add_executable(basic_usage basic_usage.c)

target_link_libraries(basic_usage
    PRIVATE
        treelock
)

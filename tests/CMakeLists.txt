# ============================================================================
# TreeLocks 测试程序
#
# 测试可执行文件依赖 treelock_core 模块。
# ============================================================================

# ── 测试: 协议正确性 ──
add_executable(test_protocol test_protocol.c)
target_link_libraries(test_protocol PRIVATE treelock_core)
# 允许测试代码访问模块私有头文件（src/ 目录）
target_include_directories(test_protocol
    PRIVATE
        ${CMAKE_SOURCE_DIR}/modules/treelock_core/src
)
add_test(NAME test_protocol COMMAND test_protocol)

# ── 测试: 并发压力 ──
add_executable(test_concurrent test_concurrent.c)
target_link_libraries(test_concurrent PRIVATE treelock_core)
target_include_directories(test_concurrent
    PRIVATE
        ${CMAKE_SOURCE_DIR}/modules/treelock_core/src
)
add_test(NAME test_concurrent COMMAND test_concurrent)

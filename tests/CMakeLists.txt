# ============================================================================
# TreeLocks 测试
# ============================================================================

add_executable(test_protocol test_protocol.c)
target_link_libraries(test_protocol PRIVATE treelock)
add_test(NAME test_protocol COMMAND test_protocol)

add_executable(test_concurrent test_concurrent.c)
target_link_libraries(test_concurrent PRIVATE treelock)
add_test(NAME test_concurrent COMMAND test_concurrent)

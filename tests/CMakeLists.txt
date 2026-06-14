# ============================================================================
# TreeLocks 测试程序
#
# 使用 Google Test (GTest) 框架，通过 FetchContent 自动下载。
# 测试文件编译为 C++，通过 extern "C" 调用 C 被测代码。
# ============================================================================

# ── 集成 GTest ──
include(FetchContent)
FetchContent_Declare(
    googletest
    SOURCE_DIR "${CMAKE_SOURCE_DIR}/_deps/googletest"
)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# ── 公共配置 ──
function(treelock_add_test TEST_NAME TEST_SOURCE)
    add_executable(${TEST_NAME} ${TEST_SOURCE})
    target_link_libraries(${TEST_NAME} PRIVATE ${ARGN} gtest_main)
    target_include_directories(${TEST_NAME}
        PRIVATE
            ${CMAKE_SOURCE_DIR}/modules/treelock_core/src
            ${CMAKE_SOURCE_DIR}/modules/treelock_log/src
    )
    # GTest 有一处 MSVC 特有的 signed/unsigned 不匹配警告，关闭它
    if(MSVC)
        target_compile_options(${TEST_NAME} PRIVATE /wd4245 /wd4389)
    endif()
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endfunction()

# ── 测试: 协议正确性 ──
treelock_add_test(test_protocol test_protocol.cc treelock_core)

# ── 测试: 日志模块 ──
treelock_add_test(test_log test_log.cc treelock_log treelock_core)

# ── 测试: 并发压力 ──
treelock_add_test(test_concurrent test_concurrent.cc treelock_core treelock_tree)

# ── 测试: 树结构管理 ──
treelock_add_test(test_tree test_tree.cc treelock_tree)

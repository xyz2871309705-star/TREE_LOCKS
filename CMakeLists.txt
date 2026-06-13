cmake_minimum_required(VERSION 3.16)
project(TreeLocks VERSION 0.1.0 LANGUAGES C)

# ============================================================================
# 编译选项
# ============================================================================
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# 编译警告
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Wconversion)
endif()

if(MSVC)
    add_compile_options(/W4)
endif()

# ============================================================================
# 构建类型
# ============================================================================
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING
        "Build type: Debug, Release, RelWithDebInfo, MinSizeRel" FORCE)
endif()

# ============================================================================
# 子目录
# ============================================================================
add_subdirectory(src)

# 可选：构建示例
option(TREELOCK_BUILD_EXAMPLES "Build example programs" ON)
if(TREELOCK_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# 可选：构建测试
option(TREELOCK_BUILD_TESTS "Build test programs" ON)
if(TREELOCK_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

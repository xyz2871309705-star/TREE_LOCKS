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
# 全局头文件路径
# ============================================================================
# 模块公共头文件汇总目录（方便 IDE 索引）
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/include)

# ============================================================================
# 子模块
# ============================================================================

# 模块 1: 核心锁协议与客户端 API（阶段一）
add_subdirectory(modules/treelock_core)

# 模块 2: 通信层（阶段二/三占位，不产出编译目标）
add_subdirectory(modules/treelock_comm)

# 模块 3: 服务端（阶段三占位，不产出编译目标）
add_subdirectory(modules/treelock_server)

# ============================================================================
# 示例程序
# ============================================================================
option(TREELOCK_BUILD_EXAMPLES "Build example programs" ON)
if(TREELOCK_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# ============================================================================
# 测试程序
# ============================================================================
option(TREELOCK_BUILD_TESTS "Build test programs" ON)
if(TREELOCK_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

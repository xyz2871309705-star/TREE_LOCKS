cmake_minimum_required(VERSION 3.16)
project(TreeLocks VERSION 0.1.0 LANGUAGES C)

# ============================================================================
# LSP 支持: 始终生成 compile_commands.json (clangd / ccls / IDE)
# 并展开 @rsp 响应文件（Windows 上 CMake 默认使用，clangd 无法解析）
#
# clangd 通过 --compile-commands-dir=build 读取，无需项目根副本。
# ============================================================================
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 构建时自动展开 build/compile_commands.json 中的 @rsp 引用
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_FOUND)
    add_custom_target(expand_compile_commands ALL
        COMMAND ${Python3_EXECUTABLE}
            "${CMAKE_SOURCE_DIR}/cmake/expand_cc.py"
            "${CMAKE_BINARY_DIR}"
            -o "${CMAKE_BINARY_DIR}/compile_commands.json"
        COMMENT "展开 compile_commands.json 中的 @rsp (clangd ready)"
        VERBATIM
    )
else()
    message(WARNING "Python3 not found — compile_commands.json @rsp 不会展开，clangd 可能无法跳转")
endif()

# ============================================================================
# 平台检测
# ============================================================================
if(WIN32)
    set(TREELOCK_PLATFORM "windows")
elseif(APPLE)
    set(TREELOCK_PLATFORM "macos")
else()
    set(TREELOCK_PLATFORM "linux")
endif()
message(STATUS "Target platform: ${TREELOCK_PLATFORM} (${CMAKE_SYSTEM_NAME})")
message(STATUS "C compiler:      ${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}")

# ============================================================================
# 编译标准
# ============================================================================
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)  # 需要 GNU 扩展 (##__VA_ARGS__, pthread)

# ============================================================================
# 构建类型
# ============================================================================
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING
        "Build type: Debug, Release, RelWithDebInfo, MinSizeRel" FORCE)
endif()

# ============================================================================
# 库类型选项
# ============================================================================
option(TREELOCK_BUILD_SHARED "Build shared libraries (.dll/.so) instead of static" OFF)

if(TREELOCK_BUILD_SHARED)
    set(TREELOCK_LIB_TYPE SHARED)
    # 构建 DLL 时设置 TREELOCK_BUILD_DLL 以启用 dllexport
    add_compile_definitions(TREELOCK_BUILD_DLL)
else()
    set(TREELOCK_LIB_TYPE STATIC)
    add_compile_definitions(TREELOCK_STATIC)
endif()
message(STATUS "Library type:     ${TREELOCK_LIB_TYPE}")

# ============================================================================
# 线程库查找（跨平台）
# ============================================================================
find_package(Threads REQUIRED)
if(CMAKE_USE_PTHREADS_INIT)
    message(STATUS "Thread library:   pthread (via FindThreads)")
elseif(CMAKE_USE_WIN32_THREADS_INIT)
    message(STATUS "Thread library:   Win32 threads")
endif()

# ============================================================================
# 导入通用模块
# ============================================================================
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(CompilerWarnings)

# ============================================================================
# 安装路径（遵循 GNU 标准）
# ============================================================================
include(GNUInstallDirs)

# ============================================================================
# 子模块
# ============================================================================

# 第三方: 轻量级 JSON 解析器
add_subdirectory(modules/cjson)

# 模块 0: 日志模块
add_subdirectory(modules/treelock_log)

# 模块 1: 核心锁协议与客户端 API
add_subdirectory(modules/treelock_core)

# 模块 1.5: 树结构管理（JSON 加载 + 路径加锁 + 协议校验）
add_subdirectory(modules/treelock_tree)

# 模块 2: 通信层（阶段二/三占位）
add_subdirectory(modules/treelock_comm)

# 模块 3: 服务端（阶段三占位）
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

# ============================================================================
# 构建信息摘要
# ============================================================================
message(STATUS "")
message(STATUS "TreeLocks ${PROJECT_VERSION} configured:")
message(STATUS "  Build type:   ${CMAKE_BUILD_TYPE}")
message(STATUS "  Examples:     ${TREELOCK_BUILD_EXAMPLES}")
message(STATUS "  Tests:        ${TREELOCK_BUILD_TESTS}")
message(STATUS "  Install dir:  ${CMAKE_INSTALL_PREFIX}")

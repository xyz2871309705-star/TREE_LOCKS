# ============================================================================
# 模块: treelock_core — 核心锁协议与客户端 API
#
# 产出: libtreelock_core (静态或动态)
# 依赖: treelock_log, pthread
# ============================================================================

set(MODULE_NAME treelock_core)

# ── 源文件 ──
set(MODULE_SOURCES
    src/protocol.c
    src/lock_table.c
    src/client.c
)

# ── 公共头文件 ──
set(MODULE_PUBLIC_HEADERS
    include/treelock.h
    include/treelock_types.h
    include/treelock_platform.h
)

# ── 私有头文件 ──
set(MODULE_PRIVATE_HEADERS
    src/internal.h
)

# ── 构建库 ──
add_library(${MODULE_NAME} ${TREELOCK_LIB_TYPE}
    ${MODULE_SOURCES}
    ${MODULE_PUBLIC_HEADERS}
    ${MODULE_PRIVATE_HEADERS}
)

# ── 头文件搜索路径 ──
target_include_directories(${MODULE_NAME}
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# ── 链接依赖 ──
target_link_libraries(${MODULE_NAME}
    PUBLIC
        treelock_log
        Threads::Threads
        uthash
)

# ── 警告 ──
set_project_warnings(${MODULE_NAME})

# ── 平台定义 ──
if(WIN32)
    target_compile_definitions(${MODULE_NAME} PRIVATE _WIN32_WINNT=0x0600)
endif()

# ── 安装 ──
install(TARGETS ${MODULE_NAME}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES ${MODULE_PUBLIC_HEADERS}
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${MODULE_NAME}
)

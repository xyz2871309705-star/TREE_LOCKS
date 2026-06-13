# ============================================================================
# 模块: treelock_log — 日志模块
#
# 产出: libtreelock_log (静态或动态)
# 依赖: treelock_types.h + treelock_platform.h, pthread
# ============================================================================

set(MODULE_NAME treelock_log)

# ── 源文件 ──
set(MODULE_SOURCES
    src/log_core.c
)

set(MODULE_PUBLIC_HEADERS
    include/treelock_log.h
)

set(MODULE_PRIVATE_HEADERS
    src/log_internal.h
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
        ${CMAKE_SOURCE_DIR}/modules/treelock_core/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# ── 链接依赖 ──
target_link_libraries(${MODULE_NAME}
    PUBLIC
        Threads::Threads
)

# ── 警告 ──
set_project_warnings(${MODULE_NAME})

# ── 平台定义 ──
if(WIN32)
    target_compile_definitions(${MODULE_NAME} PRIVATE _WIN32_WINNT=0x0600)
endif()

# ── Release 构建裁剪 DEBUG/TRACE ──
target_compile_definitions(${MODULE_NAME}
    PRIVATE
        $<$<CONFIG:Release>:NDEBUG>
)

# ── 安装 ──
install(TARGETS ${MODULE_NAME}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES ${MODULE_PUBLIC_HEADERS}
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${MODULE_NAME}
)

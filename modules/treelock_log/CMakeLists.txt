# ============================================================================
# 模块: treelock_log — 日志模块
#
# 职责:
#   - 6 级日志等级（FATAL/ERROR/WARN/INFO/DEBUG/TRACE）
#   - 编译期 + 运行期双重输出控制
#   - 外部回调注册（自定义日志输出）
#   - 线程安全 + 递归保护
#
# 产出: libtreelock_log.a (静态库)
# 依赖: treelock_types.h（类型封装宏）, pthread（线程安全）
# ============================================================================

set(MODULE_NAME treelock_log)

# ── 源文件 ──
set(MODULE_SOURCES
    src/log_core.c
)

# ── 头文件 ──
set(MODULE_PUBLIC_HEADERS
    include/treelock_log.h
)

set(MODULE_PRIVATE_HEADERS
    src/log_internal.h
)

# ── 构建静态库 ──
add_library(${MODULE_NAME} STATIC
    ${MODULE_SOURCES}
    ${MODULE_PUBLIC_HEADERS}
    ${MODULE_PRIVATE_HEADERS}
)

# ── 头文件搜索路径 ──
target_include_directories(${MODULE_NAME}
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/modules/treelock_core/include  # treelock_types.h
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# ── 链接依赖 ──
target_link_libraries(${MODULE_NAME}
    PUBLIC
        pthread
)

# ── 编译定义：Release 构建裁掉 DEBUG/TRACE ──
target_compile_definitions(${MODULE_NAME}
    PRIVATE
        $<$<CONFIG:Release>:NDEBUG>
)

# ── 安装规则 ──
install(TARGETS ${MODULE_NAME}
    ARCHIVE DESTINATION lib
)

install(FILES ${MODULE_PUBLIC_HEADERS}
    DESTINATION include/${MODULE_NAME}
)

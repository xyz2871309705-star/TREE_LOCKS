# ============================================================================
# 模块: treelock_core — 核心锁协议与客户端 API
#
# 职责:
#   - 多粒度意向锁兼容矩阵
#   - 锁模式转换与升级/降级路径验证
#   - 内存锁表管理（授权列表、等待队列）
#   - 线程安全客户端 API
#
# 产出: libtreelock_core.a (静态库)
# 依赖: pthread (系统库), treelock_types.h (本模块)
# ============================================================================

set(MODULE_NAME treelock_core)

# ── 源文件 ──
set(MODULE_SOURCES
    src/protocol.c
    src/lock_table.c
    src/client.c
)

# ── 头文件 ──
set(MODULE_PUBLIC_HEADERS
    include/treelock.h
    include/treelock_types.h
)

set(MODULE_PRIVATE_HEADERS
    src/internal.h
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
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# ── 链接依赖 ──
target_link_libraries(${MODULE_NAME}
    PUBLIC
        pthread
)

# ── 安装规则 ──
install(TARGETS ${MODULE_NAME}
    ARCHIVE DESTINATION lib
)

install(FILES ${MODULE_PUBLIC_HEADERS}
    DESTINATION include/${MODULE_NAME}
)

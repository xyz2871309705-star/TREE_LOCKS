# ============================================================================
# 模块: treelock_tree — 树结构管理
#
# 职责:
#   - JSON 树定义文件解析与加载
#   - 树结构校验（环检测/ID 唯一/单根）
#   - 路径加锁/解锁
#   - 协议自动校验（lock 时检查祖先锁）
#
# 产出: libtreelock_tree (静态库)
# 依赖: treelock_core, cjson
# ============================================================================

set(MODULE_NAME treelock_tree)

# ── 源文件 ──
set(MODULE_SOURCES
    src/tree_core.c
    src/tree_json.c
    src/tree_validate.c
    src/tree_path.c
    src/tree_api.c
)

# ── 公共头文件 ──
set(MODULE_PUBLIC_HEADERS
    include/treelock_tree.h
)

# ── 私有头文件 ──
set(MODULE_PRIVATE_HEADERS
    src/tree_internal.h
)

# ── 构建库 ──
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
        # 需要访问 treelock_core 内部结构 (internal.h 中的 treelock_s)
        ${CMAKE_SOURCE_DIR}/modules/treelock_core/src
)

# ── 链接依赖 ──
target_link_libraries(${MODULE_NAME}
    PUBLIC
        treelock_core
    PRIVATE
        cjson
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
)

install(FILES ${MODULE_PUBLIC_HEADERS}
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${MODULE_NAME}
)

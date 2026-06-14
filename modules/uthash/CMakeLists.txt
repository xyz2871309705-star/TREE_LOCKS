# ============================================================================
# 模块: uthash — C 哈希表库 (第三方, header-only)
#
# 来源: Troy D. Hanson, v2.3.0, MIT License
# 产出: uthash (INTERFACE 库，纯头文件)
# 依赖: 无
# ============================================================================

set(MODULE_NAME uthash)

add_library(${MODULE_NAME} INTERFACE)

target_include_directories(${MODULE_NAME}
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# ── 安装 ──
install(FILES uthash.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${MODULE_NAME}
)

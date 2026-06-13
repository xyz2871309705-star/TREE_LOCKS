# ============================================================================
# 模块: cjson — 轻量级 JSON 解析器 (第三方)
#
# 来源: Dave Gamble, v1.7.18, MIT License
# 产出: libcjson (静态库)
# 依赖: 无 (纯 C99 实现)
# ============================================================================

set(MODULE_NAME cjson)

# ── 源文件 ──
set(MODULE_SOURCES
    cJSON.c
)

# ── 公共头文件 ──
set(MODULE_PUBLIC_HEADERS
    cJSON.h
)

# ── 构建库 ──
add_library(${MODULE_NAME} STATIC
    ${MODULE_SOURCES}
    ${MODULE_PUBLIC_HEADERS}
)

# ── 头文件搜索路径 ──
target_include_directories(${MODULE_NAME}
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# ── cJSON 需要宽松的 C 标准 ──
target_compile_options(${MODULE_NAME} PRIVATE
    $<$<C_COMPILER_ID:MSVC>:/W0>
    $<$<NOT:$<C_COMPILER_ID:MSVC>>:-w>
)

# ── 安装 ──
install(TARGETS ${MODULE_NAME}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(FILES ${MODULE_PUBLIC_HEADERS}
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${MODULE_NAME}
)

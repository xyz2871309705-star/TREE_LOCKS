# ============================================================================
# 编译器警告配置
#
# 为不同编译器设置合理的警告级别，隔离平台差异。
# ============================================================================

function(set_project_warnings TARGET_NAME)
    # ── GNU / Clang ──
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${TARGET_NAME} PRIVATE
            -Wall
            -Wextra
            -Wshadow
            -Wconversion
            # 不启用 -Wpedantic：日志宏依赖 GNU ##__VA_ARGS__
        )
    endif()

    # ── MSVC ──
    if(CMAKE_C_COMPILER_ID MATCHES "MSVC")
        target_compile_options(${TARGET_NAME} PRIVATE
            /W4           # 高警告级别
            /wd4100       # 未引用形参 (IN/OUT 宏会导致大量误报)
            /wd4204       # C99 匿名结构体 (非标准扩展)
        )
    endif()
endfunction()

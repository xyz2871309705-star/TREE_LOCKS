# ============================================================================
# 展开 compile_commands.json 中的 @xxx.rsp 响应文件引用
#
# CMake 在 Windows 上默认使用响应文件传递 include 路径，
# 但 clangd 无法解析 @file.rsp 语法，导致代码跳转失败。
# 此脚本将 @rsp 引用原地替换为实际文件内容。
# ============================================================================

function(expand_compile_commands BUILD_DIR)
    set(CC_FILE "${CMAKE_SOURCE_DIR}/compile_commands.json")
    set(BUILD_CC  "${BUILD_DIR}/compile_commands.json")

    if(NOT EXISTS "${BUILD_CC}")
        message(WARNING "compile_commands.json not found in ${BUILD_DIR}")
        return()
    endif()

    # 复制到项目根目录
    file(COPY "${BUILD_CC}" DESTINATION "${CMAKE_SOURCE_DIR}")

    # 使用 CMake 内置命令展开 @rsp 引用
    file(READ "${CC_FILE}" CC_CONTENT)

    # 正则匹配 @路径/to/file.rsp
    string(REGEX MATCHALL "@[^ \"]*\\.rsp" RSP_REFS "${CC_CONTENT}")

    foreach(RSP_REF IN LISTS RSP_REFS)
        string(SUBSTRING "${RSP_REF}" 1 -1 RSP_REL_PATH)  # 去掉开头的 @
        set(RSP_FULL "${BUILD_DIR}/${RSP_REL_PATH}")

        if(EXISTS "${RSP_FULL}")
            file(READ "${RSP_FULL}" RSP_CONTENT)
            string(STRIP "${RSP_CONTENT}" RSP_CONTENT)
            # 替换命令中的单个 @rsp 引用
            string(REPLACE "${RSP_REF}" "${RSP_CONTENT}" CC_CONTENT "${CC_CONTENT}")
        endif()
    endforeach()

    file(WRITE "${CC_FILE}" "${CC_CONTENT}")
    message(STATUS "compile_commands.json expanded (clangd ready)")
endfunction()

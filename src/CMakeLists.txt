# ============================================================================
# TreeLocks 核心库
# ============================================================================

set(TREELOCK_SOURCES
    protocol.c
    lock_table.c
    client.c
)

set(TREELOCK_HEADERS
    ${CMAKE_SOURCE_DIR}/include/treelock.h
    internal.h
)

add_library(treelock STATIC ${TREELOCK_SOURCES} ${TREELOCK_HEADERS})

target_include_directories(treelock
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(treelock
    PUBLIC
        pthread
)

# 安装规则
install(TARGETS treelock
    ARCHIVE DESTINATION lib
)

install(FILES ${CMAKE_SOURCE_DIR}/include/treelock.h
    DESTINATION include
)

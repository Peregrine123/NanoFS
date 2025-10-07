cmake_minimum_required(VERSION 3.20)
project(modernfs C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ===== 构建Rust库 (可选，Week 5需要) =====
find_program(CARGO_EXECUTABLE cargo)
if(CARGO_EXECUTABLE)
    add_custom_target(rust_core ALL
        COMMAND cargo build --release
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Building Rust core library..."
    )
    message(STATUS "Cargo found: ${CARGO_EXECUTABLE}")
else()
    message(WARNING "Cargo not found, skipping Rust core library (only needed for Week 5+)")
    add_custom_target(rust_core)
endif()

# ===== C编译选项 =====
add_compile_options(
    -Wall -Wextra
    -D_FILE_OFFSET_BITS=64
)

# ===== 头文件目录 =====
include_directories(
    ${CMAKE_SOURCE_DIR}/include
)

# ===== FFI测试可执行文件 =====
add_executable(test_ffi
    src/test_ffi.c
)

add_dependencies(test_ffi rust_core)

# ===== 链接Rust静态库 =====
target_link_libraries(test_ffi
    ${CMAKE_SOURCE_DIR}/target/release/librust_core.a
    pthread
    dl
    m
)

# Windows特定配置
if(WIN32)
    target_link_libraries(test_ffi
        ws2_32
        userenv
        bcrypt
    )
endif()

# ===== 块设备层测试可执行文件 =====
add_executable(test_block_layer
    src/test_block_layer.c
    src/block_dev.c
    src/buffer_cache.c
    src/block_alloc.c
)

target_link_libraries(test_block_layer
    pthread
    m
)

if(WIN32)
    target_link_libraries(test_block_layer
        ws2_32
    )
endif()

# ===== Inode层测试可执行文件 =====
add_executable(test_inode_layer
    src/test_inode_layer.c
    src/block_dev.c
    src/buffer_cache.c
    src/block_alloc.c
    src/inode.c
    src/directory.c
    src/path.c
)

target_link_libraries(test_inode_layer
    pthread
    m
)

if(WIN32)
    target_link_libraries(test_inode_layer
        ws2_32
    )
endif()

# ===== 简化目录测试 =====
add_executable(test_dir_simple
    src/test_dir_simple.c
    src/block_dev.c
    src/buffer_cache.c
    src/block_alloc.c
    src/inode.c
    src/directory.c
)

target_link_libraries(test_dir_simple
    pthread
    m
)

if(WIN32)
    target_link_libraries(test_dir_simple
        ws2_32
    )
endif()

# ===== Journal测试可执行文件 (Week 5) =====
add_executable(test_journal
    src/test_journal.c
)

add_dependencies(test_journal rust_core)

target_link_libraries(test_journal
    ${CMAKE_SOURCE_DIR}/target/release/librust_core.a
    pthread
    dl
    m
)

if(WIN32)
    target_link_libraries(test_journal
        ws2_32
        userenv
        bcrypt
    )
endif()

# ===== FUSE支持 (仅Linux) =====
if(UNIX AND NOT APPLE)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FUSE3 REQUIRED fuse3)

    # mkfs.modernfs - 格式化工具
    add_executable(mkfs.modernfs
        src/mkfs.c
        src/block_dev.c
        src/buffer_cache.c
        src/superblock.c
    )

    target_link_libraries(mkfs.modernfs
        pthread
        m
    )

    # modernfs - FUSE主程序
    add_executable(modernfs
        src/main_fuse.c
        src/fuse_ops.c
        src/fs_context.c
        src/superblock.c
        src/block_dev.c
        src/buffer_cache.c
        src/block_alloc.c
        src/inode.c
        src/directory.c
        src/path.c
    )

    target_include_directories(modernfs PRIVATE ${FUSE3_INCLUDE_DIRS})
    target_compile_options(modernfs PRIVATE ${FUSE3_CFLAGS_OTHER})

    target_link_libraries(modernfs
        ${FUSE3_LIBRARIES}
        pthread
        m
    )

    message(STATUS "FUSE3 found: ${FUSE3_VERSION}")
    message(STATUS "Building ModernFS FUSE driver")
else()
    message(WARNING "FUSE is only supported on Linux, skipping FUSE targets")
endif()

# ===== 安装 =====
if(UNIX AND NOT APPLE)
    install(TARGETS test_ffi test_block_layer test_inode_layer test_dir_simple test_journal mkfs.modernfs modernfs DESTINATION bin)
else()
    install(TARGETS test_ffi test_block_layer test_inode_layer test_dir_simple test_journal DESTINATION bin)
endif()
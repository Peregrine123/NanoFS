cmake_minimum_required(VERSION 3.20)
project(modernfs C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ===== 构建Rust库 =====
add_custom_target(rust_core ALL
    COMMAND cargo build --release
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Building Rust core library..."
)

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

# ===== 安装 =====
install(TARGETS test_ffi DESTINATION bin)
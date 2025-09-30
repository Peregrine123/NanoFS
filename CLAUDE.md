# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 项目概述

**ModernFS** 是一个用于操作系统课程的 C + Rust 混合架构文件系统项目。

- **C 层**: FUSE 接口、块设备管理、Inode 和目录操作
- **Rust 层**: WAL 日志系统、Extent 分配器、事务管理
- **FFI**: C 与 Rust 通过 `include/modernfs/rust_ffi.h` 进行互操作

## 核心命令

### 环境验证
```bash
# Windows
check_env.bat

# Linux/macOS
./check_env.sh
```

### 构建项目
```bash
# Windows
build.bat

# Linux/macOS
./build.sh
```

构建流程:
1. Cargo 构建 Rust 静态库 (`target/release/librust_core.a`)
2. CMake 编译 C 代码并链接 Rust 库

### 测试
```bash
# FFI 测试 (Windows)
build\test_ffi.exe

# FFI 测试 (Linux/macOS)
./build/test_ffi

# Rust 单元测试
cargo test

# 单个模块测试
cargo test -p rust_core --lib journal
```

### 清理
```bash
# 清理构建产物
rm -rf build/ target/

# Windows
rmdir /s /q build target
```

## 架构要点

### Workspace 结构
```
Cargo.toml (workspace root)
├── rust_core/           # C-FFI 静态库
├── tools/mkfs-rs/       # mkfs.modernfs 工具
├── tools/fsck-rs/       # fsck.modernfs 工具
└── tools/benchmark-rs/  # 性能测试工具
```

### C-Rust FFI 边界
- **头文件**: `include/modernfs/rust_ffi.h`
- **Rust 导出**: `rust_core/src/lib.rs` 中的 `#[no_mangle] pub extern "C"` 函数
- **不透明指针**: C 侧使用 `RustJournalManager*`、`RustExtentAllocator*` 等不透明类型
- **错误处理**: Rust 返回负数 errno 或 NULL 指针表示错误

### 模块职责划分
- **journal/**: WAL (Write-Ahead Log) 实现,保证崩溃一致性
- **extent/**: Extent-based 块分配器,使用 bitvec 位图管理
- **transaction/**: 事务封装,提供原子性保证

### 开发阶段
```
✅ Phase 0: 环境搭建、FFI 骨架
⏳ Phase 1: C 基础实现 (块设备、Inode、目录)
⏳ Phase 2: Rust 核心模块 (Journal、Extent、事务)
⏳ Phase 3: Rust 工具集 (mkfs、fsck、benchmark)
```

## 编码约定

### Rust
- 使用 `anyhow::Result` 进行错误传播
- FFI 函数必须用 `catch_panic` 包装避免 unwinding 跨越 C 边界
- 所有公开的 FFI 接口在 `lib.rs` 中导出

### C
- 使用 C11 标准
- 所有 Rust 指针视为不透明,禁止解引用
- 通过 FFI 调用 Rust 前必须检查返回值 (NULL/-1)

## 常见任务

### 添加新的 FFI 函数
1. 在 `include/modernfs/rust_ffi.h` 声明 C 接口
2. 在 `rust_core/src/lib.rs` 实现 `#[no_mangle] pub extern "C"` 函数
3. 重新运行 `build.bat` / `./build.sh`

### 修改 Rust 核心模块后测试
```bash
cargo test -p rust_core
# 然后重新构建完整项目
build.bat  # 或 ./build.sh
```

### 跨平台注意事项
- Windows 需要额外链接 `ws2_32`, `userenv`, `bcrypt` (见 `CMakeLists.txt`)
- 路径分隔符使用 CMake 变量 `${CMAKE_SOURCE_DIR}`
- WSL 用户可使用 Linux 构建流程

## 故障排查

### "Rust 命令未找到"
```bash
# Windows: 重启终端或手动添加到 PATH
%USERPROFILE%\.cargo\bin

# Linux/macOS
source $HOME/.cargo/env
```

### 链接错误 "undefined reference to rust_xxx"
1. 确认 `cargo build --release` 成功生成 `librust_core.a`
2. 检查 `CMakeLists.txt` 中的 `target_link_libraries` 路径
3. 删除 `build/` 和 `target/` 目录后重新构建

### CMake 找不到 Rust 库
确保构建顺序正确:
```cmake
add_dependencies(test_ffi rust_core)
```

## 参考文档
- `ModernFS_Hybrid_Plan.md`: 完整设计文档
- `QUICKSTART.md`: 快速开始指南
- `README.md`: 项目概览
# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 项目概述

**ModernFS** 是一个用于操作系统课程的 C + Rust 混合架构文件系统项目。

- **C 层**: FUSE 接口、块设备管理、Inode 和目录操作
- **Rust 层**: WAL 日志系统、Extent 分配器、事务管理
- **FFI**: C 与 Rust 通过 `include/modernfs/rust_ffi.h` 进行互操作

**当前进度**: Week 2 完成 ✅
- ✅ 块设备层 (block_dev.c, 270行)
- ✅ 缓冲区缓存 (buffer_cache.c, 362行)
- ✅ 块分配器 (block_alloc.c, 350行)
- ✅ 测试套件 (test_block_layer.c, 314行)

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

# 块设备层测试 (Week 2) ✅
./build/test_block_layer

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

**C模块** (已实现):
- **block_dev.c**: 块设备IO层,提供读写同步接口 ✅
- **buffer_cache.c**: LRU缓存,哈希表+双向链表实现 ✅
- **block_alloc.c**: 位图块分配器,支持连续块分配 ✅

**C模块** (待实现):
- **inode.c**: Inode管理,缓存,数据块映射
- **directory.c**: 目录项管理
- **path.c**: 路径解析

**Rust模块** (待实现):
- **journal/**: WAL (Write-Ahead Log) 实现,保证崩溃一致性
- **extent/**: Extent-based 块分配器,使用 bitvec 位图管理
- **transaction/**: 事务封装,提供原子性保证

### 开发阶段
```
✅ Week 1 (Phase 0): 环境搭建、FFI 骨架
✅ Week 2: C 块设备层实现
   - 块设备IO (270行)
   - 缓冲区缓存 (362行)
   - 块分配器 (350行)
   - 测试套件 (314行)
⏳ Week 3: Inode与目录管理
⏳ Week 4: FUSE集成
⏳ Phase 2 (Week 5-7): Rust 核心模块 (Journal、Extent、事务)
⏳ Phase 3 (Week 8): Rust 工具集 (mkfs、fsck、benchmark)
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
- **内存对齐**: 使用 `posix_memalign()` 而非 `aligned_alloc()` (更可靠)
- **结构体**: 使用 `__attribute__((packed))` 确保磁盘结构无填充
- **线程安全**: 使用 `pthread_rwlock_t` 保护共享数据,`pthread_mutex_t` 保护元数据
- **错误处理**: 所有公共API返回错误码,负数表示errno

## 常见任务

### Week 2 完成的模块使用

#### 块设备操作
```c
#include "modernfs/block_dev.h"

// 打开设备
block_device_t *dev = blkdev_open("disk.img");

// 读取块
uint8_t buf[BLOCK_SIZE];
blkdev_read(dev, block_num, buf);

// 写入块
blkdev_write(dev, block_num, data);

// 同步到磁盘
blkdev_sync(dev);

// 关闭设备
blkdev_close(dev);
```

#### 块分配器使用
```c
#include "modernfs/block_alloc.h"

// 初始化分配器
block_allocator_t *alloc = block_alloc_init(dev, ...);

// 分配单个块
block_t block = block_alloc(alloc);

// 分配多个连续块
block_t start;
uint32_t count;
block_alloc_multiple(alloc, 10, &start, &count);

// 释放块
block_free(alloc, block);

// 同步位图
block_alloc_sync(alloc);
```

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
# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 项目概述

**ModernFS** 是一个用于操作系统课程的 C + Rust 混合架构文件系统项目。

- **C 层**: FUSE 接口、块设备管理、Inode 和目录操作
- **Rust 层**: WAL 日志系统、Extent 分配器、事务管理
- **FFI**: C 与 Rust 通过 `include/modernfs/rust_ffi.h` 进行互操作

**当前进度**: Week 4 完成 ✅
- ✅ Week 1-3: 块设备层、Inode层、目录管理、路径解析 (~3000行)
- ✅ FUSE操作接口 (fuse_ops.c, ~668行)
- ✅ FUSE主程序 (main_fuse.c, ~133行)
- ✅ 文件系统上下文 (fs_context.c, ~179行)
- ✅ 超级块管理 (superblock.c, ~161行)
- ✅ mkfs工具 (mkfs.c, ~257行)
- ✅ Week 4: FUSE集成完成
  - ✅ 文件系统挂载/卸载
  - ✅ 目录读取 (readdir)
  - ✅ 目录创建 (mkdir)
  - ✅ 文件创建 (create)
  - ✅ 文件统计 (stat, statfs)
  - ⚠️  文件读写功能待完善

## 核心命令

**重要提示**: Windows环境下，FUSE相关功能需要在WSL (Windows Subsystem for Linux) 中运行。基础测试可以在Windows原生环境运行。

### 环境验证
```bash
# Windows原生
check_env.bat

# Linux/macOS/WSL
./check_env.sh
```

### 构建项目

**Week 1-3 基础测试 (Windows原生或WSL)**:
```bash
# Windows原生
build.bat

# Linux/macOS/WSL
./build.sh
```

**Week 4+ FUSE功能 (必须在WSL中)**:
```bash
# 在WSL中执行
cd /mnt/e/ampa_migra/D/校务/大三上/OS/NanoFS  # 根据实际路径调整
./build.sh
```

构建流程:
1. Cargo 构建 Rust 静态库 (`target/release/librust_core.a`)
2. CMake 编译 C 代码并链接 Rust 库
3. WSL环境会额外构建 `mkfs.modernfs` 和 `modernfs` FUSE可执行文件

### 测试

**基础测试 (Windows原生或WSL)**:
```bash
# FFI 测试
./build/test_ffi

# 块设备层测试 (Week 2)
./build/test_block_layer

# Inode层测试 (Week 3)
./build/test_inode_layer

# 简化目录测试
./build/test_dir_simple

# Rust 单元测试
cargo test

# 单个模块测试
cargo test -p rust_core --lib journal
```

**FUSE集成测试 (必须在WSL中)**:
```bash
# 1. 创建100MB磁盘镜像并格式化
./build/mkfs.modernfs /tmp/test.img 100

# 2. 创建挂载点
mkdir -p /tmp/mnt

# 3. 挂载文件系统 (前台运行，调试模式)
./build/modernfs /tmp/test.img /tmp/mnt -f

# 4. 在另一个终端测试文件操作
cd /tmp/mnt
mkdir testdir
echo "Hello ModernFS" > testdir/hello.txt
cat testdir/hello.txt
ls -la
cd ..

# 5. 卸载 (按Ctrl+C或另一个终端执行)
fusermount -u /tmp/mnt
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
- **inode.c**: Inode管理,LRU缓存,数据块映射(直接/间接/二级间接) ✅
- **directory.c**: 目录项管理,增删查,变长目录项 ✅
- **path.c**: 路径解析,规范化,basename/dirname ✅

**C模块** (待实现):
- **fuse_ops.c**: FUSE操作接口

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
✅ Week 3: Inode与目录管理
   - Inode管理 (~900行): LRU缓存、位图分配、数据块映射
   - 目录管理 (~400行): 增删查、遍历、变长目录项
   - 路径解析 (~350行): 规范化、basename/dirname
   - 测试套件 (5个测试，100%通过率)
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

### Week 3 完成的模块使用

#### Inode操作
```c
#include "modernfs/inode.h"

// 初始化Inode缓存
inode_cache_t *icache = inode_cache_init(dev, balloc, 64, 32);

// 分配新Inode
inode_t_mem *inode = inode_alloc(icache, INODE_TYPE_FILE);

// 获取已有Inode
inode_t_mem *inode = inode_get(icache, inum);

// 锁定Inode
inode_lock(inode);

// 读写数据
ssize_t read = inode_read(icache, inode, buf, offset, size);
ssize_t written = inode_write(icache, inode, buf, offset, size);

// 同步到磁盘
inode_sync(icache, inode);

// 释放Inode
inode_unlock(inode);
inode_put(icache, inode);
```

#### 目录操作
```c
#include "modernfs/directory.h"

// 在目录中添加文件
dir_add(icache, dir_inode, "file.txt", file_inum);

// 查找文件
inode_t found_inum;
dir_lookup(icache, dir_inode, "file.txt", &found_inum);

// 删除文件
dir_remove(icache, dir_inode, "file.txt");

// 遍历目录
int callback(const char *name, inode_t inum, void *arg) {
    printf("%s: %u\n", name, inum);
    return 0;
}
dir_iterate(icache, dir_inode, callback, NULL);
```

#### 路径解析
```c
#include "modernfs/path.h"

// 规范化路径
char normalized[256];
path_normalize("/a/b/../c/./d", normalized, sizeof(normalized));
// 结果: "/a/c/d"

// 解析路径获取Inode
inode_t_mem *inode = path_resolve(icache, root_inum, cwd_inum,
                                   "/foo/bar/file.txt", true);

// 解析父目录
inode_t_mem *parent;
char filename[256];
path_resolve_parent(icache, root_inum, cwd_inum,
                    "/foo/bar/file.txt", &parent, filename);

// 提取basename
const char *name = path_basename("/foo/bar/file.txt");  // "file.txt"

// 提取dirname
char dirname[256];
path_dirname("/foo/bar/file.txt", dirname, sizeof(dirname));  // "/foo/bar"
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
- **Windows原生环境**: 支持Week 1-3的基础测试 (block_dev, inode, directory等)
- **FUSE功能**: 仅支持Linux，Windows用户需要使用WSL
- **WSL路径映射**: Windows的 `E:\ampa_migra\D\校务\大三上\OS\NanoFS` 在WSL中为 `/mnt/e/ampa_migra/D/校务/大三上/OS/NanoFS`
- **WSL依赖安装**:
  ```bash
  # 在WSL中安装必要依赖
  sudo apt-get update
  sudo apt-get install build-essential cmake libfuse3-dev pkg-config fuse3
  ```
- 路径分隔符使用 CMake 变量 `${CMAKE_SOURCE_DIR}`

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
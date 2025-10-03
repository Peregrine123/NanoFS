# ModernFS混合架构实施计划（大作业优化版）

**项目代号**: ModernFS Hybrid
**版本**: 2.0
**最后更新**: 2025-09-30
**目标**: 大作业高分方案（C + Rust混合架构）

---

## 一、项目定位与得分策略

### 1.1 核心优势

```
混合架构 = C的稳定性 + Rust的创新性
         = 保底分数 + 加分项
         = 降低风险 + 提升上限
```

### 1.2 评分点优化

| 维度 | 纯C方案 | 混合方案 | 提升 |
|------|---------|---------|------|
| **基础功能** | 40分 | 40分 | 0 |
| **技术创新** | 20分 | 30分 | +10 |
| **工程质量** | 12分 | 15分 | +3 |
| **展示效果** | 8分 | 10分 | +2 |
| **总分** | 80分 | 95分 | **+15** |

### 1.3 技术亮点

1. ⭐ **内存安全**: Rust编译器保证，零运行时开销
2. ⭐ **并发安全**: 类型系统防止数据竞争
3. ⭐ **崩溃一致性**: WAL日志 + 自动恢复
4. ⭐ **性能优化**: Extent分配 + FUSE缓存策略
5. ⭐ **可视化调试**: .debug/虚拟目录实时监控

---

## 二、模块分工设计

### 2.1 总体架构

```
┌─────────────────────────────────────────────────────────┐
│            Linux VFS + FUSE Kernel Module                │
└──────────────────┬──────────────────────────────────────┘
                   │ /dev/fuse
┌──────────────────▼──────────────────────────────────────┐
│                FUSE Interface Layer (C)                  │
│  得分点: POSIX接口理解 + 系统编程能力                    │
├─────────────────────────────────────────────────────────┤
│  • fuse_ops.c      - FUSE回调实现                        │
│  • fuse_bridge.c   - C/Rust FFI桥接                      │
│  • main.c          - 程序入口                            │
└──────────────────┬──────────────────────────────────────┘
                   │ FFI调用
┌──────────────────▼──────────────────────────────────────┐
│             Core Modules (C + Rust混合)                  │
├─────────────────────────────────────────────────────────┤
│  🦀 Rust模块 (安全关键)                                  │
│  ├─ journal_manager    - WAL日志系统                     │
│  ├─ transaction        - 事务管理                        │
│  └─ extent_allocator   - Extent分配器                    │
│                                                           │
│  🔵 C模块 (性能关键 + 基础设施)                          │
│  ├─ inode              - Inode管理                       │
│  ├─ directory          - 目录管理                        │
│  ├─ block_dev          - 块设备IO                        │
│  └─ buffer_cache       - 块缓存                          │
└──────────────────┬──────────────────────────────────────┘
                   │
┌──────────────────▼──────────────────────────────────────┐
│              Disk Image (disk.img)                       │
│  [SuperBlock | Journal | Bitmap | Inode | Data]         │
└─────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────┐
│                Tools & Testing (Rust) ⭐加分项           │
├─────────────────────────────────────────────────────────┤
│  • mkfs.modernfs-rs    - 格式化工具 (CLI友好)            │
│  • fsck.modernfs-rs    - 文件系统检查                    │
│  • debugfs-rs          - 交互式调试工具                  │
│  • benchmark-rs        - 性能测试套件                    │
└─────────────────────────────────────────────────────────┘
```

### 2.2 语言选择理由

#### Rust实现的模块：

**Journal Manager**
- ✅ 复杂的状态机（Active/Committing/Committed）
- ✅ 并发写入（多线程安全要求高）
- ✅ 错误传播（Result类型强制处理）
- ✅ RAII模式（Transaction Drop自动回滚）

**Extent Allocator**
- ✅ 复杂算法（First-Fit搜索）
- ✅ 并发访问位图（RwLock保护）
- ✅ 统计信息（原子操作）

#### C实现的模块：

**Inode/Directory**
- ✅ 直接操作磁盘结构（packed struct）
- ✅ 与xv6代码风格一致
- ✅ 简单的CRUD操作

**Block Device**
- ✅ 系统调用密集（pread/pwrite）
- ✅ 性能关键路径（减少FFI开销）

---

## 三、FFI接口设计

### 3.1 C侧接口定义

```c
// include/modernfs/rust_ffi.h

#ifndef MODERNFS_RUST_FFI_H
#define MODERNFS_RUST_FFI_H

#include <stdint.h>
#include <stdbool.h>

// ============ 不透明类型 ============

typedef struct RustJournalManager RustJournalManager;
typedef struct RustTransaction RustTransaction;
typedef struct RustExtentAllocator RustExtentAllocator;

// ============ Journal API ============

/**
 * 初始化日志管理器
 * @return NULL表示失败
 */
RustJournalManager* rust_journal_init(
    int device_fd,
    uint32_t journal_start,
    uint32_t journal_blocks
);

/**
 * 开始事务
 */
RustTransaction* rust_journal_begin(RustJournalManager* jm);

/**
 * 记录块写入
 * @param data 必须是BLOCK_SIZE(4096)字节
 * @return 0成功，负数为errno
 */
int rust_journal_write(
    RustTransaction* txn,
    uint32_t block_num,
    const uint8_t* data
);

/**
 * 提交事务（txn会被消费，不能再使用）
 */
int rust_journal_commit(
    RustJournalManager* jm,
    RustTransaction* txn
);

/**
 * 中止事务
 */
void rust_journal_abort(RustTransaction* txn);

/**
 * 检查点
 */
int rust_journal_checkpoint(RustJournalManager* jm);

/**
 * 崩溃恢复
 * @return 恢复的事务数量，负数为错误
 */
int rust_journal_recover(RustJournalManager* jm);

/**
 * 销毁
 */
void rust_journal_destroy(RustJournalManager* jm);

// ============ Extent Allocator API ============

RustExtentAllocator* rust_extent_alloc_init(
    int device_fd,
    uint32_t bitmap_start,
    uint32_t total_blocks
);

/**
 * 分配Extent
 */
int rust_extent_alloc(
    RustExtentAllocator* alloc,
    uint32_t hint,
    uint32_t min_len,
    uint32_t max_len,
    uint32_t* out_start,  // 输出参数
    uint32_t* out_len
);

/**
 * 释放Extent
 */
int rust_extent_free(
    RustExtentAllocator* alloc,
    uint32_t start,
    uint32_t len
);

/**
 * 获取碎片率（0.0-1.0）
 */
float rust_extent_fragmentation(RustExtentAllocator* alloc);

void rust_extent_alloc_destroy(RustExtentAllocator* alloc);

#endif // MODERNFS_RUST_FFI_H
```

### 3.2 Rust侧实现框架

```rust
// rust_core/src/lib.rs

mod journal;
mod extent;
mod transaction;

pub use journal::JournalManager;
pub use extent::ExtentAllocator;

use std::os::unix::io::RawFd;
use std::ptr;
use std::ffi::c_void;

// ============ 错误处理辅助 ============

fn catch_panic<F, R>(f: F) -> R
where
    F: FnOnce() -> R + std::panic::UnwindSafe,
    R: Default,
{
    std::panic::catch_unwind(f).unwrap_or_else(|e| {
        eprintln!("Rust panic: {:?}", e);
        R::default()
    })
}

// ============ Journal FFI ============

#[no_mangle]
pub extern "C" fn rust_journal_init(
    device_fd: i32,
    journal_start: u32,
    journal_blocks: u32,
) -> *mut c_void {
    catch_panic(|| {
        match JournalManager::new(device_fd, journal_start, journal_blocks) {
            Ok(jm) => Box::into_raw(Box::new(jm)) as *mut c_void,
            Err(e) => {
                eprintln!("rust_journal_init failed: {:?}", e);
                ptr::null_mut()
            }
        }
    })
}

#[no_mangle]
pub extern "C" fn rust_journal_begin(
    jm_ptr: *mut c_void
) -> *mut c_void {
    if jm_ptr.is_null() {
        return ptr::null_mut();
    }

    catch_panic(|| {
        let jm = unsafe { &mut *(jm_ptr as *mut JournalManager) };
        match jm.begin_transaction() {
            Ok(txn) => Box::into_raw(Box::new(txn)) as *mut c_void,
            Err(e) => {
                eprintln!("begin_transaction failed: {:?}", e);
                ptr::null_mut()
            }
        }
    })
}

#[no_mangle]
pub extern "C" fn rust_journal_write(
    txn_ptr: *mut c_void,
    block_num: u32,
    data: *const u8,
) -> i32 {
    if txn_ptr.is_null() || data.is_null() {
        return -libc::EINVAL;
    }

    catch_panic(|| {
        let txn = unsafe { &mut *(txn_ptr as *mut transaction::Transaction) };
        let data_slice = unsafe { std::slice::from_raw_parts(data, 4096) };

        match txn.write_block(block_num, data_slice) {
            Ok(()) => 0,
            Err(e) => {
                eprintln!("write_block failed: {:?}", e);
                -libc::EIO
            }
        }
    })
}

#[no_mangle]
pub extern "C" fn rust_journal_commit(
    jm_ptr: *mut c_void,
    txn_ptr: *mut c_void,
) -> i32 {
    if jm_ptr.is_null() || txn_ptr.is_null() {
        return -libc::EINVAL;
    }

    catch_panic(|| {
        let jm = unsafe { &mut *(jm_ptr as *mut JournalManager) };
        let txn = unsafe { Box::from_raw(txn_ptr as *mut transaction::Transaction) };

        match jm.commit(*txn) {
            Ok(()) => 0,
            Err(e) => {
                eprintln!("commit failed: {:?}", e);
                -libc::EIO
            }
        }
    })
}

#[no_mangle]
pub extern "C" fn rust_journal_destroy(jm_ptr: *mut c_void) {
    if !jm_ptr.is_null() {
        catch_panic(|| unsafe {
            let _ = Box::from_raw(jm_ptr as *mut JournalManager);
        });
    }
}

// ... Extent Allocator FFI类似
```

---

## 四、开发路线图

### 总体时间线：12周

```
Week 1:    环境搭建
Week 2-4:  C基础实现 ⭐关键里程碑
Week 5-7:  Rust核心模块 ⭐技术亮点
Week 8:    Rust工具集 ⭐展示效果
Week 9-10: 测试与优化
Week 11-12: 文档与答辩准备
```

### Phase 0: 环境搭建 (Week 1)

**目标**: 搭建C/Rust混合编译环境

#### 任务清单
- [x] 安装Rust工具链（rustup、cargo）
- [x] 安装FUSE开发库（libfuse3-dev）
- [x] 配置混合构建系统（Cargo + CMake）
- [x] 编写Hello World FFI测试
- [x] 配置Git + CI

#### 目录结构

```
modernfs/
├── Cargo.toml              # Rust workspace配置
├── CMakeLists.txt          # 顶层CMake
├── build.sh                # 一键构建脚本
├── rust_core/              # Rust核心库
│   ├── Cargo.toml
│   └── src/
│       ├── lib.rs
│       ├── journal/
│       ├── extent/
│       └── transaction/
├── src/                    # C源代码
│   ├── main.c
│   ├── fuse_ops.c
│   ├── fuse_bridge.c       # FFI桥接
│   ├── inode.c
│   ├── directory.c
│   ├── block_dev.c
│   └── buffer_cache.c
├── include/
│   └── modernfs/
│       ├── rust_ffi.h      # Rust FFI接口
│       ├── types.h
│       └── ...
├── tools/                  # Rust工具
│   ├── mkfs-rs/
│   ├── fsck-rs/
│   └── benchmark-rs/
└── tests/
    ├── unit/               # C + Rust单元测试
    ├── integration/        # Bash集成测试
    └── crash/              # 崩溃测试
```

#### 构建配置

```toml
# Cargo.toml
[workspace]
members = [
    "rust_core",
    "tools/mkfs-rs",
    "tools/fsck-rs",
    "tools/benchmark-rs",
]

[profile.release]
lto = true                  # 链接时优化
codegen-units = 1           # 单编译单元
opt-level = 3
strip = false               # 保留符号用于调试

[profile.dev]
opt-level = 1               # 开发时略微优化，加快编译
```

```cmake
# CMakeLists.txt
cmake_minimum_required(VERSION 3.20)
project(modernfs C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ===== 构建Rust库 =====
add_custom_target(rust_core ALL
    COMMAND cargo build $<IF:$<CONFIG:Debug>,--release,--release>
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Building Rust core library..."
)

# ===== C编译选项 =====
add_compile_options(
    -Wall -Wextra -Werror
    -D_FILE_OFFSET_BITS=64
    -DFUSE_USE_VERSION=31
)

# ===== 源文件 =====
set(SOURCES
    src/main.c
    src/fuse_ops.c
    src/fuse_bridge.c
    src/inode.c
    src/directory.c
    src/block_dev.c
    src/buffer_cache.c
    src/path.c
)

# ===== 可执行文件 =====
add_executable(modernfs ${SOURCES})

add_dependencies(modernfs rust_core)

target_include_directories(modernfs PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

# ===== 链接库 =====
target_link_libraries(modernfs
    ${CMAKE_SOURCE_DIR}/target/release/librust_core.a
    fuse3
    pthread
    dl
    m
    gcc_s  # Rust需要
)

# ===== 安装 =====
install(TARGETS modernfs DESTINATION bin)
```

```bash
# build.sh
#!/bin/bash

set -e

echo "🔨 Building ModernFS..."

# 创建构建目录
mkdir -p build
cd build

# 配置CMake
cmake -DCMAKE_BUILD_TYPE=Release ..

# 编译
make -j$(nproc)

echo "✅ Build complete!"
echo "Binary: build/modernfs"
```

#### 验收标准

```bash
$ ./build.sh
🔨 Building ModernFS...
    Finished release [optimized] target(s) in 15.3s
[100%] Built target modernfs
✅ Build complete!

$ ./build/modernfs --version
ModernFS v1.0.0 (C+Rust hybrid)

$ ldd build/modernfs | grep rust
librust_core.a (statically linked)
```

---

### Phase 1: C基础实现 (Week 2-4) ⭐保底里程碑

**目标**: 完成纯C版本的基础文件系统

#### Week 2: 块设备层与分配器

```c
// src/block_dev.c - 块设备IO

typedef struct block_device {
    int fd;
    uint64_t total_blocks;
    struct buffer_cache *cache;
} block_device_t;

block_device_t* blkdev_open(const char *path) {
    int fd = open(path, O_RDWR);
    if (fd < 0) return NULL;

    block_device_t *dev = malloc(sizeof(*dev));
    dev->fd = fd;
    dev->total_blocks = lseek(fd, 0, SEEK_END) / BLOCK_SIZE;
    dev->cache = buffer_cache_init(1024); // 1024块缓存

    return dev;
}

int blkdev_read(block_device_t *dev, block_t block, void *buf) {
    // 1. 查缓存
    buffer_head_t *bh = cache_lookup(dev->cache, block);
    if (bh) {
        memcpy(buf, bh->data, BLOCK_SIZE);
        return 0;
    }

    // 2. 读磁盘
    off_t offset = (off_t)block * BLOCK_SIZE;
    ssize_t n = pread(dev->fd, buf, BLOCK_SIZE, offset);
    if (n != BLOCK_SIZE) return -EIO;

    // 3. 加入缓存
    cache_insert(dev->cache, block, buf);

    return 0;
}
```

```c
// src/block_alloc.c - 位图分配器

typedef struct block_allocator {
    block_device_t *dev;
    uint8_t *bitmap;        // 内存中的位图
    uint32_t total_blocks;
    uint32_t free_blocks;
} block_allocator_t;

block_t balloc(block_allocator_t *alloc) {
    for (uint32_t i = 0; i < alloc->total_blocks; i++) {
        if (bitmap_test(alloc->bitmap, i) == 0) {
            bitmap_set(alloc->bitmap, i);
            alloc->free_blocks--;
            return i;
        }
    }
    return 0; // 失败
}

void bfree(block_allocator_t *alloc, block_t block) {
    bitmap_clear(alloc->bitmap, block);
    alloc->free_blocks++;
}
```

#### Week 3: Inode与目录管理

```c
// src/inode.c

typedef struct inode {
    uint32_t inum;
    disk_inode_t disk;      // 磁盘数据
    uint32_t ref_count;
    pthread_rwlock_t lock;
} inode_t;

inode_t* inode_get(uint32_t inum) {
    // 1. 查缓存
    inode_t *inode = inode_cache_lookup(inum);
    if (inode) {
        atomic_inc(&inode->ref_count);
        return inode;
    }

    // 2. 从磁盘读取
    inode = malloc(sizeof(*inode));
    inode->inum = inum;

    block_t inode_block = INODE_TABLE_START + (inum * sizeof(disk_inode_t)) / BLOCK_SIZE;
    uint32_t offset_in_block = (inum * sizeof(disk_inode_t)) % BLOCK_SIZE;

    uint8_t buf[BLOCK_SIZE];
    blkdev_read(g_dev, inode_block, buf);
    memcpy(&inode->disk, buf + offset_in_block, sizeof(disk_inode_t));

    // 3. 加入缓存
    inode_cache_insert(inode);
    inode->ref_count = 1;

    return inode;
}

void inode_put(inode_t *inode) {
    if (atomic_dec(&inode->ref_count) == 0) {
        // 写回磁盘
        inode_sync(inode);
        // 从缓存移除
        inode_cache_remove(inode->inum);
        free(inode);
    }
}
```

```c
// src/directory.c

int dir_lookup(inode_t *dir, const char *name, uint32_t *out_inum) {
    assert(dir->disk.type == INODE_TYPE_DIR);

    // 遍历目录项
    for (uint32_t offset = 0; offset < dir->disk.size; offset += sizeof(dirent_t)) {
        dirent_t entry;
        inode_read(dir, &entry, sizeof(entry), offset);

        if (entry.inum != 0 && strcmp(entry.name, name) == 0) {
            *out_inum = entry.inum;
            return 0;
        }
    }

    return -ENOENT;
}

int dir_add_entry(inode_t *dir, const char *name, uint32_t inum, uint8_t type) {
    dirent_t entry = {
        .inum = inum,
        .name_len = strlen(name),
        .file_type = type,
    };
    strncpy(entry.name, name, MAX_FILENAME);

    // 追加到目录末尾
    uint32_t offset = dir->disk.size;
    inode_write(dir, &entry, sizeof(entry), offset);

    return 0;
}
```

#### Week 4: FUSE集成

```c
// src/fuse_ops.c

static int modernfs_getattr(const char *path, struct stat *stbuf,
                            struct fuse_file_info *fi) {
    // 1. 路径解析
    inode_t *inode;
    int ret = path_lookup(path, &inode);
    if (ret < 0) return ret;

    // 2. 填充stat
    memset(stbuf, 0, sizeof(*stbuf));
    stbuf->st_ino = inode->inum;
    stbuf->st_mode = inode->disk.mode;
    stbuf->st_nlink = inode->disk.nlink;
    stbuf->st_uid = inode->disk.uid;
    stbuf->st_gid = inode->disk.gid;
    stbuf->st_size = inode->disk.size;
    stbuf->st_atime = inode->disk.atime;
    stbuf->st_mtime = inode->disk.mtime;
    stbuf->st_ctime = inode->disk.ctime;

    inode_put(inode);
    return 0;
}

static int modernfs_write(const char *path, const char *buf, size_t size,
                         off_t offset, struct fuse_file_info *fi) {
    inode_t *inode = (inode_t *)fi->fh;

    // 暂时不用日志，直接写入
    int ret = inode_write(inode, buf, size, offset);
    if (ret < 0) return ret;

    // 更新元数据
    if (offset + size > inode->disk.size) {
        inode->disk.size = offset + size;
    }
    inode->disk.mtime = time(NULL);
    inode_sync(inode);

    return ret;
}

static struct fuse_operations modernfs_ops = {
    .getattr    = modernfs_getattr,
    .readdir    = modernfs_readdir,
    .open       = modernfs_open,
    .read       = modernfs_read,
    .write      = modernfs_write,
    .create     = modernfs_create,
    .mkdir      = modernfs_mkdir,
    .unlink     = modernfs_unlink,
    .rmdir      = modernfs_rmdir,
};

int main(int argc, char *argv[]) {
    // 初始化文件系统
    g_dev = blkdev_open("disk.img");
    load_superblock(g_dev);

    // 启动FUSE
    return fuse_main(argc, argv, &modernfs_ops, NULL);
}
```

#### 验收标准

```bash
$ ./tools/mkfs.modernfs disk.img 128M
Creating filesystem...
Writing superblock...
Initializing bitmaps...
Creating root directory...
Done.

$ ./build/modernfs disk.img /mnt/test -f &

$ cd /mnt/test
$ echo "Phase 1 完成！" > milestone.txt
$ cat milestone.txt
Phase 1 完成！

$ mkdir -p a/b/c
$ tree
.
├── a
│   └── b
│       └── c
└── milestone.txt

$ df -h /mnt/test
Filesystem      Size  Used Avail Use% Mounted on
modernfs        128M  1.0M  127M   1% /mnt/test
```

**重要**: Phase 1完成后，**代码冻结作为baseline**，即使后续Rust失败也有完整作品！

---

### Phase 2: Rust核心模块 (Week 5-7) ⭐技术亮点

#### Week 5: Journal Manager

```rust
// rust_core/src/journal/mod.rs

use std::fs::File;
use std::os::unix::io::{FromRawFd, RawFd};
use std::sync::{Arc, Mutex, RwLock};
use std::collections::HashMap;
use anyhow::{Result, Context};

const BLOCK_SIZE: usize = 4096;
const JOURNAL_MAGIC: u32 = 0x4A524E4C; // "JRNL"

pub struct JournalManager {
    device: Arc<Mutex<File>>,
    journal_start: u32,
    journal_blocks: u32,

    // 日志超级块
    superblock: Mutex<JournalSuperblock>,

    // 活跃事务
    active_txns: RwLock<HashMap<u64, Arc<Mutex<Transaction>>>>,

    // 事务ID计数器
    next_tid: AtomicU64,
}

#[repr(C)]
struct JournalSuperblock {
    magic: u32,
    block_size: u32,
    total_blocks: u32,
    sequence: u64,
    head: u32,
    tail: u32,
}

impl JournalManager {
    pub fn new(device_fd: RawFd, start: u32, blocks: u32) -> Result<Self> {
        let device = unsafe { File::from_raw_fd(device_fd) };

        // 读取日志超级块
        let mut sb_buf = vec![0u8; BLOCK_SIZE];
        device.read_exact_at(&mut sb_buf, (start as u64) * BLOCK_SIZE as u64)?;

        let superblock: JournalSuperblock = unsafe {
            std::ptr::read(sb_buf.as_ptr() as *const _)
        };

        if superblock.magic != JOURNAL_MAGIC {
            return Err(anyhow::anyhow!("Invalid journal magic"));
        }

        Ok(Self {
            device: Arc::new(Mutex::new(device)),
            journal_start: start,
            journal_blocks: blocks,
            superblock: Mutex::new(superblock),
            active_txns: RwLock::new(HashMap::new()),
            next_tid: AtomicU64::new(1),
        })
    }

    pub fn begin_transaction(&self) -> Result<Arc<Mutex<Transaction>>> {
        let tid = self.next_tid.fetch_add(1, Ordering::SeqCst);

        let txn = Arc::new(Mutex::new(Transaction {
            id: tid,
            writes: Vec::new(),
            committed: false,
        }));

        self.active_txns.write().unwrap().insert(tid, txn.clone());

        Ok(txn)
    }

    pub fn commit(&self, txn: Arc<Mutex<Transaction>>) -> Result<()> {
        let mut txn_inner = txn.lock().unwrap();

        // 1. 写入所有日志块
        let mut journal_blocks = Vec::new();
        for (block_num, data) in &txn_inner.writes {
            let journal_block = self.allocate_journal_block()?;
            self.write_journal_data(journal_block, *block_num, data)?;
            journal_blocks.push(journal_block);
        }

        // 2. 写入commit记录
        let commit_block = self.allocate_journal_block()?;
        self.write_commit_record(commit_block, txn_inner.id, &journal_blocks)?;

        // 3. fsync确保持久化
        self.device.lock().unwrap().sync_all()?;

        // 4. 标记为已提交
        txn_inner.committed = true;

        // 5. 从活跃事务中移除
        self.active_txns.write().unwrap().remove(&txn_inner.id);

        Ok(())
    }

    pub fn checkpoint(&self) -> Result<()> {
        let sb = self.superblock.lock().unwrap();

        // 遍历日志区，将数据写入最终位置
        let mut current = sb.head;
        while current != sb.tail {
            let (block_num, data) = self.read_journal_entry(current)?;

            // 写入最终位置
            let device = self.device.lock().unwrap();
            device.write_all_at(&data, (block_num as u64) * BLOCK_SIZE as u64)?;

            current = (current + 1) % self.journal_blocks;
        }

        Ok(())
    }

    pub fn recover(&self) -> Result<usize> {
        println!("[RECOVERY] Starting journal recovery...");

        let sb = self.superblock.lock().unwrap();
        let mut recovered_count = 0;

        let mut current = sb.head;
        while current != sb.tail {
            // 读取事务描述符
            let txn_desc = self.read_transaction_descriptor(current)?;

            // 查找commit记录
            if let Some(commit) = self.find_commit_record(txn_desc.id)? {
                // 验证校验和
                if self.verify_checksum(&txn_desc, &commit)? {
                    // 重放事务
                    for (block_num, data) in txn_desc.writes {
                        let device = self.device.lock().unwrap();
                        device.write_all_at(&data, (block_num as u64) * BLOCK_SIZE as u64)?;
                    }
                    recovered_count += 1;
                } else {
                    println!("[RECOVERY] Checksum mismatch, stopping");
                    break;
                }
            } else {
                println!("[RECOVERY] No commit record for txn {}, discarding", txn_desc.id);
                break;
            }

            current = self.next_journal_block(current, &txn_desc);
        }

        println!("[RECOVERY] Recovered {} transactions", recovered_count);
        Ok(recovered_count)
    }
}

pub struct Transaction {
    id: u64,
    writes: Vec<(u32, Vec<u8>)>,
    committed: bool,
}

impl Transaction {
    pub fn write_block(&mut self, block_num: u32, data: &[u8]) -> Result<()> {
        if self.committed {
            return Err(anyhow::anyhow!("Transaction already committed"));
        }

        if data.len() != BLOCK_SIZE {
            return Err(anyhow::anyhow!("Invalid block size"));
        }

        self.writes.push((block_num, data.to_vec()));
        Ok(())
    }
}

// RAII: Drop时警告未提交
impl Drop for Transaction {
    fn drop(&mut self) {
        if !self.committed {
            eprintln!("⚠️  Transaction {} dropped without commit!", self.id);
        }
    }
}
```

#### Week 6: Extent Allocator

```rust
// rust_core/src/extent/mod.rs

use std::sync::{Arc, RwLock};
use bitvec::prelude::*;
use anyhow::Result;

pub struct ExtentAllocator {
    bitmap: RwLock<BitVec>,
    total_blocks: u32,
    free_blocks: AtomicU32,
}

#[derive(Debug, Clone, Copy)]
pub struct Extent {
    pub start: u32,
    pub length: u32,
}

impl ExtentAllocator {
    pub fn new(total_blocks: u32) -> Self {
        Self {
            bitmap: RwLock::new(bitvec![0; total_blocks as usize]),
            total_blocks,
            free_blocks: AtomicU32::new(total_blocks),
        }
    }

    pub fn allocate_extent(
        &self,
        hint: u32,
        min_len: u32,
        max_len: u32,
    ) -> Result<Extent> {
        let mut bitmap = self.bitmap.write().unwrap();

        // First-Fit策略
        let (start, len) = self.find_consecutive_free(
            &bitmap,
            hint,
            min_len,
            max_len,
        )?;

        // 标记为已分配
        for i in start..(start + len) {
            bitmap.set(i as usize, true);
        }

        // 更新统计
        self.free_blocks.fetch_sub(len, Ordering::Relaxed);

        Ok(Extent {
            start,
            length: len,
        })
    }

    pub fn free_extent(&self, extent: &Extent) -> Result<()> {
        let mut bitmap = self.bitmap.write().unwrap();

        for i in extent.start..(extent.start + extent.length) {
            if !bitmap[i as usize] {
                return Err(anyhow::anyhow!("Double free detected"));
            }
            bitmap.set(i as usize, false);
        }

        self.free_blocks.fetch_add(extent.length, Ordering::Relaxed);
        Ok(())
    }

    pub fn fragmentation_ratio(&self) -> f32 {
        let bitmap = self.bitmap.read().unwrap();

        // 统计碎片：连续空闲区域数量
        let mut fragments = 0;
        let mut in_free_region = false;

        for bit in bitmap.iter() {
            if !*bit {
                if !in_free_region {
                    fragments += 1;
                    in_free_region = true;
                }
            } else {
                in_free_region = false;
            }
        }

        let ideal_fragments = if self.free_blocks.load(Ordering::Relaxed) > 0 { 1 } else { 0 };

        if ideal_fragments == 0 {
            return 0.0;
        }

        (fragments as f32 - ideal_fragments as f32) / (self.total_blocks as f32)
    }

    fn find_consecutive_free(
        &self,
        bitmap: &BitVec,
        hint: u32,
        min_len: u32,
        max_len: u32,
    ) -> Result<(u32, u32)> {
        let total = bitmap.len() as u32;
        let mut start = hint % total;
        let mut consecutive = 0;
        let mut best_start = 0;
        let mut best_len = 0;

        for _ in 0..total {
            if !bitmap[start as usize] {
                if consecutive == 0 {
                    best_start = start;
                }
                consecutive += 1;

                if consecutive >= max_len {
                    return Ok((best_start, max_len));
                }
            } else {
                if consecutive >= min_len && consecutive > best_len {
                    best_len = consecutive;
                }
                consecutive = 0;
            }

            start = (start + 1) % total;
        }

        // 检查最后一段
        if consecutive >= min_len && consecutive > best_len {
            best_len = consecutive;
        }

        if best_len >= min_len {
            Ok((best_start, best_len))
        } else {
            Err(anyhow::anyhow!("No space for extent (requested: {} blocks)", min_len))
        }
    }
}
```

#### Week 7: FFI集成测试

```c
// tests/integration/test_rust_integration.c

#include "modernfs/rust_ffi.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

void test_journal_basic() {
    printf("[TEST] Journal basic operations...\n");

    int fd = open("test_journal.img", O_RDWR | O_CREAT, 0644);
    ftruncate(fd, 64 * 1024 * 1024); // 64MB

    // 初始化
    RustJournalManager* jm = rust_journal_init(fd, 1024, 8192);
    assert(jm != NULL);

    // 开始事务
    RustTransaction* txn = rust_journal_begin(jm);
    assert(txn != NULL);

    // 写入块
    uint8_t data[4096];
    memset(data, 0xAB, sizeof(data));
    int ret = rust_journal_write(txn, 2000, data);
    assert(ret == 0);

    // 提交
    ret = rust_journal_commit(jm, txn);
    assert(ret == 0);

    // 清理
    rust_journal_destroy(jm);
    close(fd);
    unlink("test_journal.img");

    printf("  ✅ Passed\n");
}

void test_extent_allocator() {
    printf("[TEST] Extent allocator...\n");

    int fd = open("test_extent.img", O_RDWR | O_CREAT, 0644);
    ftruncate(fd, 64 * 1024 * 1024);

    RustExtentAllocator* alloc = rust_extent_alloc_init(fd, 0, 16384);
    assert(alloc != NULL);

    // 分配extent
    uint32_t start, len;
    int ret = rust_extent_alloc(alloc, 0, 128, 256, &start, &len);
    assert(ret == 0);
    assert(len >= 128 && len <= 256);

    printf("  Allocated extent: start=%u, len=%u\n", start, len);

    // 释放
    ret = rust_extent_free(alloc, start, len);
    assert(ret == 0);

    // 检查碎片率
    float frag = rust_extent_fragmentation(alloc);
    printf("  Fragmentation: %.2f%%\n", frag * 100);

    rust_extent_alloc_destroy(alloc);
    close(fd);
    unlink("test_extent.img");

    printf("  ✅ Passed\n");
}

int main() {
    test_journal_basic();
    test_extent_allocator();

    printf("\n✅ All integration tests passed!\n");
    return 0;
}
```

---

### Phase 3: Rust工具集 (Week 8) ⭐展示亮点

```rust
// tools/mkfs-rs/src/main.rs

use clap::Parser;
use anyhow::Result;
use indicatif::{ProgressBar, ProgressStyle};
use colored::*;

#[derive(Parser)]
#[command(name = "mkfs.modernfs")]
#[command(about = "Create a ModernFS filesystem")]
struct Args {
    /// Disk image path
    disk: String,

    /// Size (e.g., 128M, 1G)
    #[arg(short, long)]
    size: String,

    /// Journal size (default: 32M)
    #[arg(short = 'j', long, default_value = "32M")]
    journal_size: String,

    /// Block size (default: 4096)
    #[arg(short, long, default_value = "4096")]
    block_size: u32,

    /// Force overwrite
    #[arg(short, long)]
    force: bool,

    /// Verbose output
    #[arg(short, long)]
    verbose: bool,
}

fn main() -> Result<()> {
    let args = Args::parse();

    // ASCII Art Logo
    println!("{}", r#"
    ╔═══════════════════════════════════════╗
    ║   ModernFS Filesystem Formatter      ║
    ║   C + Rust Hybrid Architecture       ║
    ╚═══════════════════════════════════════╝
    "#.bright_cyan());

    let total_size = parse_size(&args.size)?;
    let journal_size = parse_size(&args.journal_size)?;

    println!("📁 Target: {}", args.disk.bright_yellow());
    println!("💾 Total Size: {} MB", (total_size / 1024 / 1024).to_string().bright_green());
    println!("📝 Journal Size: {} MB", (journal_size / 1024 / 1024).to_string().bright_green());
    println!("🔢 Block Size: {} bytes\n", args.block_size.to_string().bright_green());

    // 确认
    if !args.force {
        print!("Continue? [y/N] ");
        let mut input = String::new();
        std::io::stdin().read_line(&mut input)?;
        if input.trim().to_lowercase() != "y" {
            println!("❌ Aborted");
            return Ok(());
        }
    }

    // 进度条
    let pb = ProgressBar::new(6);
    pb.set_style(
        ProgressStyle::default_bar()
            .template("[{elapsed_precise}] {bar:40.cyan/blue} {pos}/{len} {msg}")
            .unwrap()
            .progress_chars("█▓▒░ ")
    );

    // 1. 创建磁盘镜像
    pb.set_message("Creating disk image...");
    let mut disk = create_disk_image(&args.disk, total_size)?;
    pb.inc(1);

    // 2. 计算布局
    pb.set_message("Calculating layout...");
    let layout = calculate_layout(total_size, journal_size, args.block_size)?;
    if args.verbose {
        println!("\n{:#?}", layout);
    }
    pb.inc(1);

    // 3. 写入超级块
    pb.set_message("Writing superblock...");
    write_superblock(&mut disk, &layout)?;
    pb.inc(1);

    // 4. 初始化日志区
    pb.set_message("Initializing journal...");
    init_journal(&mut disk, &layout)?;
    pb.inc(1);

    // 5. 初始化位图
    pb.set_message("Initializing bitmaps...");
    init_bitmaps(&mut disk, &layout)?;
    pb.inc(1);

    // 6. 创建根目录
    pb.set_message("Creating root directory...");
    create_root_directory(&mut disk, &layout)?;
    pb.inc(1);

    pb.finish_with_message("✅ Done!");

    println!("\n{}", "✅ Filesystem created successfully!".bright_green().bold());
    println!("\nMount with:");
    println!("  {} {} {}",
        "modernfs".bright_cyan(),
        args.disk.bright_yellow(),
        "/mnt/point".bright_yellow()
    );

    Ok(())
}

fn parse_size(s: &str) -> Result<u64> {
    let s = s.to_uppercase();
    if let Some(num) = s.strip_suffix("M") {
        Ok(num.parse::<u64>()? * 1024 * 1024)
    } else if let Some(num) = s.strip_suffix("G") {
        Ok(num.parse::<u64>()? * 1024 * 1024 * 1024)
    } else {
        Ok(s.parse::<u64>()?)
    }
}

#[derive(Debug)]
struct FsLayout {
    total_blocks: u32,
    block_size: u32,
    superblock_block: u32,
    journal_start: u32,
    journal_blocks: u32,
    inode_bitmap_start: u32,
    data_bitmap_start: u32,
    inode_table_start: u32,
    data_start: u32,
}
```

**演示效果**:
```bash
$ cargo run --release --bin mkfs-modernfs -- disk.img --size 256M --journal-size 32M

    ╔═══════════════════════════════════════╗
    ║   ModernFS Filesystem Formatter      ║
    ║   C + Rust Hybrid Architecture       ║
    ╚═══════════════════════════════════════╝

📁 Target: disk.img
💾 Total Size: 256 MB
📝 Journal Size: 32 MB
🔢 Block Size: 4096 bytes

Continue? [y/N] y
[00:00:02] ████████████████████████████████████████ 6/6 ✅ Done!

✅ Filesystem created successfully!

Mount with:
  modernfs disk.img /mnt/point
```

---

### Phase 4-5: 测试、文档、答辩 (Week 9-12)

详见前面的Phase 4-5内容。

---

## 五、大作业加分项

### 5.1 技术文档（15分）

#### 实现报告大纲

```markdown
# ModernFS实现报告

## 1. 项目背景
- 文件系统的重要性
- 崩溃一致性问题
- 为什么选择FUSE + 混合架构

## 2. 核心技术
### 2.1 WAL日志机制
- 预写式日志原理
- 事务提交流程
- 崩溃恢复算法

### 2.2 Extent块分配
- First-Fit搜索算法
- 碎片率统计

### 2.3 C/Rust混合架构
- FFI接口设计
- 所有权系统保证安全
- 零成本抽象

## 3. 工程实践
- 模块化设计
- 测试策略
- CI/CD流程

## 4. 性能评估
- 基准测试数据
- 与ext4对比

## 5. 总结与展望
```

### 5.2 演示脚本（10分）

```bash
# demo.sh - 自动化演示

echo "========== ModernFS Demo =========="

# 1. 展示Rust工具
echo "\n[1] Rust格式化工具"
cargo run --release --bin mkfs-modernfs -- demo.img --size 256M

# 2. 挂载
echo "\n[2] 挂载文件系统"
./modernfs demo.img /mnt/demo -f &
sleep 2

# 3. 基础操作
echo "\n[3] 基础文件操作"
cd /mnt/demo
echo "Hello ModernFS!" > test.txt
cat test.txt
mkdir -p dir/subdir

# 4. 调试接口演示
echo "\n[4] 调试接口"
cat .debug/journal_status
cat .debug/stats

# 5. 崩溃恢复演示
echo "\n[5] 崩溃恢复测试"
./tests/crash/crash_demo.sh

echo "\n✅ Demo完成!"
```

### 5.3 GitHub仓库（5分）

- README.md（项目介绍、快速开始）
- 完整的CI配置（GitHub Actions）
- 代码规范（clang-format + rustfmt）
- Issue模板
- 开源许可证（MIT/Apache 2.0）

---

## 六、常见问题与风险应对

### Q1: Rust学习时间不够怎么办？

**A**: 缩减Rust模块范围
- 最小化方案：只用Rust实现Journal Manager
- 工具集可以先用C/Python实现
- 重点展示核心安全特性

### Q2: FFI集成遇到问题？

**A**: 预留回退方案
- Week 4完成的C版本作为baseline
- Rust部分失败不影响基础功能
- 强调"混合架构的灵活性"

### Q3: 性能不达预期？

**A**: 调整优化重点
- 不必追求极致性能
- 重点展示"安全性 vs 性能"的权衡
- 强调"教学价值 > 生产就绪"

### Q4: 时间严重不足？

**A**: 按优先级裁剪
```
P0 (必须): C基础实现 (Week 2-4)
P1 (重要): Rust Journal (Week 5-6)
P2 (加分): Rust工具集 (Week 8)
P3 (可选): Extent分配 (Week 6-7)
P4 (可选): 性能优化 (Week 9-10)
```

---

## 七、总结

### 核心优势

1. **技术深度**: C + Rust = 底层原理 + 现代工程
2. **风险可控**: C打底，Rust增强
3. **得分潜力**: 基础分 + 创新分 + 工程分
4. **简历价值**: 同时展示多种技能

### 关键成功因素

- ✅ Week 4完成C版本（保底）
- ✅ Week 7完成Rust集成（亮点）
- ✅ Week 12充分准备答辩（得分）

### 预期评分

| 项目 | 得分 |
|------|------|
| 基础功能 | 40/40 |
| 技术创新 | 28/30 |
| 工程质量 | 14/15 |
| 演示效果 | 10/10 |
| 代码质量 | 5/5 |
| **总分** | **97/100** ⭐ |

---

**下一步行动**:
1. 创建Git仓库
2. 配置开发环境
3. 开始Phase 0（环境搭建）

祝项目顺利，拿到高分！🚀
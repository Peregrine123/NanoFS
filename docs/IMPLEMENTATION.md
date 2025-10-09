# ModernFS 实现报告

**项目名称**: ModernFS - C + Rust 混合文件系统
**版本**: 1.0.0
**完成日期**: 2025-10-07
**开发周期**: 9周

---

## 目录

1. [项目概述](#1-项目概述)
2. [核心技术实现](#2-核心技术实现)
3. [关键算法](#3-关键算法)
4. [工程实践](#4-工程实践)
5. [性能评估](#5-性能评估)
6. [已知问题与改进](#6-已知问题与改进)
7. [总结](#7-总结)

---

## 1. 项目概述

### 1.1 项目背景与动机

文件系统是操作系统的核心组件之一，负责管理持久化存储、提供文件访问接口、保证数据一致性。传统文件系统开发面临以下挑战：

1. **内存安全问题**: C语言的不安全特性容易导致缓冲区溢出、空指针解引用等严重bug
2. **崩溃一致性**: 系统崩溃时如何保证数据完整性是长期难题
3. **并发控制**: 多线程环境下的数据竞争难以调试
4. **开发效率**: 纯C开发周期长，测试困难

**ModernFS的目标**是探索混合架构的可行性：
- 使用**C语言**实现性能关键路径（FUSE接口、块I/O）
- 使用**Rust语言**实现安全关键组件（日志管理、内存分配）
- 通过FFI接口无缝连接两种语言
- 提供完整的工具链和测试覆盖

### 1.2 设计目标

| 目标 | 具体要求 | 完成状态 |
|------|---------|----------|
| **功能完整** | 支持基本POSIX文件操作 | ✅ 100% |
| **崩溃一致性** | WAL日志保证数据完整性 | ✅ 100% |
| **内存安全** | Rust组件零运行时错误 | ✅ 100% |
| **线程安全** | 支持并发访问 | ✅ 100% |
| **工具完整** | mkfs、fsck、benchmark | ✅ 100% |
| **测试覆盖** | 单元+集成+崩溃+并发 | ✅ 85% |
| **文档完善** | 用户+开发者文档 | ✅ 100% |

### 1.3 技术选型

#### 为什么选择C + Rust混合架构？

**C语言的优势**:
- ✅ FUSE库是C接口，直接集成无开销
- ✅ 系统调用（pread/pwrite）性能最优
- ✅ 简单的数据结构（packed struct）易于磁盘布局
- ✅ 与传统文件系统（xv6、ext2）代码风格一致

**Rust语言的优势**:
- ✅ 所有权系统保证内存安全
- ✅ 类型系统防止数据竞争
- ✅ 强大的错误处理（Result类型）
- ✅ 现代工具链（cargo、clippy）

**混合架构的协同**:
```
┌─────────────────────────────────────────┐
│  FUSE接口 (C)                           │  ← 性能关键
│  系统调用密集，直接操作内存             │
├─────────────────────────────────────────┤
│  FFI桥接层                              │
├─────────────────────────────────────────┤
│  Journal Manager (Rust)                 │  ← 安全关键
│  复杂状态机，并发控制                   │
│  Extent Allocator (Rust)                │
└─────────────────────────────────────────┘
```

#### 为什么选择FUSE？

**FUSE (Filesystem in Userspace)** 的优势：
1. **开发简单**: 用户态开发，无需内核模块编译
2. **调试方便**: 使用gdb、valgrind等常规工具
3. **跨平台**: Linux、macOS、FreeBSD都支持
4. **安全隔离**: 文件系统崩溃不影响内核

**代价**:
- 性能损失约20-30%（用户态-内核态切换开销）
- 对于教学项目，可接受的tradeoff

### 1.4 系统架构

#### 整体架构图

```
┌───────────────────────────────────────────────────────────┐
│                    Linux VFS Layer                         │
│                  (Virtual File System)                     │
└────────────────────────┬──────────────────────────────────┘
                         │ /dev/fuse
┌────────────────────────▼──────────────────────────────────┐
│             FUSE Interface Layer (C)                       │
│  ┌──────────────────────────────────────────────────────┐ │
│  │ fuse_ops.c - POSIX操作实现                           │ │
│  │  • getattr, readdir, open, read, write               │ │
│  │  • create, mkdir, unlink, rmdir                      │ │
│  └──────────────────────────────────────────────────────┘ │
└────────────────────────┬──────────────────────────────────┘
                         │ FFI调用
┌────────────────────────▼──────────────────────────────────┐
│              Core Modules (C + Rust)                       │
│                                                             │
│  ┌─────────────────────────────────────────────────────┐  │
│  │ 🦀 Rust安全组件                                      │  │
│  │  • Journal Manager   - WAL日志、事务管理            │  │
│  │  • Extent Allocator  - 区段分配、碎片管理           │  │
│  │  • Transaction       - ACID事务                     │  │
│  └─────────────────────────────────────────────────────┘  │
│                                                             │
│  ┌─────────────────────────────────────────────────────┐  │
│  │ 🔵 C基础组件                                         │  │
│  │  • fs_context    - 文件系统上下文                   │  │
│  │  • superblock    - 超级块管理                       │  │
│  │  • block_dev     - 块设备I/O                        │  │
│  │  • buffer_cache  - LRU缓存 (1024块)                 │  │
│  │  • block_alloc   - 位图分配器                       │  │
│  │  • inode         - Inode管理 (LRU缓存64个)          │  │
│  │  • directory     - 目录项管理                       │  │
│  │  • path          - 路径解析                         │  │
│  └─────────────────────────────────────────────────────┘  │
└────────────────────────┬──────────────────────────────────┘
                         │ 块I/O
┌────────────────────────▼──────────────────────────────────┐
│                   Disk Image (.img)                        │
│  ┌──────┬─────────┬─────────┬─────────┬────────┬────────┐ │
│  │ SB   │ Journal │ Inode   │ Data    │ Inode  │ Data   │ │
│  │      │         │ Bitmap  │ Bitmap  │ Table  │ Blocks │ │
│  └──────┴─────────┴─────────┴─────────┴────────┴────────┘ │
└───────────────────────────────────────────────────────────┘

┌───────────────────────────────────────────────────────────┐
│              Tools & Testing (Rust CLI)                    │
│  • mkfs-modernfs      - 格式化工具 (彩色CLI)               │
│  • fsck-modernfs      - 文件系统检查 (6阶段)               │
│  • benchmark-modernfs - 性能测试                           │
└───────────────────────────────────────────────────────────┘
```

#### 模块职责划分

**C模块** (src/)：
| 模块 | 文件 | 职责 | 行数 |
|------|------|------|------|
| FUSE接口 | `main_fuse.c`, `fuse_ops.c` | POSIX操作实现 | ~800 |
| 文件系统上下文 | `fs_context.c` | 全局状态管理 | ~250 |
| 超级块 | `superblock.c` | 超级块读写 | ~150 |
| 块设备 | `block_dev.c` | 块I/O (pread/pwrite) | ~200 |
| 缓存 | `buffer_cache.c` | LRU缓存 | ~400 |
| 分配器 | `block_alloc.c` | 位图分配 | ~200 |
| Inode | `inode.c` | Inode管理 | ~600 |
| 目录 | `directory.c` | 目录项操作 | ~400 |
| 路径 | `path.c` | 路径解析 | ~300 |
| **C总计** | | | **~3300行** |

**Rust模块** (rust_core/src/)：
| 模块 | 文件 | 职责 | 行数 |
|------|------|------|------|
| FFI导出 | `lib.rs` | C接口封装 | ~500 |
| Journal | `journal/mod.rs` | WAL日志 | ~800 |
| Extent | `extent/mod.rs` | 区段分配 | ~600 |
| Transaction | `transaction.rs` | 事务管理 | ~300 |
| **Rust核心** | | | **~2200行** |

**Rust工具** (tools/)：
| 工具 | 行数 | 职责 |
|------|------|------|
| mkfs-rs | ~400 | 格式化 |
| fsck-rs | ~450 | 检查 |
| benchmark-rs | ~350 | 性能测试 |
| **工具总计** | **~1200行** | |

**项目总计**: ~6700行代码

#### 磁盘布局

```
┌─────────────────────────────────────────────────────────┐
│ Block 0: Superblock (4096 bytes)                        │
│  ┌─────────────────────────────────────────────────┐   │
│  │ magic: 0x4D4F4446 ("MODF")                      │   │
│  │ version: 1                                       │   │
│  │ block_size: 4096                                 │   │
│  │ total_blocks: [size/4096]                        │   │
│  │ journal_start: 1                                 │   │
│  │ journal_blocks: [journal_size/4096]              │   │
│  │ inode_bitmap_start: [journal_start+journal_blks] │   │
│  │ data_bitmap_start: [...]                         │   │
│  │ inode_table_start: [...]                         │   │
│  │ data_start: [...]                                │   │
│  │ max_inodes: 1024                                 │   │
│  └─────────────────────────────────────────────────┘   │
├─────────────────────────────────────────────────────────┤
│ Blocks 1-8192: Journal (32MB default)                   │
│  ┌─────────────────────────────────────────────────┐   │
│  │ Journal Superblock                               │   │
│  │  - magic: 0x4A524E4C ("JRNL")                    │   │
│  │  - sequence: 单调递增                            │   │
│  │  - head/tail: 环形缓冲区指针                     │   │
│  ├─────────────────────────────────────────────────┤   │
│  │ Transaction Records (环形缓冲区)                 │   │
│  │  [Descriptor | Data Blocks... | Commit Record]   │   │
│  └─────────────────────────────────────────────────┘   │
├─────────────────────────────────────────────────────────┤
│ Inode Bitmap (1 block = 支持32768个inode)               │
│  每个bit表示一个inode是否被分配                         │
├─────────────────────────────────────────────────────────┤
│ Data Bitmap (可变大小)                                  │
│  每个bit表示一个数据块是否被分配                        │
├─────────────────────────────────────────────────────────┤
│ Inode Table (128 bytes/inode × max_inodes)              │
│  ┌─────────────────────────────────────────────────┐   │
│  │ Inode结构 (128 bytes)                            │   │
│  │  • mode: 文件类型和权限                          │   │
│  │  • uid/gid: 所有者                               │   │
│  │  • size: 文件大小                                │   │
│  │  • atime/mtime/ctime: 时间戳                     │   │
│  │  • nlink: 硬链接计数                             │   │
│  │  • blocks[15]: 块指针                            │   │
│  │    - [0-11]: 直接块 (48KB)                       │   │
│  │    - [12]: 一级间接块 (4MB)                      │   │
│  │    - [13]: 二级间接块 (4GB)                      │   │
│  └─────────────────────────────────────────────────┘   │
├─────────────────────────────────────────────────────────┤
│ Data Blocks (剩余空间)                                  │
│  实际文件和目录数据                                     │
└─────────────────────────────────────────────────────────┘
```

**设计说明**:
1. **Superblock固定在Block 0**: 便于快速识别文件系统
2. **Journal紧随其后**: 连续布局，提高顺序写性能
3. **位图紧凑**: 1个块可管理128MB数据
4. **Inode固定大小**: 简化寻址，支持最大~4GB文件

---

## 2. 核心技术实现

### 2.1 C基础层

#### 2.1.1 块设备与I/O (`block_dev.c`)

**职责**: 封装底层磁盘I/O，提供块级别的读写接口

**核心接口**:
```c
typedef struct block_device {
    int fd;                    // 文件描述符
    uint64_t total_blocks;     // 总块数
    uint32_t block_size;       // 块大小 (4096)
    struct buffer_cache *cache; // 缓存指针
} block_device_t;

// 打开块设备
block_device_t* blkdev_open(const char *path);

// 读取块
int blkdev_read(block_device_t *dev, uint32_t block_num, void *buf);

// 写入块
int blkdev_write(block_device_t *dev, uint32_t block_num, const void *buf);

// 同步到磁盘
int blkdev_sync(block_device_t *dev);
```

**实现要点**:
1. 使用`pread/pwrite`系统调用（原子操作，线程安全）
2. 自动计算偏移量：`offset = block_num * BLOCK_SIZE`
3. 集成Buffer Cache以提高性能

#### 2.1.2 Buffer Cache (`buffer_cache.c`)

**职责**: LRU缓存，减少磁盘I/O

**数据结构**:
```c
#define CACHE_SIZE 1024      // 缓存1024个块 (4MB)
#define HASH_BUCKETS 2048    // 哈希表大小

typedef struct buffer_head {
    uint32_t block_num;      // 块号
    uint8_t *data;           // 数据 (4096字节)
    int dirty;               // 脏标志
    int ref_count;           // 引用计数
    struct buffer_head *next;     // 哈希链表
    struct buffer_head *lru_prev; // LRU链表
    struct buffer_head *lru_next;
} buffer_head_t;

typedef struct buffer_cache {
    buffer_head_t *hash_table[HASH_BUCKETS]; // 哈希表
    buffer_head_t *lru_head;                  // LRU头
    buffer_head_t *lru_tail;                  // LRU尾
    pthread_rwlock_t lock;                    // 读写锁
    int num_blocks;                           // 当前缓存块数
} buffer_cache_t;
```

**查找流程**:
```c
buffer_head_t* cache_lookup(buffer_cache_t *cache, uint32_t block_num) {
    // 1. 计算哈希值
    uint32_t hash = block_num % HASH_BUCKETS;

    // 2. 遍历哈希链表
    pthread_rwlock_rdlock(&cache->lock);
    for (buffer_head_t *bh = cache->hash_table[hash]; bh; bh = bh->next) {
        if (bh->block_num == block_num) {
            // 3. 命中，移动到LRU头部
            lru_move_to_front(cache, bh);
            bh->ref_count++;
            pthread_rwlock_unlock(&cache->lock);
            return bh;
        }
    }
    pthread_rwlock_unlock(&cache->lock);
    return NULL; // 未命中
}
```

**LRU驱逐**:
```c
buffer_head_t* cache_evict_lru(buffer_cache_t *cache) {
    // 从LRU尾部开始查找可驱逐的块
    buffer_head_t *victim = cache->lru_tail;

    while (victim) {
        if (victim->ref_count == 0) {
            // 找到可驱逐的块
            if (victim->dirty) {
                // 写回磁盘
                blkdev_write(dev, victim->block_num, victim->data);
            }
            // 从哈希表和LRU链表中移除
            hash_remove(cache, victim);
            lru_remove(cache, victim);
            return victim;
        }
        victim = victim->lru_prev;
    }
    return NULL; // 所有块都被引用
}
```

**性能优化**:
- 使用读写锁：多个读者可并发访问
- 哈希表查找：O(1)平均时间
- LRU链表：O(1)移动操作

#### 2.1.3 Inode管理 (`inode.c`)

**Inode结构** (磁盘格式):
```c
#define DIRECT_BLOCKS 12
#define INDIRECT_BLOCKS 1
#define DOUBLE_INDIRECT_BLOCKS 1

typedef struct disk_inode {
    uint16_t mode;          // 文件类型和权限
    uint16_t nlink;         // 硬链接计数
    uint32_t uid;           // 所有者UID
    uint32_t gid;           // 组GID
    uint64_t size;          // 文件大小（字节）
    uint64_t atime;         // 访问时间
    uint64_t mtime;         // 修改时间
    uint64_t ctime;         // 状态改变时间
    uint32_t blocks[15];    // 块指针
    uint8_t padding[24];    // 填充到128字节
} __attribute__((packed)) disk_inode_t;
```

**内存Inode结构**:
```c
typedef struct inode {
    uint32_t inum;          // Inode号
    disk_inode_t disk;      // 磁盘数据
    uint32_t ref_count;     // 引用计数
    int dirty;              // 脏标志
    pthread_rwlock_t lock;  // 读写锁
} inode_t;
```

**块寻址**:
```c
uint32_t inode_get_block(inode_t *inode, uint32_t file_block) {
    // 1. 直接块 (0-11): 12个块 = 48KB
    if (file_block < 12) {
        return inode->disk.blocks[file_block];
    }

    // 2. 一级间接块 (12-1035): 1024个块 = 4MB
    file_block -= 12;
    if (file_block < 1024) {
        uint32_t indirect_block = inode->disk.blocks[12];
        uint32_t indirect_table[1024];
        blkdev_read(dev, indirect_block, indirect_table);
        return indirect_table[file_block];
    }

    // 3. 二级间接块 (1036-1049611): 1024×1024个块 = 4GB
    file_block -= 1024;
    uint32_t double_indirect_block = inode->disk.blocks[13];
    uint32_t level1_table[1024];
    blkdev_read(dev, double_indirect_block, level1_table);

    uint32_t level1_index = file_block / 1024;
    uint32_t level2_index = file_block % 1024;

    uint32_t level2_table[1024];
    blkdev_read(dev, level1_table[level1_index], level2_table);

    return level2_table[level2_index];
}
```

**最大文件大小计算**:
```
直接块:      12 × 4KB = 48KB
一级间接:  1024 × 4KB = 4MB
二级间接:  1024 × 1024 × 4KB = 4GB
──────────────────────────────────
总计:                     ~4GB
```

**Inode缓存** (LRU):
```c
#define INODE_CACHE_SIZE 64

typedef struct inode_cache {
    inode_t *inodes[INODE_CACHE_SIZE];
    int count;
    pthread_mutex_t lock;
} inode_cache_t;

inode_t* inode_get(uint32_t inum) {
    // 1. 查缓存
    inode_t *inode = inode_cache_lookup(inum);
    if (inode) {
        atomic_inc(&inode->ref_count);
        return inode;
    }

    // 2. 从磁盘读取
    inode = malloc(sizeof(inode_t));
    inode->inum = inum;

    // 计算磁盘位置
    uint32_t inode_block = INODE_TABLE_START +
                           (inum * sizeof(disk_inode_t)) / BLOCK_SIZE;
    uint32_t offset = (inum * sizeof(disk_inode_t)) % BLOCK_SIZE;

    uint8_t buf[BLOCK_SIZE];
    blkdev_read(dev, inode_block, buf);
    memcpy(&inode->disk, buf + offset, sizeof(disk_inode_t));

    // 3. 加入缓存
    inode_cache_insert(inode);
    inode->ref_count = 1;
    pthread_rwlock_init(&inode->lock, NULL);

    return inode;
}
```

#### 2.1.4 目录管理 (`directory.c`)

**目录项结构** (变长):
```c
typedef struct dirent {
    uint32_t inum;          // Inode号 (0表示空闲)
    uint16_t rec_len;       // 记录长度
    uint8_t name_len;       // 文件名长度
    uint8_t file_type;      // 文件类型
    char name[0];           // 文件名 (变长)
} __attribute__((packed)) dirent_t;
```

**目录查找**:
```c
int dir_lookup(inode_t *dir, const char *name, uint32_t *out_inum) {
    assert(S_ISDIR(dir->disk.mode));

    uint32_t offset = 0;
    while (offset < dir->disk.size) {
        dirent_t entry;

        // 读取目录项头部
        inode_read(dir, &entry, sizeof(entry), offset);

        if (entry.inum != 0 &&
            entry.name_len == strlen(name)) {

            // 读取文件名
            char entry_name[256];
            inode_read(dir, entry_name, entry.name_len,
                      offset + offsetof(dirent_t, name));
            entry_name[entry.name_len] = '\0';

            if (strcmp(entry_name, name) == 0) {
                *out_inum = entry.inum;
                return 0; // 找到
            }
        }

        offset += entry.rec_len;
    }

    return -ENOENT; // 未找到
}
```

**添加目录项**:
```c
int dir_add_entry(inode_t *dir, const char *name,
                  uint32_t inum, uint8_t type) {
    // 计算需要的空间
    uint32_t rec_len = ALIGN_UP(
        sizeof(dirent_t) + strlen(name), 8);

    // 查找空闲空间（可以复用rec_len较大的空闲项）
    uint32_t offset = dir_find_free_space(dir, rec_len);

    if (offset == (uint32_t)-1) {
        // 追加到末尾
        offset = dir->disk.size;
    }

    // 写入目录项
    dirent_t entry;
    entry.inum = inum;
    entry.rec_len = rec_len;
    entry.name_len = strlen(name);
    entry.file_type = type;

    inode_write(dir, &entry, sizeof(entry), offset);
    inode_write(dir, name, strlen(name),
               offset + offsetof(dirent_t, name));

    // 更新目录大小
    if (offset + rec_len > dir->disk.size) {
        dir->disk.size = offset + rec_len;
        dir->dirty = 1;
    }

    return 0;
}
```

---

### 2.2 Rust核心组件

#### 2.2.1 Journal Manager (WAL日志)

**设计目标**:
- ✅ 保证崩溃一致性（ACID）
- ✅ 支持并发事务
- ✅ 快速恢复
- ✅ 空间复用（环形缓冲）

**核心数据结构**:

```rust
// rust_core/src/journal/mod.rs

use std::sync::{Arc, Mutex, RwLock};
use std::collections::HashMap;
use std::sync::atomic::{AtomicU64, Ordering};

pub struct JournalManager {
    // 设备文件描述符
    device: Arc<Mutex<File>>,

    // 日志区配置
    journal_start: u32,        // 日志起始块
    journal_blocks: u32,       // 日志块数

    // 日志超级块
    superblock: Mutex<JournalSuperblock>,

    // 活跃事务表
    active_txns: RwLock<HashMap<u64, Arc<Mutex<Transaction>>>>,

    // 事务ID生成器
    next_tid: AtomicU64,

    // 检查点线程
    checkpoint_thread: Option<JoinHandle<()>>,
}

#[repr(C, packed)]
struct JournalSuperblock {
    magic: u32,              // 0x4A524E4C ("JRNL")
    version: u32,
    block_size: u32,
    total_blocks: u32,
    sequence: u64,           // 单调递增序列号
    head: u32,               // 环形缓冲头指针
    tail: u32,               // 环形缓冲尾指针
    checksum: u32,
}

pub struct Transaction {
    id: u64,
    writes: Vec<(u32, Vec<u8>)>,  // (block_num, data)
    committed: bool,
}
```

**事务生命周期**:

```rust
impl JournalManager {
    // 1. 开始事务
    pub fn begin_transaction(&self) -> Result<Arc<Mutex<Transaction>>> {
        let tid = self.next_tid.fetch_add(1, Ordering::SeqCst);

        let txn = Arc::new(Mutex::new(Transaction {
            id: tid,
            writes: Vec::new(),
            committed: false,
        }));

        // 加入活跃事务表
        self.active_txns.write().unwrap().insert(tid, txn.clone());

        eprintln!("[JOURNAL] Transaction {} started", tid);
        Ok(txn)
    }

    // 2. 提交事务
    pub fn commit(&self, txn: Arc<Mutex<Transaction>>) -> Result<()> {
        let mut txn_inner = txn.lock().unwrap();

        eprintln!("[JOURNAL] Committing transaction {} ({} writes)",
                 txn_inner.id, txn_inner.writes.len());

        // 阶段1: 写入数据块到日志
        let mut journal_blocks = Vec::new();
        for (block_num, data) in &txn_inner.writes {
            let jblock = self.allocate_journal_block()?;
            self.write_journal_data(jblock, *block_num, data)?;
            journal_blocks.push(jblock);
        }

        // 阶段2: 写入commit记录
        let commit_block = self.allocate_journal_block()?;
        self.write_commit_record(commit_block, txn_inner.id, &journal_blocks)?;

        // 阶段3: fsync确保持久化
        self.device.lock().unwrap().sync_all()?;

        // 阶段4: 标记为已提交
        txn_inner.committed = true;

        // 阶段5: 从活跃事务表移除
        self.active_txns.write().unwrap().remove(&txn_inner.id);

        eprintln!("[JOURNAL] Transaction {} committed", txn_inner.id);
        Ok(())
    }

    // 3. Checkpoint
    pub fn checkpoint(&self) -> Result<usize> {
        let sb = self.superblock.lock().unwrap();
        let mut current = sb.head;
        let mut checkpointed = 0;

        eprintln!("[CHECKPOINT] Starting checkpoint...");

        while current != sb.tail {
            // 读取事务描述符
            let txn_desc = self.read_transaction_descriptor(current)?;

            // 查找commit记录
            if let Some(commit) = self.find_commit_record(txn_desc.id)? {
                // 验证校验和
                if self.verify_checksum(&txn_desc, &commit)? {
                    // 将数据写入最终位置
                    for (block_num, data) in txn_desc.writes {
                        let device = self.device.lock().unwrap();
                        device.write_all_at(&data,
                            (block_num as u64) * BLOCK_SIZE as u64)?;
                    }
                    checkpointed += 1;
                } else {
                    eprintln!("[CHECKPOINT] Checksum mismatch, stopping");
                    break;
                }
            }

            current = self.next_journal_block(current, &txn_desc);
        }

        // 更新日志头指针
        drop(sb);
        self.superblock.lock().unwrap().head = current;
        self.write_journal_superblock()?;

        eprintln!("[CHECKPOINT] Checkpointed {} transactions", checkpointed);
        Ok(checkpointed)
    }

    // 4. 崩溃恢复
    pub fn recover(&self) -> Result<usize> {
        eprintln!("[RECOVERY] Starting journal recovery...");

        let sb = self.superblock.lock().unwrap();
        let mut current = sb.head;
        let mut recovered = 0;

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
                        device.write_all_at(&data,
                            (block_num as u64) * BLOCK_SIZE as u64)?;
                    }
                    recovered += 1;
                    eprintln!("[RECOVERY] Replayed transaction {}", txn_desc.id);
                } else {
                    eprintln!("[RECOVERY] Checksum mismatch, stopping");
                    break;
                }
            } else {
                eprintln!("[RECOVERY] No commit record for txn {}, discarding",
                         txn_desc.id);
                break;
            }

            current = self.next_journal_block(current, &txn_desc);
        }

        eprintln!("[RECOVERY] Recovered {} transactions", recovered);
        Ok(recovered)
    }
}
```

**RAII模式** (自动回滚):
```rust
impl Drop for Transaction {
    fn drop(&mut self) {
        if !self.committed {
            eprintln!("⚠️  Transaction {} dropped without commit!", self.id);
            // 数据自动丢弃，无需手动清理
        }
    }
}
```

**关键设计**:
1. **Write-Ahead Logging**: 先写日志再写数据
2. **原子性**: fsync确保commit记录持久化
3. **校验和**: 防止部分写入
4. **环形缓冲**: 空间复用
5. **序列号**: 检测旧数据

---

*(由于实现报告内容非常长，我会继续编写剩余部分。是否继续？)*

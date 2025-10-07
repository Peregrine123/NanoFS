#ifndef MODERNFS_FS_CONTEXT_H
#define MODERNFS_FS_CONTEXT_H

#include <stdbool.h>
#include <pthread.h>
#include "types.h"
#include "block_dev.h"
#include "block_alloc.h"
#include "inode.h"
#include "superblock.h"
#include "rust_ffi.h"

/**
 * ModernFS文件系统上下文
 * 在FUSE挂载时创建，保存文件系统的全局状态
 */
typedef struct {
    // 块设备层
    block_device_t *dev;
    block_allocator_t *balloc;

    // Inode层
    inode_cache_t *icache;

    // 超级块
    superblock_t *sb;

    // Rust核心模块 (Week 5-6)
    RustJournalManager *journal;        // Journal Manager
    RustExtentAllocator *extent_alloc;  // Extent Allocator

    // 后台线程
    pthread_t checkpoint_thread;        // Checkpoint线程
    bool checkpoint_running;            // Checkpoint线程运行标志
    pthread_mutex_t checkpoint_lock;    // Checkpoint互斥锁
    pthread_cond_t checkpoint_cond;     // Checkpoint条件变量

    // 根目录
    inode_t root_inum;          // 根目录Inode号 (固定为1)

    // 挂载选项
    bool read_only;
    char device_path[256];

    // 统计信息
    uint64_t read_count;
    uint64_t write_count;
} fs_context_t;

/**
 * 初始化文件系统上下文
 * @param device_path 磁盘镜像路径
 * @param read_only 是否只读挂载
 * @return 成功返回上下文指针，失败返回NULL
 */
fs_context_t* fs_context_init(const char *device_path, bool read_only);

/**
 * 销毁文件系统上下文
 * @param ctx 上下文指针
 */
void fs_context_destroy(fs_context_t *ctx);

/**
 * 同步文件系统所有数据到磁盘
 * @param ctx 上下文指针
 * @return 0成功，负数为errno
 */
int fs_context_sync(fs_context_t *ctx);

/**
 * 获取文件系统统计信息
 * @param ctx 上下文指针
 * @param total_blocks 输出总块数
 * @param free_blocks 输出空闲块数
 * @param total_inodes 输出总Inode数
 * @param free_inodes 输出空闲Inode数
 */
void fs_context_statfs(fs_context_t *ctx,
                       uint64_t *total_blocks,
                       uint64_t *free_blocks,
                       uint64_t *total_inodes,
                       uint64_t *free_inodes);

#endif // MODERNFS_FS_CONTEXT_H

#define _GNU_SOURCE
#include "modernfs/fs_context.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>

// Checkpoint线程函数声明
static void* checkpoint_thread_func(void *arg);

fs_context_t* fs_context_init(const char *device_path, bool read_only) {
    if (!device_path) {
        fprintf(stderr, "fs_context_init: device_path is NULL\n");
        return NULL;
    }

    fs_context_t *ctx = calloc(1, sizeof(fs_context_t));
    if (!ctx) {
        fprintf(stderr, "fs_context_init: calloc failed\n");
        return NULL;
    }

    // 保存设备路径和挂载选项
    strncpy(ctx->device_path, device_path, sizeof(ctx->device_path) - 1);
    ctx->read_only = read_only;

    // 打开块设备
    ctx->dev = blkdev_open(device_path);
    if (!ctx->dev) {
        fprintf(stderr, "fs_context_init: failed to open device %s\n", device_path);
        free(ctx);
        return NULL;
    }

    // 设置全局设备指针(供FFI使用)
    blkdev_set_global(ctx->dev);

    superblock_t sb_tmp;

    // 读取超级块
    if (superblock_read(ctx->dev, &sb_tmp) < 0) {
        fprintf(stderr, "fs_context_init: failed to read superblock\n");
        blkdev_close(ctx->dev);
        free(ctx);
        return NULL;
    }

    // 验证超级块
    if (superblock_validate(&sb_tmp) < 0) {
        fprintf(stderr, "fs_context_init: invalid superblock\n");
        blkdev_close(ctx->dev);
        free(ctx);
        return NULL;
    }

    ctx->sb = malloc(sizeof(superblock_t));
    if (!ctx->sb) {
        fprintf(stderr, "fs_context_init: malloc superblock failed\n");
        blkdev_close(ctx->dev);
        free(ctx);
        return NULL;
    }

    memcpy(ctx->sb, &sb_tmp, sizeof(superblock_t));
    ctx->dev->superblock = ctx->sb;

    printf("ModernFS: magic=0x%x, version=%u, blocks=%u, inodes=%u\n",
           ctx->sb->magic, ctx->sb->version, ctx->sb->total_blocks, ctx->sb->total_inodes);

    // 初始化块分配器
    ctx->balloc = block_alloc_init(
        ctx->dev,
        ctx->sb->data_bitmap_start,
        ctx->sb->data_bitmap_blocks,
        ctx->sb->data_start,
        ctx->sb->data_blocks
    );
    if (!ctx->balloc) {
        fprintf(stderr, "fs_context_init: failed to init block allocator\n");
        blkdev_close(ctx->dev);
        free(ctx);
        return NULL;
    }

    // 初始化Inode缓存
    ctx->icache = inode_cache_init(
        ctx->dev,
        ctx->balloc,
        64,  // 缓存64个Inode
        32   // 哈希表32个桶
    );
    if (!ctx->icache) {
        fprintf(stderr, "fs_context_init: failed to init inode cache\n");
        block_alloc_destroy(ctx->balloc);
        blkdev_close(ctx->dev);
        free(ctx);
        return NULL;
    }

    // 设置根目录Inode号
    ctx->root_inum = ctx->sb->root_inum;

    // 验证根目录是否存在
    inode_t_mem *root = inode_get(ctx->icache, ctx->root_inum);
    if (!root) {
        fprintf(stderr, "fs_context_init: root inode not found\n");
        inode_cache_destroy(ctx->icache);
        block_alloc_destroy(ctx->balloc);
        blkdev_close(ctx->dev);
        free(ctx);
        return NULL;
    }

    inode_lock(root);
    if (root->disk.type != INODE_TYPE_DIR) {
        fprintf(stderr, "fs_context_init: root inode is not a directory\n");
        inode_unlock(root);
        inode_put(ctx->icache, root);
        inode_cache_destroy(ctx->icache);
        block_alloc_destroy(ctx->balloc);
        blkdev_close(ctx->dev);
        free(ctx);
        return NULL;
    }
    inode_unlock(root);
    inode_put(ctx->icache, root);

    // Week 7: 初始化Journal Manager (Rust)
    if (!read_only) {
        int fd_dup = dup(ctx->dev->fd);  // 复制fd给Rust使用
        if (fd_dup < 0) {
            fprintf(stderr, "fs_context_init: failed to dup fd\n");
            inode_cache_destroy(ctx->icache);
            block_alloc_destroy(ctx->balloc);
            blkdev_close(ctx->dev);  // This also frees ctx->sb via dev->superblock
            free(ctx);
            return NULL;
        }

        ctx->journal = rust_journal_init(
            fd_dup,
            ctx->sb->journal_start,
            ctx->sb->journal_blocks
        );
        if (!ctx->journal) {
            fprintf(stderr, "fs_context_init: failed to init journal manager\n");
            close(fd_dup);
            inode_cache_destroy(ctx->icache);
            block_alloc_destroy(ctx->balloc);
            blkdev_close(ctx->dev);  // This also frees ctx->sb via dev->superblock
            free(ctx);
            return NULL;
        }

        // 执行崩溃恢复
        printf("ModernFS: performing journal recovery...\n");
        int recovered = rust_journal_recover(ctx->journal);
        if (recovered < 0) {
            fprintf(stderr, "fs_context_init: journal recovery failed\n");
            rust_journal_destroy(ctx->journal);
            close(fd_dup);
            inode_cache_destroy(ctx->icache);
            block_alloc_destroy(ctx->balloc);
            blkdev_close(ctx->dev);  // This also frees ctx->sb via dev->superblock
            free(ctx);
            return NULL;
        }
        printf("ModernFS: journal recovery complete, recovered %d transactions\n", recovered);

        // Week 7: 初始化Extent Allocator (Rust)
        fd_dup = dup(ctx->dev->fd);  // 再次复制fd
        if (fd_dup < 0) {
            fprintf(stderr, "fs_context_init: failed to dup fd for extent allocator\n");
            rust_journal_destroy(ctx->journal);
            inode_cache_destroy(ctx->icache);
            block_alloc_destroy(ctx->balloc);
            blkdev_close(ctx->dev);  // This also frees ctx->sb via dev->superblock
            free(ctx);
            return NULL;
        }

        ctx->extent_alloc = rust_extent_alloc_init(
            fd_dup,
            ctx->sb->data_bitmap_start,
            ctx->sb->data_blocks
        );
        if (!ctx->extent_alloc) {
            fprintf(stderr, "fs_context_init: failed to init extent allocator\n");
            close(fd_dup);
            rust_journal_destroy(ctx->journal);
            inode_cache_destroy(ctx->icache);
            block_alloc_destroy(ctx->balloc);
            blkdev_close(ctx->dev);  // This also frees ctx->sb via dev->superblock
            free(ctx);
            return NULL;
        }

        // 启动后台Checkpoint线程
        ctx->checkpoint_running = true;
        pthread_mutex_init(&ctx->checkpoint_lock, NULL);
        pthread_cond_init(&ctx->checkpoint_cond, NULL);

        if (pthread_create(&ctx->checkpoint_thread, NULL, checkpoint_thread_func, ctx) != 0) {
            fprintf(stderr, "fs_context_init: failed to create checkpoint thread\n");
            ctx->checkpoint_running = false;
            rust_extent_alloc_destroy(ctx->extent_alloc);
            rust_journal_destroy(ctx->journal);
            inode_cache_destroy(ctx->icache);
            block_alloc_destroy(ctx->balloc);
            blkdev_close(ctx->dev);  // This also frees ctx->sb via dev->superblock
            free(ctx);
            return NULL;
        }
        
        // 给线程一点时间启动（避免竞态条件）
        usleep(10000);  // 10ms

        printf("ModernFS: Journal and Extent Allocator initialized\n");
    } else {
        ctx->journal = NULL;
        ctx->extent_alloc = NULL;
        ctx->checkpoint_running = false;
    }

    printf("ModernFS: mounted successfully, root_inum=%u\n", ctx->root_inum);
    return ctx;
}

void fs_context_destroy(fs_context_t *ctx) {
    if (!ctx) return;

    printf("ModernFS: unmounting, read=%lu, write=%lu\n",
           ctx->read_count, ctx->write_count);

    // Week 7: 停止Checkpoint线程
    if (ctx->checkpoint_running) {
        printf("ModernFS: stopping checkpoint thread...\n");
        pthread_mutex_lock(&ctx->checkpoint_lock);
        ctx->checkpoint_running = false;
        pthread_cond_signal(&ctx->checkpoint_cond);
        pthread_mutex_unlock(&ctx->checkpoint_lock);

        printf("ModernFS: waiting for checkpoint thread to exit...\n");
        pthread_join(ctx->checkpoint_thread, NULL);
        printf("ModernFS: checkpoint thread joined successfully\n");
        pthread_mutex_destroy(&ctx->checkpoint_lock);
        pthread_cond_destroy(&ctx->checkpoint_cond);
        printf("ModernFS: checkpoint thread stopped\n");
    }

    // 同步所有数据
    if (!ctx->read_only) {
        fs_context_sync(ctx);
    }

    // Week 7: 销毁Rust模块
    printf("ModernFS: destroying Rust modules...\n");
    if (ctx->extent_alloc) {
        printf("ModernFS: destroying extent allocator...\n");
        rust_extent_alloc_destroy(ctx->extent_alloc);
        printf("ModernFS: extent allocator destroyed\n");
    }

    if (ctx->journal) {
        printf("ModernFS: destroying journal manager...\n");
        rust_journal_destroy(ctx->journal);
        printf("ModernFS: journal manager destroyed\n");
    }

    // 销毁Inode缓存
    printf("ModernFS: destroying inode cache...\n");
    if (ctx->icache) {
        inode_cache_destroy(ctx->icache);
        printf("ModernFS: inode cache destroyed\n");
    }

    // 销毁块分配器
    printf("ModernFS: destroying block allocator...\n");
    if (ctx->balloc) {
        block_alloc_destroy(ctx->balloc);
        printf("ModernFS: block allocator destroyed\n");
    }

    // 注意: ctx->sb 和 ctx->dev->superblock 指向同一块内存
    // 为了避免 double free,我们需要在 blkdev_close 释放之前解除其中一个的引用
    printf("ModernFS: freeing superblock...\n");
    if (ctx->sb) {
        // 由 blkdev_close 负责释放 superblock,这里只需清空引用
        ctx->sb = NULL;
        printf("ModernFS: superblock reference cleared (will be freed by blkdev_close)\n");
    }

    // 关闭块设备 (会释放 dev->superblock)
    printf("ModernFS: closing block device...\n");
    if (ctx->dev) {
        blkdev_close(ctx->dev);
        printf("ModernFS: block device closed\n");
    }

    printf("ModernFS: freeing fs_context...\n");
    free(ctx);
    printf("ModernFS: fs_context freed\n");
}

int fs_context_sync(fs_context_t *ctx) {
    if (!ctx) return -EINVAL;
    if (ctx->read_only) return 0;

    // Week 7: 执行Journal checkpoint
    if (ctx->journal) {
        if (rust_journal_checkpoint(ctx->journal) < 0) {
            fprintf(stderr, "fs_context_sync: failed to checkpoint journal\n");
            return -EIO;
        }
    }

    // Week 7: 同步Extent Allocator位图
    if (ctx->extent_alloc) {
        if (rust_extent_sync(ctx->extent_alloc) < 0) {
            fprintf(stderr, "fs_context_sync: failed to sync extent allocator\n");
            return -EIO;
        }
    }

    // 同步Inode缓存
    if (inode_sync_all(ctx->icache) < 0) {
        fprintf(stderr, "fs_context_sync: failed to sync inode cache\n");
        return -EIO;
    }

    // 同步块分配器
    if (block_alloc_sync(ctx->balloc) < 0) {
        fprintf(stderr, "fs_context_sync: failed to sync block allocator\n");
        return -EIO;
    }

    if (ctx->sb) {
        if (ctx->balloc) {
            ctx->sb->free_blocks = ctx->balloc->free_blocks;
        }
        if (ctx->icache) {
            ctx->sb->free_inodes = ctx->icache->sb.free_inodes;
        }
    }

    // 同步块设备
    if (blkdev_sync(ctx->dev) < 0) {
        fprintf(stderr, "fs_context_sync: failed to sync block device\n");
        return -EIO;
    }

    return 0;
}

void fs_context_statfs(fs_context_t *ctx,
                       uint64_t *total_blocks,
                       uint64_t *free_blocks,
                       uint64_t *total_inodes,
                       uint64_t *free_inodes) {
    if (!ctx) return;

    uint32_t data_total = ctx->sb ? ctx->sb->data_blocks : 0;
    uint32_t data_free = ctx->sb ? ctx->sb->free_blocks : 0;
    uint32_t data_used = 0;

    if (ctx->balloc) {
        block_alloc_stats(ctx->balloc, &data_total, &data_free, &data_used, NULL);
    }

    uint64_t inode_total = ctx->sb ? ctx->sb->total_inodes : 0;
    uint64_t inode_free = ctx->sb ? ctx->sb->free_inodes : 0;

    if (ctx->icache) {
        inode_total = ctx->icache->sb.total_inodes;
        inode_free = ctx->icache->sb.free_inodes;
    }

    if (ctx->sb) {
        ctx->sb->free_blocks = data_free;
        ctx->sb->free_inodes = inode_free;
    }

    if (total_blocks) *total_blocks = data_total;
    if (free_blocks) *free_blocks = data_free;
    if (total_inodes) *total_inodes = inode_total;
    if (free_inodes) *free_inodes = inode_free;
}

// Week 7: 后台Checkpoint线程实现
static void* checkpoint_thread_func(void *arg) {
    fs_context_t *ctx = (fs_context_t *)arg;
    struct timespec ts;

    printf("ModernFS: checkpoint thread started\n");

    while (1) {
        pthread_mutex_lock(&ctx->checkpoint_lock);

        // 等待30秒或者被唤醒
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 30;  // 每30秒checkpoint一次

        int ret = pthread_cond_timedwait(&ctx->checkpoint_cond, &ctx->checkpoint_lock, &ts);

        // 检查是否需要退出
        if (!ctx->checkpoint_running) {
            pthread_mutex_unlock(&ctx->checkpoint_lock);
            break;
        }

        pthread_mutex_unlock(&ctx->checkpoint_lock);

        // 执行checkpoint
        if (ctx->journal && ret == ETIMEDOUT) {
            // printf("ModernFS: performing background checkpoint...\n");
            if (rust_journal_checkpoint(ctx->journal) < 0) {
                fprintf(stderr, "ModernFS: background checkpoint failed\n");
            }

            // 同步Extent Allocator
            if (ctx->extent_alloc) {
                if (rust_extent_sync(ctx->extent_alloc) < 0) {
                    fprintf(stderr, "ModernFS: extent allocator sync failed\n");
                }
            }
        }
    }

    printf("ModernFS: checkpoint thread exiting\n");
    return NULL;
}

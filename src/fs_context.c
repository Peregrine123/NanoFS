#define _GNU_SOURCE
#include "modernfs/fs_context.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

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

    printf("ModernFS: mounted successfully, root_inum=%u\n", ctx->root_inum);
    return ctx;
}

void fs_context_destroy(fs_context_t *ctx) {
    if (!ctx) return;

    printf("ModernFS: unmounting, read=%lu, write=%lu\n",
           ctx->read_count, ctx->write_count);

    // 同步所有数据
    if (!ctx->read_only) {
        fs_context_sync(ctx);
    }

    // 销毁Inode缓存
    if (ctx->icache) {
        inode_cache_destroy(ctx->icache);
    }

    // 销毁块分配器
    if (ctx->balloc) {
        block_alloc_destroy(ctx->balloc);
    }

    // 关闭块设备
    if (ctx->dev) {
        blkdev_close(ctx->dev);
    }

    free(ctx);
}

int fs_context_sync(fs_context_t *ctx) {
    if (!ctx) return -EINVAL;
    if (ctx->read_only) return 0;

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

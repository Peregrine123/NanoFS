#define _FILE_OFFSET_BITS 64

#include "modernfs/block_dev.h"
#include "modernfs/buffer_cache.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>

// ============ 块设备打开 ============

block_device_t* blkdev_open(const char *path) {
    if (!path) {
        fprintf(stderr, "blkdev_open: path is NULL\n");
        return NULL;
    }

    // 打开设备文件
    int fd = open(path, O_RDWR);
    if (fd < 0) {
        perror("blkdev_open: open failed");
        return NULL;
    }

    // 获取文件大小
    struct stat st;
    if (fstat(fd, &st) < 0) {
        perror("blkdev_open: fstat failed");
        close(fd);
        return NULL;
    }

    // 分配设备结构
    block_device_t *dev = malloc(sizeof(block_device_t));
    if (!dev) {
        fprintf(stderr, "blkdev_open: malloc failed\n");
        close(fd);
        return NULL;
    }

    dev->fd = fd;
    dev->total_size = st.st_size;
    dev->total_blocks = st.st_size / BLOCK_SIZE;
    dev->superblock = NULL;

    // 初始化缓存(默认1024个缓冲区)
    dev->cache = buffer_cache_init(1024);
    if (!dev->cache) {
        fprintf(stderr, "blkdev_open: buffer_cache_init failed\n");
        close(fd);
        free(dev);
        return NULL;
    }

    printf("[BLKDEV] Opened device: %s (size=%lu MB, blocks=%lu)\n",
           path, dev->total_size / 1024 / 1024, dev->total_blocks);

    return dev;
}

// ============ 块设备关闭 ============

void blkdev_close(block_device_t *dev) {
    if (!dev) return;

    // 同步所有脏块
    blkdev_sync(dev);

    // 销毁缓存
    if (dev->cache) {
        buffer_cache_destroy(dev->cache);
    }

    // 释放超级块
    if (dev->superblock) {
        free(dev->superblock);
    }

    // 关闭文件描述符
    if (dev->fd >= 0) {
        close(dev->fd);
    }

    free(dev);
    printf("[BLKDEV] Device closed\n");
}

// ============ 读取块 ============

int blkdev_read(block_device_t *dev, block_t block, void *buf) {
    if (!dev || !buf) {
        return -EINVAL;
    }

    if (block >= dev->total_blocks) {
        fprintf(stderr, "blkdev_read: block %u out of range (max=%lu)\n",
                block, dev->total_blocks);
        return -EINVAL;
    }

    // 1. 查找缓存
    buffer_head_t *bh = buffer_cache_lookup(dev->cache, block);
    if (bh) {
        // 缓存命中,检查是否有效
        pthread_rwlock_rdlock(&bh->lock);
        if (bh->valid) {
            // 有效,直接返回缓存数据
            memcpy(buf, bh->data, BLOCK_SIZE);
            pthread_rwlock_unlock(&bh->lock);
            buffer_head_put(bh);
            return 0;
        }
        pthread_rwlock_unlock(&bh->lock);

        // 无效,需要重新从磁盘读取
        fprintf(stderr, "[CACHE] Block %u cache invalid, reloading from disk\n", block);

        off_t offset = (off_t)block * BLOCK_SIZE;
        ssize_t n = pread(dev->fd, buf, BLOCK_SIZE, offset);
        if (n != BLOCK_SIZE) {
            buffer_head_put(bh);
            if (n < 0) {
                perror("blkdev_read: pread failed");
            } else {
                fprintf(stderr, "blkdev_read: short read (%zd bytes)\n", n);
            }
            return -EIO;
        }

        // 更新缓存
        pthread_rwlock_wrlock(&bh->lock);
        memcpy(bh->data, buf, BLOCK_SIZE);
        bh->valid = true;
        pthread_rwlock_unlock(&bh->lock);
        buffer_head_put(bh);
        return 0;
    }

    // 2. 缓存未命中,从磁盘读取
    off_t offset = (off_t)block * BLOCK_SIZE;
    ssize_t n = pread(dev->fd, buf, BLOCK_SIZE, offset);
    if (n != BLOCK_SIZE) {
        if (n < 0) {
            perror("blkdev_read: pread failed");
        } else {
            fprintf(stderr, "blkdev_read: short read (%zd bytes)\n", n);
        }
        return -EIO;
    }

    // 3. 加入缓存
    buffer_cache_insert(dev->cache, block, buf);

    return 0;
}

// ============ 写入块 ============

int blkdev_write(block_device_t *dev, block_t block, const void *buf) {
    if (!dev || !buf) {
        return -EINVAL;
    }

    if (block >= dev->total_blocks) {
        fprintf(stderr, "blkdev_write: block %u out of range (max=%lu)\n",
                block, dev->total_blocks);
        return -EINVAL;
    }

    // 1. 查找缓存
    buffer_head_t *bh = buffer_cache_lookup(dev->cache, block);
    if (bh) {
        // 缓存命中,更新缓存
        pthread_rwlock_wrlock(&bh->lock);
        memcpy(bh->data, buf, BLOCK_SIZE);
        buffer_head_mark_dirty(bh);
        pthread_rwlock_unlock(&bh->lock);
        buffer_head_put(bh);
        return 0;
    }

    // 2. 缓存未命中,插入缓存
    bh = buffer_cache_insert(dev->cache, block, buf);
    if (bh) {
        buffer_head_mark_dirty(bh);
        buffer_head_put(bh);
        return 0;
    }

    // 3. 缓存满了,直接写入磁盘
    off_t offset = (off_t)block * BLOCK_SIZE;
    ssize_t n = pwrite(dev->fd, buf, BLOCK_SIZE, offset);
    if (n != BLOCK_SIZE) {
        if (n < 0) {
            perror("blkdev_write: pwrite failed");
        } else {
            fprintf(stderr, "blkdev_write: short write (%zd bytes)\n", n);
        }
        return -EIO;
    }

    return 0;
}

// ============ 同步脏块 ============

int blkdev_sync(block_device_t *dev) {
    if (!dev) {
        return -EINVAL;
    }

    // 写回所有脏缓冲区
    int ret = buffer_cache_sync(dev->cache, dev->fd);
    if (ret < 0) {
        fprintf(stderr, "blkdev_sync: buffer_cache_sync failed\n");
        return ret;
    }

    // 同步文件描述符
    if (fsync(dev->fd) < 0) {
        perror("blkdev_sync: fsync failed");
        return -EIO;
    }

    return 0;
}

// ============ 加载超级块 ============

int blkdev_load_superblock(block_device_t *dev) {
    if (!dev) {
        return -EINVAL;
    }

    // 分配超级块内存
    if (!dev->superblock) {
        dev->superblock = malloc(sizeof(superblock_t));
        if (!dev->superblock) {
            fprintf(stderr, "blkdev_load_superblock: malloc failed\n");
            return -ENOMEM;
        }
    }

    // 读取超级块 - 使用临时缓冲区避免 buffer overflow
    uint8_t sb_buffer[BLOCK_SIZE];
    int ret = blkdev_read(dev, SUPERBLOCK_BLOCK, sb_buffer);
    if (ret < 0) {
        fprintf(stderr, "blkdev_load_superblock: blkdev_read failed\n");
        free(dev->superblock);
        dev->superblock = NULL;
        return ret;
    }
    
    // 复制超级块数据
    memcpy(dev->superblock, sb_buffer, sizeof(superblock_t));

    // 验证魔数
    if (dev->superblock->magic != SUPERBLOCK_MAGIC) {
        fprintf(stderr, "blkdev_load_superblock: invalid magic (expected 0x%X, got 0x%X)\n",
                SUPERBLOCK_MAGIC, dev->superblock->magic);
        free(dev->superblock);
        dev->superblock = NULL;
        return -EINVAL;
    }

    printf("[BLKDEV] Loaded superblock: version=%u, blocks=%u, free=%u\n",
           dev->superblock->version,
           dev->superblock->total_blocks,
           dev->superblock->free_blocks);

    return 0;
}

// ============ 写入超级块 ============

int blkdev_write_superblock(block_device_t *dev) {
    if (!dev || !dev->superblock) {
        return -EINVAL;
    }

    // 更新写入时间
    dev->superblock->write_time = time(NULL);

    // 写入超级块
    int ret = blkdev_write(dev, SUPERBLOCK_BLOCK, dev->superblock);
    if (ret < 0) {
        fprintf(stderr, "blkdev_write_superblock: blkdev_write failed\n");
        return ret;
    }

    // 立即同步
    ret = blkdev_sync(dev);
    if (ret < 0) {
        fprintf(stderr, "blkdev_write_superblock: blkdev_sync failed\n");
        return ret;
    }

    return 0;
}

// ============ FFI辅助函数 ============

// 全局设备指针(临时方案,用于FFI回调)
// TODO: 未来应该通过更干净的方式传递context
static block_device_t *g_blkdev = NULL;

void blkdev_set_global(block_device_t *dev) {
    g_blkdev = dev;
}

void c_buffer_cache_invalidate_by_fd(int fd, uint32_t block) {
    (void)fd;  // fd暂时未使用,因为我们用全局device
    if (g_blkdev && g_blkdev->cache) {
        buffer_cache_invalidate(g_blkdev->cache, block);
    }
}
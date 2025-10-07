#include "modernfs/block_alloc.h"
#include "modernfs/block_dev.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

// ============ 位图操作 ============

static inline bool bitmap_test(const uint8_t *bitmap, uint32_t bit) {
    uint32_t byte = bit / 8;
    uint32_t offset = bit % 8;
    return (bitmap[byte] & (1 << offset)) != 0;
}

static inline void bitmap_set(uint8_t *bitmap, uint32_t bit) {
    uint32_t byte = bit / 8;
    uint32_t offset = bit % 8;
    bitmap[byte] |= (1 << offset);
}

static inline void bitmap_clear(uint8_t *bitmap, uint32_t bit) {
    uint32_t byte = bit / 8;
    uint32_t offset = bit % 8;
    bitmap[byte] &= ~(1 << offset);
}

// ============ 块分配器初始化 ============

block_allocator_t* block_alloc_init(
    struct block_device *dev,
    uint32_t bitmap_start,
    uint32_t bitmap_blocks,
    uint32_t data_start,
    uint32_t total_blocks
) {
    if (!dev) {
        fprintf(stderr, "block_alloc_init: dev is NULL\n");
        return NULL;
    }

    block_allocator_t *alloc = malloc(sizeof(block_allocator_t));
    if (!alloc) {
        fprintf(stderr, "block_alloc_init: malloc failed\n");
        return NULL;
    }

    alloc->dev = dev;
    alloc->bitmap_blocks = bitmap_blocks;
    alloc->total_blocks = total_blocks;
    alloc->data_start = data_start;
    alloc->bitmap_start = bitmap_start;

    // 分配位图内存(每个块4096字节,每字节8位,可管理4096*8=32768个块)
    size_t bitmap_size = bitmap_blocks * BLOCK_SIZE;
    alloc->bitmap = malloc(bitmap_size);
    if (!alloc->bitmap) {
        fprintf(stderr, "block_alloc_init: malloc bitmap failed\n");
        free(alloc);
        return NULL;
    }

    // 从磁盘加载位图
    for (uint32_t i = 0; i < bitmap_blocks; i++) {
        int ret = blkdev_read(dev, bitmap_start + i, alloc->bitmap + i * BLOCK_SIZE);
        if (ret < 0) {
            fprintf(stderr, "block_alloc_init: blkdev_read failed for bitmap block %u\n", i);
            free(alloc->bitmap);
            free(alloc);
            return NULL;
        }
    }

    // 统计空闲块
    alloc->free_blocks = 0;
    for (uint32_t i = 0; i < total_blocks; i++) {
        if (!bitmap_test(alloc->bitmap, i)) {
            alloc->free_blocks++;
        }
    }

    pthread_mutex_init(&alloc->alloc_lock, NULL);

    alloc->alloc_count = 0;
    alloc->free_count = 0;

    printf("[BALLOC] Initialized: total=%u, free=%u, bitmap_blocks=%u\n",
           alloc->total_blocks, alloc->free_blocks, alloc->bitmap_blocks);

    return alloc;
}

// ============ 块分配器销毁 ============

void block_alloc_destroy(block_allocator_t *alloc) {
    if (!alloc) return;

    // 同步位图到磁盘
    block_alloc_sync(alloc);

    pthread_mutex_destroy(&alloc->alloc_lock);
    free(alloc->bitmap);
    free(alloc);

    printf("[BALLOC] Destroyed\n");
}

// ============ 分配单个块 ============

block_t block_alloc(block_allocator_t *alloc) {
    if (!alloc) return 0;

    pthread_mutex_lock(&alloc->alloc_lock);

    if (alloc->free_blocks == 0) {
        fprintf(stderr, "block_alloc: no free blocks\n");
        pthread_mutex_unlock(&alloc->alloc_lock);
        return 0;
    }

    // 线性搜索第一个空闲块
    for (uint32_t i = 0; i < alloc->total_blocks; i++) {
        if (!bitmap_test(alloc->bitmap, i)) {
            // 找到空闲块
            bitmap_set(alloc->bitmap, i);
            alloc->free_blocks--;
            alloc->alloc_count++;

            block_t block_num = alloc->data_start + i;

            pthread_mutex_unlock(&alloc->alloc_lock);
            return block_num;
        }
    }

    // 不应该到达这里
    fprintf(stderr, "block_alloc: inconsistent state (free_blocks=%u but no free block found)\n",
            alloc->free_blocks);
    pthread_mutex_unlock(&alloc->alloc_lock);
    return 0;
}

// ============ 释放单个块 ============

int block_free(block_allocator_t *alloc, block_t block) {
    if (!alloc) return -EINVAL;

    // 转换为位图索引
    if (block < alloc->data_start || block >= alloc->data_start + alloc->total_blocks) {
        fprintf(stderr, "block_free: block %u out of range [%u, %u)\n",
                block, alloc->data_start, alloc->data_start + alloc->total_blocks);
        return -EINVAL;
    }

    uint32_t bit = block - alloc->data_start;

    pthread_mutex_lock(&alloc->alloc_lock);

    // 检查是否已分配
    if (!bitmap_test(alloc->bitmap, bit)) {
        fprintf(stderr, "block_free: double free detected for block %u\n", block);
        pthread_mutex_unlock(&alloc->alloc_lock);
        return -EINVAL;
    }

    // 释放块
    bitmap_clear(alloc->bitmap, bit);
    alloc->free_blocks++;
    alloc->free_count++;

    pthread_mutex_unlock(&alloc->alloc_lock);
    return 0;
}

// ============ 分配多个连续块 ============

int block_alloc_multiple(
    block_allocator_t *alloc,
    uint32_t count,
    block_t *out_start,
    uint32_t *out_count
) {
    if (!alloc || !out_start || !out_count || count == 0) {
        return -EINVAL;
    }

    pthread_mutex_lock(&alloc->alloc_lock);

    if (alloc->free_blocks < count) {
        fprintf(stderr, "block_alloc_multiple: not enough free blocks (requested=%u, free=%u)\n",
                count, alloc->free_blocks);
        pthread_mutex_unlock(&alloc->alloc_lock);
        return -ENOSPC;
    }

    // First-Fit: 查找第一个满足条件的连续空闲块
    uint32_t consecutive = 0;
    uint32_t start = 0;

    for (uint32_t i = 0; i < alloc->total_blocks; i++) {
        if (!bitmap_test(alloc->bitmap, i)) {
            if (consecutive == 0) {
                start = i;
            }
            consecutive++;

            if (consecutive >= count) {
                // 找到足够的连续块
                for (uint32_t j = start; j < start + count; j++) {
                    bitmap_set(alloc->bitmap, j);
                }

                alloc->free_blocks -= count;
                alloc->alloc_count += count;

                *out_start = alloc->data_start + start;
                *out_count = count;

                pthread_mutex_unlock(&alloc->alloc_lock);
                return 0;
            }
        } else {
            consecutive = 0;
        }
    }

    // 没有找到足够的连续块
    fprintf(stderr, "block_alloc_multiple: no %u consecutive free blocks\n", count);
    pthread_mutex_unlock(&alloc->alloc_lock);
    return -ENOSPC;
}

// ============ 释放多个连续块 ============

int block_free_multiple(
    block_allocator_t *alloc,
    block_t start,
    uint32_t count
) {
    if (!alloc || count == 0) {
        return -EINVAL;
    }

    // 检查范围
    if (start < alloc->data_start ||
        start + count > alloc->data_start + alloc->total_blocks) {
        fprintf(stderr, "block_free_multiple: range [%u, %u) out of bounds\n",
                start, start + count);
        return -EINVAL;
    }

    pthread_mutex_lock(&alloc->alloc_lock);

    uint32_t bit_start = start - alloc->data_start;

    // 检查所有块是否已分配
    for (uint32_t i = 0; i < count; i++) {
        if (!bitmap_test(alloc->bitmap, bit_start + i)) {
            fprintf(stderr, "block_free_multiple: block %u is not allocated\n",
                    start + i);
            pthread_mutex_unlock(&alloc->alloc_lock);
            return -EINVAL;
        }
    }

    // 释放所有块
    for (uint32_t i = 0; i < count; i++) {
        bitmap_clear(alloc->bitmap, bit_start + i);
    }

    alloc->free_blocks += count;
    alloc->free_count += count;

    pthread_mutex_unlock(&alloc->alloc_lock);
    return 0;
}

// ============ 检查块是否已分配 ============

bool block_is_allocated(block_allocator_t *alloc, block_t block) {
    if (!alloc) return false;

    if (block < alloc->data_start || block >= alloc->data_start + alloc->total_blocks) {
        return false;
    }

    uint32_t bit = block - alloc->data_start;

    pthread_mutex_lock(&alloc->alloc_lock);
    bool allocated = bitmap_test(alloc->bitmap, bit);
    pthread_mutex_unlock(&alloc->alloc_lock);

    return allocated;
}

// ============ 同步位图到磁盘 ============

int block_alloc_sync(block_allocator_t *alloc) {
    if (!alloc) return -EINVAL;

    pthread_mutex_lock(&alloc->alloc_lock);

    // 使用初始化时保存的位图起始块
    uint32_t bitmap_start = alloc->bitmap_start;

    // 写入所有位图块
    for (uint32_t i = 0; i < alloc->bitmap_blocks; i++) {
        int ret = blkdev_write(
            alloc->dev,
            bitmap_start + i,
            alloc->bitmap + i * BLOCK_SIZE
        );
        if (ret < 0) {
            fprintf(stderr, "block_alloc_sync: blkdev_write failed for bitmap block %u\n", i);
            pthread_mutex_unlock(&alloc->alloc_lock);
            return ret;
        }
    }

    // 同步超级块中的统计信息(如果superblock已加载)
    if (alloc->dev->superblock) {
        alloc->dev->superblock->free_blocks = alloc->free_blocks;
        // inode统计由inode_cache管理，这里不更新
    }

    pthread_mutex_unlock(&alloc->alloc_lock);

    printf("[BALLOC] Synced bitmap to disk (blocks %u-%u)\n",
           bitmap_start, bitmap_start + alloc->bitmap_blocks - 1);
    return 0;
}

// ============ 获取统计信息 ============

void block_alloc_stats(
    block_allocator_t *alloc,
    uint32_t *total,
    uint32_t *free,
    uint32_t *used,
    float *usage
) {
    if (!alloc) return;

    pthread_mutex_lock(&alloc->alloc_lock);

    if (total) *total = alloc->total_blocks;
    if (free) *free = alloc->free_blocks;
    if (used) *used = alloc->total_blocks - alloc->free_blocks;
    if (usage) {
        *usage = (float)(alloc->total_blocks - alloc->free_blocks) / alloc->total_blocks;
    }

    pthread_mutex_unlock(&alloc->alloc_lock);
}

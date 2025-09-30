#ifndef MODERNFS_BLOCK_ALLOC_H
#define MODERNFS_BLOCK_ALLOC_H

#include "types.h"
#include <pthread.h>

// ============ 块分配器结构 ============

struct block_device;

typedef struct block_allocator {
    struct block_device *dev;       // 块设备
    uint8_t *bitmap;                // 内存中的位图
    uint32_t bitmap_blocks;         // 位图占用的块数
    uint32_t total_blocks;          // 总块数
    uint32_t free_blocks;           // 空闲块数
    uint32_t data_start;            // 数据区起始块号

    pthread_mutex_t alloc_lock;     // 分配锁

    // 统计信息
    uint64_t alloc_count;           // 分配次数
    uint64_t free_count;            // 释放次数
} block_allocator_t;

// ============ 块分配器API ============

/**
 * 初始化块分配器
 * @param dev 块设备
 * @param bitmap_start 位图起始块号
 * @param bitmap_blocks 位图块数
 * @param data_start 数据区起始块号
 * @param total_blocks 总数据块数
 * @return 成功返回分配器结构,失败返回NULL
 */
block_allocator_t* block_alloc_init(
    struct block_device *dev,
    uint32_t bitmap_start,
    uint32_t bitmap_blocks,
    uint32_t data_start,
    uint32_t total_blocks
);

/**
 * 销毁块分配器
 * @param alloc 分配器结构
 */
void block_alloc_destroy(block_allocator_t *alloc);

/**
 * 分配一个块
 * @param alloc 分配器结构
 * @return 成功返回块号,失败返回0
 */
block_t block_alloc(block_allocator_t *alloc);

/**
 * 释放一个块
 * @param alloc 分配器结构
 * @param block 块号
 * @return 0成功,负数为错误码
 */
int block_free(block_allocator_t *alloc, block_t block);

/**
 * 分配多个连续块
 * @param alloc 分配器结构
 * @param count 请求的块数
 * @param out_start 输出起始块号
 * @param out_count 输出实际分配的块数
 * @return 0成功,负数为错误码
 */
int block_alloc_multiple(
    block_allocator_t *alloc,
    uint32_t count,
    block_t *out_start,
    uint32_t *out_count
);

/**
 * 释放多个连续块
 * @param alloc 分配器结构
 * @param start 起始块号
 * @param count 块数
 * @return 0成功,负数为错误码
 */
int block_free_multiple(
    block_allocator_t *alloc,
    block_t start,
    uint32_t count
);

/**
 * 检查块是否已分配
 * @param alloc 分配器结构
 * @param block 块号
 * @return true已分配,false空闲
 */
bool block_is_allocated(block_allocator_t *alloc, block_t block);

/**
 * 同步位图到磁盘
 * @param alloc 分配器结构
 * @return 0成功,负数为错误码
 */
int block_alloc_sync(block_allocator_t *alloc);

/**
 * 获取分配器统计信息
 */
void block_alloc_stats(
    block_allocator_t *alloc,
    uint32_t *total,
    uint32_t *free,
    uint32_t *used,
    float *usage
);

#endif // MODERNFS_BLOCK_ALLOC_H
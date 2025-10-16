#ifndef MODERNFS_BUFFER_CACHE_H
#define MODERNFS_BUFFER_CACHE_H

#include "types.h"
#include <pthread.h>

// ============ 缓冲区头结构 ============

typedef struct buffer_head {
    block_t block_num;              // 块号
    uint8_t *data;                  // 数据缓冲区
    bool dirty;                     // 脏标志
    bool valid;                     // 有效标志
    int ref_count;                  // 引用计数
    pthread_rwlock_t lock;          // 读写锁

    // LRU链表
    struct buffer_head *next;
    struct buffer_head *prev;

    // 哈希表
    struct buffer_head *hash_next;
} buffer_head_t;

// ============ 缓存管理器结构 ============

typedef struct buffer_cache {
    buffer_head_t **hash_table;     // 哈希表
    uint32_t hash_size;             // 哈希表大小

    buffer_head_t *lru_head;        // LRU链表头
    buffer_head_t *lru_tail;        // LRU链表尾

    uint32_t max_buffers;           // 最大缓冲区数
    uint32_t current_buffers;       // 当前缓冲区数

    pthread_mutex_t cache_lock;     // 缓存锁

    // 统计信息
    uint64_t hit_count;             // 命中次数
    uint64_t miss_count;            // 未命中次数
    uint64_t evict_count;           // 淘汰次数
} buffer_cache_t;

// ============ 缓存API ============

/**
 * 初始化缓存
 * @param max_buffers 最大缓冲区数
 * @return 成功返回缓存结构,失败返回NULL
 */
buffer_cache_t* buffer_cache_init(uint32_t max_buffers);

/**
 * 销毁缓存
 * @param cache 缓存结构
 */
void buffer_cache_destroy(buffer_cache_t *cache);

/**
 * 查找缓存块
 * @param cache 缓存结构
 * @param block 块号
 * @return 成功返回缓冲区头,失败返回NULL
 */
buffer_head_t* buffer_cache_lookup(buffer_cache_t *cache, block_t block);

/**
 * 插入缓存块
 * @param cache 缓存结构
 * @param block 块号
 * @param data 数据(BLOCK_SIZE字节)
 * @return 成功返回缓冲区头,失败返回NULL
 */
buffer_head_t* buffer_cache_insert(buffer_cache_t *cache, block_t block, const void *data);

/**
 * 获取缓冲区(增加引用计数)
 * @param bh 缓冲区头
 */
void buffer_head_get(buffer_head_t *bh);

/**
 * 释放缓冲区(减少引用计数)
 * @param bh 缓冲区头
 */
void buffer_head_put(buffer_head_t *bh);

/**
 * 标记缓冲区为脏
 * @param bh 缓冲区头
 */
void buffer_head_mark_dirty(buffer_head_t *bh);

/**
 * 写回所有脏缓冲区
 * @param cache 缓存结构
 * @param dev_fd 设备文件描述符
 * @return 0成功,负数为错误码
 */
int buffer_cache_sync(buffer_cache_t *cache, int dev_fd);

/**
 * 获取缓存统计信息
 */
void buffer_cache_stats(buffer_cache_t *cache, uint64_t *hits, uint64_t *misses,
                        uint64_t *evicts, float *hit_rate);

/**
 * 使指定块的缓存失效
 * @param cache 缓存结构
 * @param block 块号
 */
void buffer_cache_invalidate(buffer_cache_t *cache, block_t block);

#endif // MODERNFS_BUFFER_CACHE_H
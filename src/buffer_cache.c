#include "modernfs/buffer_cache.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

// ============ 哈希函数 ============

static inline uint32_t hash_block(block_t block, uint32_t hash_size) {
    return block % hash_size;
}

// ============ LRU链表操作 ============

// 从LRU链表中移除节点
static void lru_remove(buffer_cache_t *cache, buffer_head_t *bh) {
    if (bh->prev) {
        bh->prev->next = bh->next;
    } else {
        cache->lru_head = bh->next;
    }

    if (bh->next) {
        bh->next->prev = bh->prev;
    } else {
        cache->lru_tail = bh->prev;
    }

    bh->prev = bh->next = NULL;
}

// 将节点添加到LRU链表头部(最近使用)
static void lru_add_to_head(buffer_cache_t *cache, buffer_head_t *bh) {
    bh->next = cache->lru_head;
    bh->prev = NULL;

    if (cache->lru_head) {
        cache->lru_head->prev = bh;
    } else {
        cache->lru_tail = bh;
    }

    cache->lru_head = bh;
}

// 将节点移动到LRU链表头部
static void lru_move_to_head(buffer_cache_t *cache, buffer_head_t *bh) {
    if (cache->lru_head == bh) {
        return; // 已经在头部
    }

    lru_remove(cache, bh);
    lru_add_to_head(cache, bh);
}

// ============ 哈希表操作 ============

// 从哈希表中查找
static buffer_head_t* hash_lookup(buffer_cache_t *cache, block_t block) {
    uint32_t hash = hash_block(block, cache->hash_size);
    buffer_head_t *bh = cache->hash_table[hash];

    while (bh) {
        if (bh->block_num == block) {
            return bh;
        }
        bh = bh->hash_next;
    }

    return NULL;
}

// 添加到哈希表
static void hash_insert(buffer_cache_t *cache, buffer_head_t *bh) {
    uint32_t hash = hash_block(bh->block_num, cache->hash_size);
    bh->hash_next = cache->hash_table[hash];
    cache->hash_table[hash] = bh;
}

// 从哈希表中移除
static void hash_remove(buffer_cache_t *cache, buffer_head_t *bh) {
    uint32_t hash = hash_block(bh->block_num, cache->hash_size);
    buffer_head_t **ptr = &cache->hash_table[hash];

    while (*ptr) {
        if (*ptr == bh) {
            *ptr = bh->hash_next;
            bh->hash_next = NULL;
            return;
        }
        ptr = &(*ptr)->hash_next;
    }
}

// ============ 缓冲区头操作 ============

static buffer_head_t* buffer_head_alloc(block_t block) {
    buffer_head_t *bh = malloc(sizeof(buffer_head_t));
    if (!bh) {
        return NULL;
    }

    // 使用posix_memalign替代aligned_alloc,更可靠
    if (posix_memalign((void**)&bh->data, BLOCK_SIZE, BLOCK_SIZE) != 0) {
        free(bh);
        return NULL;
    }

    bh->block_num = block;
    bh->dirty = false;
    bh->valid = false;
    bh->ref_count = 1;
    bh->next = bh->prev = bh->hash_next = NULL;

    pthread_rwlock_init(&bh->lock, NULL);

    return bh;
}

static void buffer_head_free(buffer_head_t *bh) {
    if (!bh) return;

    pthread_rwlock_destroy(&bh->lock);
    free(bh->data);
    free(bh);
}

// ============ 淘汰策略 ============

// 注意: 此函数当前未使用，但保留用于未来可能的显式淘汰需求
__attribute__((unused))
static int evict_lru_buffer(buffer_cache_t *cache, int dev_fd) {
    // 从尾部开始查找可淘汰的缓冲区(引用计数为0)
    buffer_head_t *bh = cache->lru_tail;

    while (bh) {
        if (bh->ref_count == 0) {
            // 如果是脏块,先写回
            if (bh->dirty) {
                off_t offset = (off_t)bh->block_num * BLOCK_SIZE;
                ssize_t n = pwrite(dev_fd, bh->data, BLOCK_SIZE, offset);
                if (n != BLOCK_SIZE) {
                    fprintf(stderr, "evict_lru_buffer: pwrite failed\n");
                    return -EIO;
                }
            }

            // 从LRU链表和哈希表中移除
            lru_remove(cache, bh);
            hash_remove(cache, bh);

            // 释放缓冲区
            buffer_head_free(bh);

            cache->current_buffers--;
            cache->evict_count++;

            return 0;
        }
        bh = bh->prev;
    }

    // 所有缓冲区都在使用中
    fprintf(stderr, "evict_lru_buffer: all buffers in use\n");
    return -ENOMEM;
}

// ============ 缓存API实现 ============

buffer_cache_t* buffer_cache_init(uint32_t max_buffers) {
    buffer_cache_t *cache = malloc(sizeof(buffer_cache_t));
    if (!cache) {
        return NULL;
    }

    // 哈希表大小为最大缓冲区数的2倍(减少冲突)
    cache->hash_size = max_buffers * 2;
    cache->hash_table = calloc(cache->hash_size, sizeof(buffer_head_t*));
    if (!cache->hash_table) {
        free(cache);
        return NULL;
    }

    cache->lru_head = NULL;
    cache->lru_tail = NULL;
    cache->max_buffers = max_buffers;
    cache->current_buffers = 0;

    pthread_mutex_init(&cache->cache_lock, NULL);

    cache->hit_count = 0;
    cache->miss_count = 0;
    cache->evict_count = 0;

    printf("[CACHE] Initialized: max_buffers=%u, hash_size=%u\n",
           max_buffers, cache->hash_size);

    return cache;
}

void buffer_cache_destroy(buffer_cache_t *cache) {
    if (!cache) return;

    // 释放所有缓冲区
    buffer_head_t *bh = cache->lru_head;
    while (bh) {
        buffer_head_t *next = bh->next;
        buffer_head_free(bh);
        bh = next;
    }

    free(cache->hash_table);
    pthread_mutex_destroy(&cache->cache_lock);
    free(cache);

    printf("[CACHE] Destroyed\n");
}

buffer_head_t* buffer_cache_lookup(buffer_cache_t *cache, block_t block) {
    if (!cache) return NULL;

    pthread_mutex_lock(&cache->cache_lock);

    buffer_head_t *bh = hash_lookup(cache, block);

    if (bh) {
        // 缓存命中
        bh->ref_count++;
        lru_move_to_head(cache, bh);
        cache->hit_count++;

        pthread_mutex_unlock(&cache->cache_lock);
        return bh;
    }

    // 缓存未命中
    cache->miss_count++;
    pthread_mutex_unlock(&cache->cache_lock);

    return NULL;
}

buffer_head_t* buffer_cache_insert(buffer_cache_t *cache, block_t block, const void *data) {
    if (!cache || !data) return NULL;

    pthread_mutex_lock(&cache->cache_lock);

    // 检查是否已存在
    buffer_head_t *bh = hash_lookup(cache, block);
    if (bh) {
        // 已存在,更新数据
        pthread_rwlock_wrlock(&bh->lock);
        memcpy(bh->data, data, BLOCK_SIZE);
        bh->valid = true;
        pthread_rwlock_unlock(&bh->lock);

        bh->ref_count++;
        lru_move_to_head(cache, bh);

        pthread_mutex_unlock(&cache->cache_lock);
        return bh;
    }

    // 检查是否需要淘汰
    if (cache->current_buffers >= cache->max_buffers) {
        // 假设dev_fd不可用,只能放弃插入
        // 注意:这里需要传入dev_fd,暂时返回NULL
        pthread_mutex_unlock(&cache->cache_lock);
        return NULL;
    }

    // 分配新缓冲区
    bh = buffer_head_alloc(block);
    if (!bh) {
        pthread_mutex_unlock(&cache->cache_lock);
        return NULL;
    }

    memcpy(bh->data, data, BLOCK_SIZE);
    bh->valid = true;

    // 加入哈希表和LRU链表
    hash_insert(cache, bh);
    lru_add_to_head(cache, bh);

    cache->current_buffers++;

    pthread_mutex_unlock(&cache->cache_lock);

    return bh;
}

void buffer_head_get(buffer_head_t *bh) {
    if (!bh) return;
    __sync_fetch_and_add(&bh->ref_count, 1);
}

void buffer_head_put(buffer_head_t *bh) {
    if (!bh) return;
    __sync_fetch_and_sub(&bh->ref_count, 1);
}

void buffer_head_mark_dirty(buffer_head_t *bh) {
    if (!bh) return;
    bh->dirty = true;
}

int buffer_cache_sync(buffer_cache_t *cache, int dev_fd) {
    if (!cache) return -EINVAL;

    pthread_mutex_lock(&cache->cache_lock);

    buffer_head_t *bh = cache->lru_head;
    int synced = 0;

    while (bh) {
        if (bh->dirty) {
            pthread_rwlock_rdlock(&bh->lock);

            off_t offset = (off_t)bh->block_num * BLOCK_SIZE;
            ssize_t n = pwrite(dev_fd, bh->data, BLOCK_SIZE, offset);

            pthread_rwlock_unlock(&bh->lock);

            if (n != BLOCK_SIZE) {
                fprintf(stderr, "buffer_cache_sync: pwrite failed for block %u\n",
                        bh->block_num);
                pthread_mutex_unlock(&cache->cache_lock);
                return -EIO;
            }

            bh->dirty = false;
            synced++;
        }

        bh = bh->next;
    }

    pthread_mutex_unlock(&cache->cache_lock);

    if (synced > 0) {
        printf("[CACHE] Synced %d dirty buffers\n", synced);
    }

    return 0;
}

void buffer_cache_stats(buffer_cache_t *cache, uint64_t *hits, uint64_t *misses,
                        uint64_t *evicts, float *hit_rate) {
    if (!cache) return;

    pthread_mutex_lock(&cache->cache_lock);

    if (hits) *hits = cache->hit_count;
    if (misses) *misses = cache->miss_count;
    if (evicts) *evicts = cache->evict_count;

    if (hit_rate) {
        uint64_t total = cache->hit_count + cache->miss_count;
        *hit_rate = (total > 0) ? ((float)cache->hit_count / total) : 0.0f;
    }

    pthread_mutex_unlock(&cache->cache_lock);
}

void buffer_cache_invalidate(buffer_cache_t *cache, block_t block) {
    if (!cache) return;

    pthread_mutex_lock(&cache->cache_lock);

    buffer_head_t *bh = hash_lookup(cache, block);
    if (bh) {
        // 找到了该块,将其标记为无效
        pthread_rwlock_wrlock(&bh->lock);
        bh->valid = false;
        bh->dirty = false;  // 清除脏标志,因为数据已经过期
        pthread_rwlock_unlock(&bh->lock);

        fprintf(stderr, "[CACHE] Invalidated block %u\n", block);
    }

    pthread_mutex_unlock(&cache->cache_lock);
}
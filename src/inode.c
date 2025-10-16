#include "modernfs/inode.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <sys/types.h>
#include <time.h>

// ============ 内部辅助函数 ============

static uint32_t inode_hash(inode_t inum, uint32_t hash_size) {
    return inum % hash_size;
}

static void lru_remove(inode_cache_t *cache, inode_t_mem *inode) {
    if (inode->prev) {
        inode->prev->next = inode->next;
    } else {
        cache->lru_head = inode->next;
    }

    if (inode->next) {
        inode->next->prev = inode->prev;
    } else {
        cache->lru_tail = inode->prev;
    }

    inode->prev = inode->next = NULL;
}

static void lru_push_front(inode_cache_t *cache, inode_t_mem *inode) {
    inode->next = cache->lru_head;
    inode->prev = NULL;

    if (cache->lru_head) {
        cache->lru_head->prev = inode;
    } else {
        cache->lru_tail = inode;
    }

    cache->lru_head = inode;
}

static void hash_insert(inode_cache_t *cache, inode_t_mem *inode) {
    uint32_t h = inode_hash(inode->inum, cache->hash_size);
    inode->hash_next = cache->hash_table[h];
    cache->hash_table[h] = inode;
}

static void hash_remove(inode_cache_t *cache, inode_t_mem *inode) {
    uint32_t h = inode_hash(inode->inum, cache->hash_size);
    inode_t_mem **pp = &cache->hash_table[h];

    while (*pp && *pp != inode) {
        pp = &(*pp)->hash_next;
    }

    if (*pp) {
        *pp = inode->hash_next;
    }
    inode->hash_next = NULL;
}

static inode_t_mem *hash_lookup(inode_cache_t *cache, inode_t inum) {
    uint32_t h = inode_hash(inum, cache->hash_size);
    inode_t_mem *p = cache->hash_table[h];

    while (p && p->inum != inum) {
        p = p->hash_next;
    }

    return p;
}

// 读取超级块
static int read_superblock(inode_cache_t *cache) {
    uint8_t *buf = malloc(BLOCK_SIZE);
    if (!buf) {
        return MODERNFS_ERROR;
    }

    int ret = blkdev_read(cache->dev, SUPERBLOCK_BLOCK, buf);
    if (ret != 0) {
        free(buf);
        return MODERNFS_EIO;
    }

    memcpy(&cache->sb, buf, sizeof(superblock_t));
    free(buf);

    if (cache->sb.magic != SUPERBLOCK_MAGIC) {
        fprintf(stderr, "Invalid superblock magic: 0x%x\n", cache->sb.magic);
        return MODERNFS_EINVAL;
    }

    return MODERNFS_SUCCESS;
}

// 加载Inode位图
static int load_inode_bitmap(inode_cache_t *cache) {
    cache->bitmap_blocks = cache->sb.inode_bitmap_blocks;
    size_t bitmap_size = cache->bitmap_blocks * BLOCK_SIZE;

    cache->inode_bitmap = malloc(bitmap_size);
    if (!cache->inode_bitmap) {
        return MODERNFS_ERROR;
    }

    for (uint32_t i = 0; i < cache->bitmap_blocks; i++) {
        int ret = blkdev_read(cache->dev, cache->sb.inode_bitmap_start + i,
                              cache->inode_bitmap + i * BLOCK_SIZE);
        if (ret != 0) {
            free(cache->inode_bitmap);
            cache->inode_bitmap = NULL;
            return MODERNFS_EIO;
        }
    }

    return MODERNFS_SUCCESS;
}

// 同步Inode位图到磁盘
static int sync_inode_bitmap(inode_cache_t *cache) {
    pthread_mutex_lock(&cache->bitmap_lock);

    for (uint32_t i = 0; i < cache->bitmap_blocks; i++) {
        int ret = blkdev_write(cache->dev, cache->sb.inode_bitmap_start + i,
                               cache->inode_bitmap + i * BLOCK_SIZE);
        if (ret != 0) {
            pthread_mutex_unlock(&cache->bitmap_lock);
            return MODERNFS_EIO;
        }
    }

    pthread_mutex_unlock(&cache->bitmap_lock);
    return MODERNFS_SUCCESS;
}

// ============ Inode缓存初始化和销毁 ============

inode_cache_t *inode_cache_init(block_device_t *dev,
                                block_allocator_t *balloc,
                                uint32_t max_inodes,
                                uint32_t hash_size) {
    inode_cache_t *cache = calloc(1, sizeof(inode_cache_t));
    if (!cache) {
        return NULL;
    }

    cache->dev = dev;
    cache->balloc = balloc;
    cache->max_inodes = max_inodes;
    cache->hash_size = hash_size;

    // 读取超级块
    if (read_superblock(cache) != MODERNFS_SUCCESS) {
        free(cache);
        return NULL;
    }

    // 分配Inode池
    cache->inodes = calloc(max_inodes, sizeof(inode_t_mem));
    if (!cache->inodes) {
        free(cache);
        return NULL;
    }

    // 初始化每个Inode
    for (uint32_t i = 0; i < max_inodes; i++) {
        pthread_mutex_init(&cache->inodes[i].lock, NULL);
        cache->inodes[i].ref_count = 0;
        cache->inodes[i].valid = 0;
        lru_push_front(cache, &cache->inodes[i]);
    }

    // 分配哈希表
    cache->hash_table = calloc(hash_size, sizeof(inode_t_mem *));
    if (!cache->hash_table) {
        free(cache->inodes);
        free(cache);
        return NULL;
    }

    pthread_rwlock_init(&cache->cache_lock, NULL);
    pthread_mutex_init(&cache->bitmap_lock, NULL);

    // 加载Inode位图
    if (load_inode_bitmap(cache) != MODERNFS_SUCCESS) {
        free(cache->hash_table);
        free(cache->inodes);
        free(cache);
        return NULL;
    }

    return cache;
}

void inode_cache_destroy(inode_cache_t *cache) {
    if (!cache) return;

    // 同步所有脏Inode
    inode_sync_all(cache);

    // 同步位图
    sync_inode_bitmap(cache);

    // 释放资源
    if (cache->inode_bitmap) {
        free(cache->inode_bitmap);
    }

    if (cache->hash_table) {
        free(cache->hash_table);
    }

    if (cache->inodes) {
        for (uint32_t i = 0; i < cache->max_inodes; i++) {
            pthread_mutex_destroy(&cache->inodes[i].lock);
        }
        free(cache->inodes);
    }

    pthread_rwlock_destroy(&cache->cache_lock);
    pthread_mutex_destroy(&cache->bitmap_lock);

    free(cache);
}

// ============ Inode分配和释放 ============

inode_t_mem *inode_alloc(inode_cache_t *cache, uint8_t type) {
    pthread_mutex_lock(&cache->bitmap_lock);

    // 在位图中查找空闲Inode
    inode_t inum = 0;
    int found = 0;

    for (uint32_t i = 0; i < cache->sb.total_inodes; i++) {
        uint32_t byte_idx = i / 8;
        uint32_t bit_idx = i % 8;

        if (!(cache->inode_bitmap[byte_idx] & (1 << bit_idx))) {
            inum = i;
            cache->inode_bitmap[byte_idx] |= (1 << bit_idx);
            found = 1;
            break;
        }
    }

    pthread_mutex_unlock(&cache->bitmap_lock);

    if (!found) {
        errno = ENOSPC;
        return NULL;
    }

    // 获取Inode
    inode_t_mem *inode = inode_get(cache, inum);
    if (!inode) {
        // 回滚位图
        pthread_mutex_lock(&cache->bitmap_lock);
        uint32_t byte_idx = inum / 8;
        uint32_t bit_idx = inum % 8;
        cache->inode_bitmap[byte_idx] &= ~(1 << bit_idx);
        pthread_mutex_unlock(&cache->bitmap_lock);
        return NULL;
    }

    // 初始化Inode
    inode_lock(inode);
    memset(&inode->disk, 0, sizeof(disk_inode_t));
    inode->disk.type = type;
    inode->disk.nlink = 1;
    inode->disk.size = 0;
    inode->disk.blocks = 0;
    inode->disk.ctime = inode->disk.mtime = inode->disk.atime = time(NULL);
    inode->valid = 1;  // 确保 valid=1 以便 inode_sync 可以正常工作
    inode->dirty = 1;

    // 立即同步到磁盘,避免后续读取到垃圾数据
    if (inode_sync(cache, inode) != MODERNFS_SUCCESS) {
        inode_unlock(inode);
        // 回滚位图
        pthread_mutex_lock(&cache->bitmap_lock);
        uint32_t byte_idx = inum / 8;
        uint32_t bit_idx = inum % 8;
        cache->inode_bitmap[byte_idx] &= ~(1 << bit_idx);
        pthread_mutex_unlock(&cache->bitmap_lock);
        inode_put(cache, inode);
        return NULL;
    }
    inode_unlock(inode);

    // 更新超级块
    cache->sb.free_inodes--;

    // 同步位图
    sync_inode_bitmap(cache);

    return inode;
}

int inode_free(inode_cache_t *cache, inode_t_mem *inode) {
    if (!inode) {
        return MODERNFS_EINVAL;
    }

    inode_lock(inode);

    // 释放所有数据块
    int ret = inode_truncate(cache, inode, 0);
    if (ret < 0) {
        inode_unlock(inode);
        return ret;
    }

    inode_t inum = inode->inum;

    inode_unlock(inode);
    inode_put(cache, inode);

    // 清除位图
    pthread_mutex_lock(&cache->bitmap_lock);
    uint32_t byte_idx = inum / 8;
    uint32_t bit_idx = inum % 8;
    cache->inode_bitmap[byte_idx] &= ~(1 << bit_idx);
    pthread_mutex_unlock(&cache->bitmap_lock);

    // 更新超级块
    cache->sb.free_inodes++;

    // 同步位图
    sync_inode_bitmap(cache);

    return MODERNFS_SUCCESS;
}

// ============ Inode获取和释放引用 ============

inode_t_mem *inode_get(inode_cache_t *cache, inode_t inum) {
    pthread_rwlock_wrlock(&cache->cache_lock);

    // 在哈希表中查找
    inode_t_mem *inode = hash_lookup(cache, inum);

    if (inode) {
        // 找到了，增加引用计数
        inode->ref_count++;
        lru_remove(cache, inode);
        lru_push_front(cache, inode);
        pthread_rwlock_unlock(&cache->cache_lock);

        // 如果未加载，从磁盘读取
        if (!inode->valid) {
            inode_lock(inode);
            if (!inode->valid) {
                uint32_t inode_block = cache->sb.inode_table_start +
                                       (inum * INODE_SIZE) / BLOCK_SIZE;
                uint32_t offset = (inum * INODE_SIZE) % BLOCK_SIZE;

                uint8_t *buf = malloc(BLOCK_SIZE);
                if (!buf || blkdev_read(cache->dev, inode_block, buf) != 0) {
                    free(buf);
                    inode_unlock(inode);
                    inode_put(cache, inode);
                    return NULL;
                }

                memcpy(&inode->disk, buf + offset, sizeof(disk_inode_t));
                free(buf);

                inode->valid = 1;
            }
            inode_unlock(inode);
        }

        return inode;
    }

    // 没找到，需要分配一个缓存条目
    // 从LRU尾部查找引用计数为0的Inode
    inode = cache->lru_tail;
    while (inode && inode->ref_count > 0) {
        inode = inode->prev;
    }

    if (!inode) {
        pthread_rwlock_unlock(&cache->cache_lock);
        errno = ENOMEM;
        return NULL;
    }

    // 如果是脏的，先写回
    if (inode->dirty && inode->valid) {
        inode_sync(cache, inode);
    }

    // 从哈希表和LRU中移除
    if (inode->valid) {
        hash_remove(cache, inode);
    }
    lru_remove(cache, inode);

    // 重新初始化
    inode->inum = inum;
    inode->ref_count = 1;
    inode->valid = 0;
    inode->dirty = 0;

    // 插入哈希表和LRU
    hash_insert(cache, inode);
    lru_push_front(cache, inode);

    pthread_rwlock_unlock(&cache->cache_lock);

    // 从磁盘读取
    inode_lock(inode);
    uint32_t inode_block = cache->sb.inode_table_start +
                           (inum * INODE_SIZE) / BLOCK_SIZE;
    uint32_t offset = (inum * INODE_SIZE) % BLOCK_SIZE;

    uint8_t *buf = malloc(BLOCK_SIZE);
    if (!buf || blkdev_read(cache->dev, inode_block, buf) != 0) {
        free(buf);
        inode_unlock(inode);
        inode_put(cache, inode);
        return NULL;
    }

    memcpy(&inode->disk, buf + offset, sizeof(disk_inode_t));
    free(buf);

    inode->valid = 1;
    inode_unlock(inode);

    return inode;
}

void inode_put(inode_cache_t *cache, inode_t_mem *inode) {
    if (!inode) return;

    pthread_rwlock_wrlock(&cache->cache_lock);
    inode->ref_count--;
    pthread_rwlock_unlock(&cache->cache_lock);
}

void inode_lock(inode_t_mem *inode) {
    pthread_mutex_lock(&inode->lock);
}

void inode_unlock(inode_t_mem *inode) {
    pthread_mutex_unlock(&inode->lock);
}

// ============ Inode读写 ============

int inode_sync(inode_cache_t *cache, inode_t_mem *inode) {
    if (!inode || !inode->valid || !inode->dirty) {
        return MODERNFS_SUCCESS;
    }

    uint32_t inode_block = cache->sb.inode_table_start +
                           (inode->inum * INODE_SIZE) / BLOCK_SIZE;
    uint32_t offset = (inode->inum * INODE_SIZE) % BLOCK_SIZE;

    uint8_t *buf = malloc(BLOCK_SIZE);
    if (!buf) {
        return MODERNFS_ERROR;
    }

    // 读取-修改-写入
    if (blkdev_read(cache->dev, inode_block, buf) != 0) {
        free(buf);
        return MODERNFS_EIO;
    }

    memcpy(buf + offset, &inode->disk, sizeof(disk_inode_t));

    if (blkdev_write(cache->dev, inode_block, buf) != 0) {
        free(buf);
        return MODERNFS_EIO;
    }

    free(buf);
    inode->dirty = 0;

    return MODERNFS_SUCCESS;
}

int inode_sync_all(inode_cache_t *cache) {
    pthread_rwlock_rdlock(&cache->cache_lock);

    for (uint32_t i = 0; i < cache->max_inodes; i++) {
        if (cache->inodes[i].valid && cache->inodes[i].dirty) {
            inode_sync(cache, &cache->inodes[i]);
        }
    }

    pthread_rwlock_unlock(&cache->cache_lock);

    return MODERNFS_SUCCESS;
}

// ============ 数据块映射 ============

// 每个间接块可以存储多少个块号
#define INDIRECT_BLOCKS_PER_BLOCK (BLOCK_SIZE / sizeof(block_t))

int inode_bmap(inode_cache_t *cache,
               inode_t_mem *inode,
               uint64_t offset,
               bool alloc_if_missing,
               block_t *block_out) {
    if (!inode || !block_out) {
        return MODERNFS_EINVAL;
    }

    uint32_t block_idx = offset / BLOCK_SIZE;

    // 直接块
    if (block_idx < INODE_DIRECT_BLOCKS) {
        if (inode->disk.direct[block_idx] == 0) {
            if (!alloc_if_missing) {
                *block_out = 0;
                return MODERNFS_SUCCESS;
            }

            // 分配新块
            block_t new_block = block_alloc(cache->balloc);
            if (new_block == 0) {
                return MODERNFS_ENOSPC;
            }

            inode->disk.direct[block_idx] = new_block;
            inode->disk.blocks++;
            inode->dirty = 1;
        }

        *block_out = inode->disk.direct[block_idx];
        return MODERNFS_SUCCESS;
    }

    block_idx -= INODE_DIRECT_BLOCKS;

    // 一级间接块
    if (block_idx < INDIRECT_BLOCKS_PER_BLOCK) {
        if (inode->disk.indirect == 0) {
            if (!alloc_if_missing) {
                *block_out = 0;
                return MODERNFS_SUCCESS;
            }

            // 分配间接块
            block_t new_block = block_alloc(cache->balloc);
            if (new_block == 0) {
                return MODERNFS_ENOSPC;
            }

            inode->disk.indirect = new_block;
            inode->disk.blocks++;
            inode->dirty = 1;

            // 清零间接块
            uint8_t *buf = malloc(BLOCK_SIZE);
            if (!buf) {
                return MODERNFS_ERROR;
            }
            memset(buf, 0, BLOCK_SIZE);
            if (blkdev_write(cache->dev, new_block, buf) != 0) {
                free(buf);
                return MODERNFS_EIO;
            }
            free(buf);
        }

        // 读取间接块
        uint8_t *buf = malloc(BLOCK_SIZE);
        if (!buf) {
            return MODERNFS_ERROR;
        }
        if (blkdev_read(cache->dev, inode->disk.indirect, buf) != 0) {
            free(buf);
            return MODERNFS_EIO;
        }

        block_t *indirect = (block_t *)buf;

        if (indirect[block_idx] == 0) {
            if (!alloc_if_missing) {
                free(buf);
                *block_out = 0;
                return MODERNFS_SUCCESS;
            }

            // 分配新块
            block_t new_block = block_alloc(cache->balloc);
            if (new_block == 0) {
                free(buf);
                return MODERNFS_ENOSPC;
            }

            indirect[block_idx] = new_block;
            if (blkdev_write(cache->dev, inode->disk.indirect, buf) != 0) {
                free(buf);
                return MODERNFS_EIO;
            }
            inode->disk.blocks++;
            inode->dirty = 1;
        }

        *block_out = indirect[block_idx];
        free(buf);

        return MODERNFS_SUCCESS;
    }

    block_idx -= INDIRECT_BLOCKS_PER_BLOCK;

    // 二级间接块
    if (block_idx < INDIRECT_BLOCKS_PER_BLOCK * INDIRECT_BLOCKS_PER_BLOCK) {
        if (inode->disk.double_indirect == 0) {
            if (!alloc_if_missing) {
                *block_out = 0;
                return MODERNFS_SUCCESS;
            }

            // 分配二级间接块
            block_t new_block = block_alloc(cache->balloc);
            if (new_block == 0) {
                return MODERNFS_ENOSPC;
            }

            inode->disk.double_indirect = new_block;
            inode->disk.blocks++;
            inode->dirty = 1;

            // 清零
            uint8_t *buf = malloc(BLOCK_SIZE);
            if (!buf) {
                return MODERNFS_ERROR;
            }
            memset(buf, 0, BLOCK_SIZE);
            if (blkdev_write(cache->dev, new_block, buf) != 0) {
                free(buf);
                return MODERNFS_EIO;
            }
            free(buf);
        }

        uint32_t l1_idx = block_idx / INDIRECT_BLOCKS_PER_BLOCK;
        uint32_t l2_idx = block_idx % INDIRECT_BLOCKS_PER_BLOCK;

        // 读取二级间接块
        uint8_t *buf1 = malloc(BLOCK_SIZE);
        if (!buf1) {
            return MODERNFS_ERROR;
        }
        if (blkdev_read(cache->dev, inode->disk.double_indirect, buf1) != 0) {
            free(buf1);
            return MODERNFS_EIO;
        }

        block_t *double_indirect = (block_t *)buf1;

        if (double_indirect[l1_idx] == 0) {
            if (!alloc_if_missing) {
                free(buf1);
                *block_out = 0;
                return MODERNFS_SUCCESS;
            }

            // 分配一级间接块
            block_t new_block = block_alloc(cache->balloc);
            if (new_block == 0) {
                free(buf1);
                return MODERNFS_ENOSPC;
            }

            double_indirect[l1_idx] = new_block;
            if (blkdev_write(cache->dev, inode->disk.double_indirect, buf1) != 0) {
                free(buf1);
                return MODERNFS_EIO;
            }
            inode->disk.blocks++;
            inode->dirty = 1;

            // 清零新分配的间接块
            uint8_t *buf2 = malloc(BLOCK_SIZE);
            if (!buf2) {
                free(buf1);
                return MODERNFS_ERROR;
            }
            memset(buf2, 0, BLOCK_SIZE);
            if (blkdev_write(cache->dev, new_block, buf2) != 0) {
                free(buf2);
                free(buf1);
                return MODERNFS_EIO;
            }
            free(buf2);
        }

        block_t indirect_block = double_indirect[l1_idx];
        free(buf1);

        // 读取一级间接块
        uint8_t *buf2 = malloc(BLOCK_SIZE);
        if (!buf2) {
            return MODERNFS_ERROR;
        }
        if (blkdev_read(cache->dev, indirect_block, buf2) != 0) {
            free(buf2);
            return MODERNFS_EIO;
        }

        block_t *indirect = (block_t *)buf2;

        if (indirect[l2_idx] == 0) {
            if (!alloc_if_missing) {
                free(buf2);
                *block_out = 0;
                return MODERNFS_SUCCESS;
            }

            // 分配新块
            block_t new_block = block_alloc(cache->balloc);
            if (new_block == 0) {
                free(buf2);
                return MODERNFS_ENOSPC;
            }

            indirect[l2_idx] = new_block;
            if (blkdev_write(cache->dev, indirect_block, buf2) != 0) {
                free(buf2);
                return MODERNFS_EIO;
            }
            inode->disk.blocks++;
            inode->dirty = 1;
        }

        *block_out = indirect[l2_idx];
        free(buf2);

        return MODERNFS_SUCCESS;
    }

    // 超出范围
    return MODERNFS_EINVAL;
}

int inode_truncate(inode_cache_t *cache, inode_t_mem *inode, uint64_t new_size) {
    if (!inode) {
        return MODERNFS_EINVAL;
    }

    if (new_size >= inode->disk.size) {
        inode->disk.size = new_size;
        inode->dirty = 1;
        return MODERNFS_SUCCESS;
    }

    // 计算需要保留的块数
    uint32_t new_blocks = (new_size + BLOCK_SIZE - 1) / BLOCK_SIZE;
    uint32_t old_blocks = (inode->disk.size + BLOCK_SIZE - 1) / BLOCK_SIZE;

    // 释放多余的块（从后往前）
    for (uint32_t i = new_blocks; i < old_blocks; i++) {
        block_t block;
        int ret = inode_bmap(cache, inode, i * BLOCK_SIZE, false, &block);
        if (ret == MODERNFS_SUCCESS && block != 0) {
            block_free(cache->balloc, block);
            inode->disk.blocks--;
        }
    }

    inode->disk.size = new_size;
    inode->dirty = 1;

    return MODERNFS_SUCCESS;
}

// ============ Inode读写数据 ============

ssize_t inode_read(inode_cache_t *cache,
                   inode_t_mem *inode,
                   void *buf,
                   uint64_t offset,
                   size_t size) {
    if (!inode || !buf) {
        return MODERNFS_EINVAL;
    }

    // fprintf(stderr, "[DEBUG] inode_read: inum=%u, size=%lu, offset=%lu, read_size=%zu\n",
    //         inode->inum, inode->disk.size, offset, size);

    if (offset >= inode->disk.size) {
        // fprintf(stderr, "[DEBUG] inode_read: offset >= size, returning 0\n");
        return 0;
    }

    if (offset + size > inode->disk.size) {
        size = inode->disk.size - offset;
        // fprintf(stderr, "[DEBUG] inode_read: adjusted size to %zu\n", size);
    }

    size_t total_read = 0;
    uint8_t *dest = (uint8_t *)buf;

    while (total_read < size) {
        uint64_t cur_offset = offset + total_read;
        uint32_t block_offset = cur_offset % BLOCK_SIZE;
        uint32_t to_read = BLOCK_SIZE - block_offset;

        if (to_read > size - total_read) {
            to_read = size - total_read;
        }

        block_t block;
        int ret = inode_bmap(cache, inode, cur_offset, false, &block);
        if (ret < 0) {
            // fprintf(stderr, "[DEBUG] inode_read: bmap failed with %d\n", ret);
            return ret;
        }

        // fprintf(stderr, "[DEBUG] inode_read: cur_offset=%lu, block=%u, block_offset=%u, to_read=%u\n",
        //         cur_offset, block, block_offset, to_read);

        if (block == 0) {
            // 空洞，填充0
            // fprintf(stderr, "[DEBUG] inode_read: hole detected, filling with zeros\n");
            memset(dest + total_read, 0, to_read);
        } else {
            uint8_t *bbuf = malloc(BLOCK_SIZE);
            if (!bbuf) {
                return MODERNFS_ERROR;
            }

            if (blkdev_read(cache->dev, block, bbuf) != 0) {
                // fprintf(stderr, "[DEBUG] inode_read: blkdev_read failed for block %u\n", block);
                free(bbuf);
                return MODERNFS_EIO;
            }

            // fprintf(stderr, "[DEBUG] inode_read: read block %u successfully, first 16 bytes: ", block);
            // for (int i = 0; i < 16 && i < BLOCK_SIZE; i++) {
            //     fprintf(stderr, "%02x ", bbuf[i]);
            // }
            // fprintf(stderr, "\n");

            memcpy(dest + total_read, bbuf + block_offset, to_read);
            free(bbuf);
        }

        total_read += to_read;
    }

    // 更新访问时间
    inode->disk.atime = time(NULL);
    inode->dirty = 1;

    // fprintf(stderr, "[DEBUG] inode_read: total_read=%zu\n", total_read);
    return total_read;
}

ssize_t inode_write(inode_cache_t *cache,
                    inode_t_mem *inode,
                    const void *buf,
                    uint64_t offset,
                    size_t size,
                    void *txn) {
    if (!inode || !buf) {
        return MODERNFS_EINVAL;
    }

    size_t total_written = 0;
    const uint8_t *src = (const uint8_t *)buf;

    while (total_written < size) {
        uint64_t cur_offset = offset + total_written;
        uint32_t block_offset = cur_offset % BLOCK_SIZE;
        uint32_t to_write = BLOCK_SIZE - block_offset;

        if (to_write > size - total_written) {
            to_write = size - total_written;
        }

        block_t block;
        int ret = inode_bmap(cache, inode, cur_offset, true, &block);
        if (ret < 0) {
            return ret;
        }

        uint8_t *bbuf = malloc(BLOCK_SIZE);
        if (!bbuf) {
            return MODERNFS_ERROR;
        }

        // 如果不是整块写入，需要先读取
        if (block_offset != 0 || to_write != BLOCK_SIZE) {
            if (blkdev_read(cache->dev, block, bbuf) != 0) {
                free(bbuf);
                return MODERNFS_EIO;
            }
        }

        memcpy(bbuf + block_offset, src + total_written, to_write);

        // Week 7: 如果有Journal事务，记录到Journal；否则直接写入磁盘
        if (txn != NULL) {
            // 调用Rust FFI记录块写入到Journal事务
            extern int rust_journal_write(void *txn, uint32_t block_num, const uint8_t *data);
            ret = rust_journal_write(txn, block, bbuf);
            if (ret < 0) {
                fprintf(stderr, "inode_write: rust_journal_write failed for block %u\n", block);
                free(bbuf);
                return MODERNFS_EIO;
            }
        } else {
            // 无Journal，直接写入磁盘
            if (blkdev_write(cache->dev, block, bbuf) != 0) {
                free(bbuf);
                return MODERNFS_EIO;
            }
        }

        free(bbuf);

        total_written += to_write;
    }

    // 更新文件大小
    if (offset + size > inode->disk.size) {
        inode->disk.size = offset + size;
    }

    // 更新修改时间
    inode->disk.mtime = time(NULL);
    inode->dirty = 1;

    return total_written;
}

// ============ 辅助函数 ============

void inode_stat(inode_t_mem *inode,
                uint64_t *size_out,
                uint64_t *blocks_out,
                uint8_t *type_out) {
    if (!inode) return;

    if (size_out) *size_out = inode->disk.size;
    if (blocks_out) *blocks_out = inode->disk.blocks;
    if (type_out) *type_out = inode->disk.type;
}
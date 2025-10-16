#ifndef MODERNFS_INODE_H
#define MODERNFS_INODE_H

#include "modernfs/types.h"
#include "modernfs/block_dev.h"
#include "modernfs/block_alloc.h"
#include <pthread.h>
#include <sys/types.h>

// ============ 内存中的Inode结构 ============

typedef struct inode {
    inode_t inum;                   // Inode号
    disk_inode_t disk;              // 磁盘Inode数据

    // 缓存管理
    int ref_count;                  // 引用计数
    int valid;                      // 是否已从磁盘读取
    int dirty;                      // 是否需要写回磁盘

    // LRU链表
    struct inode *prev;
    struct inode *next;

    // 哈希链表
    struct inode *hash_next;

    pthread_mutex_t lock;           // Inode锁
} inode_t_mem;

// ============ Inode缓存管理器 ============

typedef struct inode_cache {
    block_device_t *dev;
    block_allocator_t *balloc;

    superblock_t sb;                // 超级块副本

    inode_t_mem *inodes;            // Inode缓存池
    uint32_t max_inodes;            // 最大缓存Inode数

    // LRU链表
    inode_t_mem *lru_head;
    inode_t_mem *lru_tail;

    // 哈希表
    inode_t_mem **hash_table;
    uint32_t hash_size;

    pthread_rwlock_t cache_lock;    // 缓存锁

    // Inode位图操作（需要与block_alloc集成）
    uint8_t *inode_bitmap;          // Inode位图缓存
    uint32_t bitmap_blocks;         // 位图块数
    pthread_mutex_t bitmap_lock;    // 位图锁
} inode_cache_t;

// ============ Inode缓存初始化和销毁 ============

/**
 * 初始化Inode缓存
 * @param dev 块设备
 * @param balloc 块分配器
 * @param max_inodes 最大缓存Inode数
 * @param hash_size 哈希表大小
 * @return 成功返回inode_cache_t指针，失败返回NULL
 */
inode_cache_t *inode_cache_init(block_device_t *dev,
                                block_allocator_t *balloc,
                                uint32_t max_inodes,
                                uint32_t hash_size);

/**
 * 销毁Inode缓存
 * @param cache Inode缓存
 */
void inode_cache_destroy(inode_cache_t *cache);

// ============ Inode分配和释放 ============

/**
 * 分配一个新的Inode
 * @param cache Inode缓存
 * @param type Inode类型 (INODE_TYPE_FILE, INODE_TYPE_DIR, INODE_TYPE_SYMLINK)
 * @return 成功返回Inode指针，失败返回NULL
 */
inode_t_mem *inode_alloc(inode_cache_t *cache, uint8_t type);

/**
 * 释放一个Inode
 * @param cache Inode缓存
 * @param inode Inode指针
 * @return 成功返回0，失败返回负数错误码
 */
int inode_free(inode_cache_t *cache, inode_t_mem *inode);

// ============ Inode获取和释放引用 ============

/**
 * 获取Inode（增加引用计数）
 * @param cache Inode缓存
 * @param inum Inode号
 * @return 成功返回Inode指针，失败返回NULL
 */
inode_t_mem *inode_get(inode_cache_t *cache, inode_t inum);

/**
 * 释放Inode引用（减少引用计数）
 * @param cache Inode缓存
 * @param inode Inode指针
 */
void inode_put(inode_cache_t *cache, inode_t_mem *inode);

/**
 * 加锁Inode
 * @param inode Inode指针
 */
void inode_lock(inode_t_mem *inode);

/**
 * 解锁Inode
 * @param inode Inode指针
 */
void inode_unlock(inode_t_mem *inode);

// ============ Inode读写 ============

/**
 * 同步Inode到磁盘
 * @param cache Inode缓存
 * @param inode Inode指针
 * @return 成功返回0，失败返回负数错误码
 */
int inode_sync(inode_cache_t *cache, inode_t_mem *inode);

/**
 * 同步所有脏Inode到磁盘
 * @param cache Inode缓存
 * @return 成功返回0，失败返回负数错误码
 */
int inode_sync_all(inode_cache_t *cache);

// ============ 数据块映射 ============

/**
 * 将文件内偏移映射到块号
 * @param cache Inode缓存
 * @param inode Inode指针
 * @param offset 文件内偏移（字节）
 * @param alloc_if_missing 如果块不存在是否分配
 * @param block_out 输出块号
 * @return 成功返回0，失败返回负数错误码
 */
int inode_bmap(inode_cache_t *cache,
               inode_t_mem *inode,
               uint64_t offset,
               bool alloc_if_missing,
               block_t *block_out);

/**
 * 截断文件到指定大小
 * @param cache Inode缓存
 * @param inode Inode指针
 * @param new_size 新大小（字节）
 * @return 成功返回0，失败返回负数错误码
 */
int inode_truncate(inode_cache_t *cache, inode_t_mem *inode, uint64_t new_size);

// ============ Inode读写数据 ============

/**
 * 从Inode读取数据
 * @param cache Inode缓存
 * @param inode Inode指针
 * @param buf 缓冲区
 * @param offset 文件内偏移（字节）
 * @param size 读取大小
 * @return 成功返回实际读取字节数，失败返回负数错误码
 */
ssize_t inode_read(inode_cache_t *cache,
                   inode_t_mem *inode,
                   void *buf,
                   uint64_t offset,
                   size_t size);

/**
 * 向Inode写入数据
 * @param cache Inode缓存
 * @param inode Inode指针
 * @param buf 缓冲区
 * @param offset 文件内偏移（字节）
 * @param size 写入大小
 * @param txn Journal事务指针 (可选，传NULL表示不使用Journal)
 * @return 成功返回实际写入字节数，失败返回负数错误码
 */
ssize_t inode_write(inode_cache_t *cache,
                    inode_t_mem *inode,
                    const void *buf,
                    uint64_t offset,
                    size_t size,
                    void *txn);

// ============ 辅助函数 ============

/**
 * 获取Inode统计信息（用于stat系统调用）
 * @param inode Inode指针
 * @param size_out 输出文件大小
 * @param blocks_out 输出块数
 * @param type_out 输出文件类型
 */
void inode_stat(inode_t_mem *inode,
                uint64_t *size_out,
                uint64_t *blocks_out,
                uint8_t *type_out);

#endif // MODERNFS_INODE_H
#ifndef MODERNFS_BLOCK_DEV_H
#define MODERNFS_BLOCK_DEV_H

#include "types.h"

// ============ 块设备结构 ============

struct buffer_cache;

typedef struct block_device {
    int fd;                         // 文件描述符
    uint64_t total_blocks;          // 总块数
    uint64_t total_size;            // 总大小(字节)
    struct buffer_cache *cache;     // 块缓存
    superblock_t *superblock;       // 超级块
} block_device_t;

// ============ 块设备API ============

/**
 * 打开块设备
 * @param path 设备路径
 * @return 成功返回设备结构,失败返回NULL
 */
block_device_t* blkdev_open(const char *path);

/**
 * 关闭块设备
 * @param dev 设备结构
 */
void blkdev_close(block_device_t *dev);

/**
 * 读取块
 * @param dev 设备结构
 * @param block 块号
 * @param buf 缓冲区(必须至少BLOCK_SIZE字节)
 * @return 0成功,负数为错误码
 */
int blkdev_read(block_device_t *dev, block_t block, void *buf);

/**
 * 写入块
 * @param dev 设备结构
 * @param block 块号
 * @param buf 缓冲区(必须BLOCK_SIZE字节)
 * @return 0成功,负数为错误码
 */
int blkdev_write(block_device_t *dev, block_t block, const void *buf);

/**
 * 同步所有脏块到磁盘
 * @param dev 设备结构
 * @return 0成功,负数为错误码
 */
int blkdev_sync(block_device_t *dev);

/**
 * 加载超级块
 * @param dev 设备结构
 * @return 0成功,负数为错误码
 */
int blkdev_load_superblock(block_device_t *dev);

/**
 * 写入超级块
 * @param dev 设备结构
 * @return 0成功,负数为错误码
 */
int blkdev_write_superblock(block_device_t *dev);

#endif // MODERNFS_BLOCK_DEV_H
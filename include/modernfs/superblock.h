#ifndef MODERNFS_SUPERBLOCK_H
#define MODERNFS_SUPERBLOCK_H

#include <stdint.h>
#include "types.h"
#include "block_dev.h"

/**
 * ModernFS Superblock操作
 */

#define MODERNFS_MAGIC 0x4D4F4446  // "MODF" (ModernFS)
#define MODERNFS_VERSION 1

// 文件系统状态常量
#define FS_STATE_CLEAN  0
#define FS_STATE_DIRTY  1
#define FS_STATE_ERROR  2

/**
 * 读取超级块
 */
int superblock_read(block_device_t *dev, superblock_t *sb);

/**
 * 写入超级块
 */
int superblock_write(block_device_t *dev, const superblock_t *sb);

/**
 * 验证超级块有效性
 */
int superblock_validate(const superblock_t *sb);

/**
 * 初始化新的超级块
 */
void superblock_init(superblock_t *sb, uint32_t total_blocks);

#endif // MODERNFS_SUPERBLOCK_H

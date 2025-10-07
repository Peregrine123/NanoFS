#define _GNU_SOURCE
#include "modernfs/superblock.h"
#include "modernfs/block_dev.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <time.h>

int superblock_read(block_device_t *dev, superblock_t *sb) {
    if (!dev || !sb) return -EINVAL;

    // 读取块0
    uint8_t buf[BLOCK_SIZE];
    if (blkdev_read(dev, 0, buf) < 0) {
        fprintf(stderr, "superblock_read: failed to read block 0\n");
        return -EIO;
    }

    memcpy(sb, buf, sizeof(superblock_t));
    return 0;
}

int superblock_write(block_device_t *dev, const superblock_t *sb) {
    if (!dev || !sb) return -EINVAL;

    uint8_t buf[BLOCK_SIZE];
    memset(buf, 0, BLOCK_SIZE);
    memcpy(buf, sb, sizeof(superblock_t));

    if (blkdev_write(dev, 0, buf) < 0) {
        fprintf(stderr, "superblock_write: failed to write block 0\n");
        return -EIO;
    }

    if (blkdev_sync(dev) < 0) {
        fprintf(stderr, "superblock_write: failed to sync\n");
        return -EIO;
    }

    return 0;
}

int superblock_validate(const superblock_t *sb) {
    if (!sb) return -EINVAL;

    // 检查魔数
    if (sb->magic != MODERNFS_MAGIC) {
        fprintf(stderr, "superblock_validate: invalid magic 0x%x (expected 0x%x)\n",
                sb->magic, MODERNFS_MAGIC);
        return -EINVAL;
    }

    // 检查版本
    if (sb->version != MODERNFS_VERSION) {
        fprintf(stderr, "superblock_validate: unsupported version %u\n", sb->version);
        return -EINVAL;
    }

    // 检查块大小
    if (sb->block_size != BLOCK_SIZE) {
        fprintf(stderr, "superblock_validate: invalid block_size %u\n", sb->block_size);
        return -EINVAL;
    }

    // 检查根目录Inode号
    if (sb->root_inum != 1) {
        fprintf(stderr, "superblock_validate: invalid root_inum %u\n", sb->root_inum);
        return -EINVAL;
    }

    return 0;
}

void superblock_init(superblock_t *sb, uint32_t total_blocks) {
    if (!sb) return;

    memset(sb, 0, sizeof(superblock_t));

    // 魔数和版本
    sb->magic = MODERNFS_MAGIC;
    sb->version = MODERNFS_VERSION;

    // 几何信息
    sb->block_size = BLOCK_SIZE;
    sb->total_blocks = total_blocks;

    // 计算Inode数量 (每1024个数据块分配1个Inode)
    uint32_t data_blocks_estimate = total_blocks - 100;  // 预留100块给元数据
    sb->total_inodes = data_blocks_estimate / 1024;
    if (sb->total_inodes < 64) sb->total_inodes = 64;  // 至少64个Inode

    // Inode位图: 每个块可以表示32768个Inode (4096*8)
    sb->inode_bitmap_blocks = (sb->total_inodes + 32767) / 32768;

    // 数据块位图: 每个块可以表示32768个块
    uint32_t data_blocks = total_blocks - 1 - sb->inode_bitmap_blocks;  // 减去superblock和inode位图
    sb->data_bitmap_blocks = (data_blocks + 32767) / 32768;

    // Inode表: 每个块可存储32个Inode (4096/128)
    sb->inode_table_blocks = (sb->total_inodes + 31) / 32;

    // Week 7: 先分配Journal区域 (默认8MB或1/8文件系统大小，取较小值)
    uint32_t journal_size = total_blocks / 8;  // 1/8文件系统大小
    if (journal_size > 2048) journal_size = 2048;  // 最大8MB (2048块)
    if (journal_size < 256) journal_size = 256;    // 最小1MB (256块)

    sb->journal_blocks = journal_size;

    // 重新计算数据块数量 (包含journal)
    uint32_t metadata_blocks = 1 + sb->journal_blocks + sb->inode_bitmap_blocks + sb->data_bitmap_blocks + sb->inode_table_blocks;
    sb->data_blocks = total_blocks - metadata_blocks;
    sb->data_bitmap_blocks = (sb->data_blocks + 32767) / 32768;  // 重新调整位图大小

    // 最终计算数据块 (包含journal)
    metadata_blocks = 1 + sb->journal_blocks + sb->inode_bitmap_blocks + sb->data_bitmap_blocks + sb->inode_table_blocks;
    sb->data_blocks = total_blocks - metadata_blocks;

    // 区域布局
    uint32_t current_block = 1;  // 块0是superblock

    // Week 7: Journal放在最前面
    sb->journal_start = current_block;
    current_block += sb->journal_blocks;

    sb->inode_bitmap_start = current_block;
    current_block += sb->inode_bitmap_blocks;

    sb->data_bitmap_start = current_block;
    current_block += sb->data_bitmap_blocks;

    sb->inode_table_start = current_block;
    current_block += sb->inode_table_blocks;

    sb->data_start = current_block;

    // Inode信息
    sb->free_inodes = sb->total_inodes - 1;  // 减去根目录
    sb->first_inode = 2;  // 0保留，1是根目录

    // 块统计
    sb->free_blocks = sb->data_blocks - 1;  // 减去根目录占用的1个块

    // 文件系统状态
    sb->state = FS_STATE_CLEAN;
    sb->mount_time = time(NULL);
    sb->write_time = time(NULL);
    sb->mount_count = 0;

    // 根目录
    sb->root_inum = 1;

    printf("Superblock initialized:\n");
    printf("  Total blocks: %u\n", sb->total_blocks);
    printf("  Data blocks: %u\n", sb->data_blocks);
    printf("  Total inodes: %u\n", sb->total_inodes);
    printf("  Journal: blocks %u-%u (%u blocks)\n",
           sb->journal_start, sb->journal_start + sb->journal_blocks - 1,
           sb->journal_blocks);
    printf("  Inode bitmap: blocks %u-%u (%u blocks)\n",
           sb->inode_bitmap_start, sb->inode_bitmap_start + sb->inode_bitmap_blocks - 1,
           sb->inode_bitmap_blocks);
    printf("  Data bitmap: blocks %u-%u (%u blocks)\n",
           sb->data_bitmap_start, sb->data_bitmap_start + sb->data_bitmap_blocks - 1,
           sb->data_bitmap_blocks);
    printf("  Inode table: blocks %u-%u (%u blocks)\n",
           sb->inode_table_start, sb->inode_table_start + sb->inode_table_blocks - 1,
           sb->inode_table_blocks);
    printf("  Data area: blocks %u-%u (%u blocks)\n",
           sb->data_start, sb->data_start + sb->data_blocks - 1,
           sb->data_blocks);
}
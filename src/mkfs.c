#define _GNU_SOURCE
#include "modernfs/types.h"
#include "modernfs/block_dev.h"
#include "modernfs/superblock.h"
#include "modernfs/inode.h"
#include "modernfs/directory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>

/**
 * mkfs.modernfs - ModernFS文件系统格式化工具
 *
 * 用法: mkfs.modernfs <device> <size_in_mb>
 *
 * 例如: mkfs.modernfs /tmp/test.img 100
 */
// 创建磁盘镜像文件.img, 扩展到指定大小(128M)
static int create_disk_image(const char *path, uint64_t size_bytes) {
    int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        fprintf(stderr, "Failed to create %s: %s\n", path, strerror(errno));
        return -1;
    }

    // 扩展文件到指定大小
    if (ftruncate(fd, size_bytes) < 0) {
        fprintf(stderr, "Failed to resize %s: %s\n", path, strerror(errno));
        close(fd);
        return -1;
    }

    close(fd);
    return 0;
}

static int init_inode_bitmap(block_device_t *dev, superblock_t *sb) {
    uint8_t *bitmap = calloc(1, BLOCK_SIZE);
    if (!bitmap) return -ENOMEM;

    // 标记Inode 0为已使用 (保留)
    bitmap[0] |= 0x01;

    // 标记Inode 1为已使用 (根目录)
    bitmap[0] |= 0x02;

    // 写入位图块
    for (uint32_t i = 0; i < sb->inode_bitmap_blocks; i++) {
        if (blkdev_write(dev, sb->inode_bitmap_start + i, bitmap) < 0) {
            free(bitmap);
            return -EIO;
        }
        // 只有第一块需要标记已使用位
        memset(bitmap, 0, BLOCK_SIZE);
    }

    free(bitmap);
    return 0;
}

static int init_data_bitmap(block_device_t *dev, superblock_t *sb) {
    uint8_t *bitmap = calloc(1, BLOCK_SIZE);
    if (!bitmap) return -ENOMEM;

    // 标记块0为已使用 (根目录的数据块)
    bitmap[0] |= 0x01;

    // 写入位图块
    for (uint32_t i = 0; i < sb->data_bitmap_blocks; i++) {
        if (blkdev_write(dev, sb->data_bitmap_start + i, bitmap) < 0) {
            free(bitmap);
            return -EIO;
        }
        // 只有第一块需要标记已使用位
        memset(bitmap, 0, BLOCK_SIZE);
    }

    free(bitmap);
    return 0;
}

static int init_inode_table(block_device_t *dev, superblock_t *sb) {
    uint8_t *block = calloc(1, BLOCK_SIZE);
    if (!block) return -ENOMEM;

    // 清零所有Inode表块
    for (uint32_t i = 0; i < sb->inode_table_blocks; i++) {
        if (blkdev_write(dev, sb->inode_table_start + i, block) < 0) {
            free(block);
            return -EIO;
        }
    }

    // 创建根目录Inode (Inode #1)
    disk_inode_t root;
    memset(&root, 0, sizeof(disk_inode_t));
    root.type = INODE_TYPE_DIR;
    root.mode = 0755;
    root.nlink = 2;  // . 和 ..
    root.uid = 0;
    root.gid = 0;
    root.size = BLOCK_SIZE;  // 一个数据块
    root.blocks = 1;
    root.atime = time(NULL);
    root.mtime = root.atime;
    root.ctime = root.atime;
    root.direct[0] = sb->data_start;  // 第一个数据块（物理块号）
    // 其余块号已通过 memset 初始化为 0，无需再设置

    // 写入根目录Inode
    // Inode 1在Inode表中的位置: 块 inode_table_start, 偏移 1*128
    memset(block, 0, BLOCK_SIZE);
    memcpy(block + sizeof(disk_inode_t), &root, sizeof(disk_inode_t));  // Inode 0保留，Inode 1在偏移128处

    if (blkdev_write(dev, sb->inode_table_start, block) < 0) {
        free(block);
        return -EIO;
    }

    free(block);
    return 0;
}

static int init_root_directory(block_device_t *dev, superblock_t *sb) {
    uint8_t *block = calloc(1, BLOCK_SIZE);
    if (!block) return -ENOMEM;

    // 创建 "." 目录项
    dirent_t *dot = (dirent_t *)block;
    dot->inum = 1;
    dot->name_len = 1;
    strcpy(dot->name, ".");
    uint16_t dot_size = sizeof(dirent_t) + 1;
    dot_size = (dot_size + 7) & ~7;  // 8字节对齐
    dot->rec_len = dot_size;

    // 创建 ".." 目录项
    dirent_t *dotdot = (dirent_t *)(block + dot_size);
    dotdot->inum = 1;  // 根目录的父目录是自己
    dotdot->name_len = 2;
    strcpy(dotdot->name, "..");
    uint16_t dotdot_size = sizeof(dirent_t) + 2;
    dotdot_size = (dotdot_size + 7) & ~7;
    dotdot->rec_len = BLOCK_SIZE - dot_size;  // 最后一项占满剩余空间

    // 写入根目录数据块
    if (blkdev_write(dev, sb->data_start, block) < 0) {
        free(block);
        return -EIO;
    }

    free(block);
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <device> <size_in_mb>\n", argv[0]);
        fprintf(stderr, "Example: %s /tmp/test.img 100\n", argv[0]);
        return 1;
    }

    const char *device_path = argv[1];
    uint32_t size_mb = atoi(argv[2]);

    if (size_mb < 1 || size_mb > 16384) {
        fprintf(stderr, "Error: size must be between 1 and 16384 MB\n");
        return 1;
    }

    uint64_t size_bytes = (uint64_t)size_mb * 1024 * 1024;
    uint32_t total_blocks = size_bytes / BLOCK_SIZE;

    printf("╔════════════════════════════════════════╗\n");
    printf("║  mkfs.modernfs - ModernFS Formatter    ║\n");
    printf("╚════════════════════════════════════════╝\n\n");

    printf("Creating disk image: %s\n", device_path);
    printf("Size: %u MB (%lu bytes, %u blocks)\n\n", size_mb, size_bytes, total_blocks);

    // 1. 创建磁盘镜像文件
    printf("[1/6] Creating disk image...\n");
    if (create_disk_image(device_path, size_bytes) < 0) {
        return 1;
    }

    // 2. 打开块设备
    printf("[2/6] Opening block device...\n");
    block_device_t *dev = blkdev_open(device_path);
    if (!dev) {
        fprintf(stderr, "Failed to open device\n");
        return 1;
    }

    // 3. 初始化超级块
    printf("[3/6] Initializing superblock...\n");
    superblock_t sb;
    superblock_init(&sb, total_blocks);

    if (superblock_write(dev, &sb) < 0) {
        fprintf(stderr, "Failed to write superblock\n");
        blkdev_close(dev);
        return 1;
    }

    // 4. 初始化Journal区域 (Week 7)
    printf("[4/7] Initializing journal...\n");
    uint8_t *journal_block = calloc(1, BLOCK_SIZE);
    if (!journal_block) {
        fprintf(stderr, "Failed to allocate memory for journal\n");
        blkdev_close(dev);
        return 1;
    }

    // 写入journal superblock (第一个块)
    uint32_t *journal_sb = (uint32_t *)journal_block;
    journal_sb[0] = 0x4A524E4C;  // Magic "JRNL"
    journal_sb[1] = 1;            // Version 1
    journal_sb[2] = BLOCK_SIZE;   // Block size
    journal_sb[3] = sb.journal_blocks;  // Total blocks
    // sequence初始化为0
    uint64_t *sequence = (uint64_t *)&journal_sb[4];
    *sequence = 0;
    // head和tail初始化为1 (块0是superblock,数据从块1开始)
    journal_sb[6] = 1;  // head = 1
    journal_sb[7] = 1;  // tail = 1

    if (blkdev_write(dev, sb.journal_start, journal_block) < 0) {
        fprintf(stderr, "Failed to write journal superblock\n");
        free(journal_block);
        blkdev_close(dev);
        return 1;
    }

    // 清零剩余journal区域
    memset(journal_block, 0, BLOCK_SIZE);
    for (uint32_t i = 1; i < sb.journal_blocks; i++) {
        if (blkdev_write(dev, sb.journal_start + i, journal_block) < 0) {
            fprintf(stderr, "Failed to init journal block %u\n", i);
            free(journal_block);
            blkdev_close(dev);
            return 1;
        }
    }
    free(journal_block);
    printf("  Journal initialized: %u blocks (with superblock)\n", sb.journal_blocks);

    // 5. 初始化Inode位图
    printf("[5/7] Initializing inode bitmap...\n");
    if (init_inode_bitmap(dev, &sb) < 0) {
        fprintf(stderr, "Failed to init inode bitmap\n");
        blkdev_close(dev);
        return 1;
    }

    // 6. 初始化数据块位图
    printf("[6/7] Initializing data bitmap...\n");
    if (init_data_bitmap(dev, &sb) < 0) {
        fprintf(stderr, "Failed to init data bitmap\n");
        blkdev_close(dev);
        return 1;
    }

    // 7. 初始化Inode表和根目录
    printf("[7/7] Creating root directory...\n");
    if (init_inode_table(dev, &sb) < 0) {
        fprintf(stderr, "Failed to init inode table\n");
        blkdev_close(dev);
        return 1;
    }

    if (init_root_directory(dev, &sb) < 0) {
        fprintf(stderr, "Failed to init root directory\n");
        blkdev_close(dev);
        return 1;
    }

    // 同步到磁盘
    blkdev_sync(dev);
    blkdev_close(dev);

    printf("\n╔════════════════════════════════════════╗\n");
    printf("║  ✅ Filesystem created successfully!   ║\n");
    printf("╚════════════════════════════════════════╝\n");
    printf("\nYou can now mount it with:\n");
    printf("  mkdir /tmp/mnt\n");
    printf("  ./modernfs %s /tmp/mnt -f\n", device_path);

    return 0;
}
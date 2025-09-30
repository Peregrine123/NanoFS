#include "modernfs/types.h"
#include "modernfs/block_dev.h"
#include "modernfs/block_alloc.h"
#include "modernfs/inode.h"
#include "modernfs/directory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define TEST_IMG "test_dir.img"
#define IMG_SIZE (16 * 1024 * 1024)

int main() {
    printf("简化目录测试\n\n");

    // 创建测试磁盘
    FILE *f = fopen(TEST_IMG, "wb");
    fseek(f, IMG_SIZE - 1, SEEK_SET);
    fputc(0, f);
    fclose(f);

    block_device_t *dev = blkdev_open(TEST_IMG);
    assert(dev);

    // 创建超级块
    uint8_t *sb_buf = calloc(1, BLOCK_SIZE);
    superblock_t *sb = (superblock_t *)sb_buf;
    sb->magic = SUPERBLOCK_MAGIC;
    sb->block_size = BLOCK_SIZE;
    sb->total_blocks = IMG_SIZE / BLOCK_SIZE;
    sb->inode_bitmap_start = 1;
    sb->inode_bitmap_blocks = 1;
    sb->data_bitmap_start = 2;
    sb->data_bitmap_blocks = 1;
    sb->inode_table_start = 3;
    sb->total_inodes = 64;
    sb->inode_table_blocks = (64 * INODE_SIZE + BLOCK_SIZE - 1) / BLOCK_SIZE;
    sb->data_start = sb->inode_table_start + sb->inode_table_blocks;
    sb->data_blocks = sb->total_blocks - sb->data_start;
    sb->free_inodes = 64;
    sb->free_blocks = sb->data_blocks;

    blkdev_write(dev, 0, sb_buf);
    free(sb_buf);

    // 初始化位图
    uint8_t *bitmap = calloc(1, BLOCK_SIZE);
    blkdev_write(dev, 1, bitmap);
    blkdev_write(dev, 2, bitmap);
    free(bitmap);

    // 初始化Inode表
    uint8_t *inode_buf = calloc(1, BLOCK_SIZE);
    for (uint32_t i = 0; i < sb->inode_table_blocks; i++) {
        blkdev_write(dev, sb->inode_table_start + i, inode_buf);
    }
    free(inode_buf);

    blkdev_sync(dev);

    // 读取超级块
    uint8_t *sb_read = malloc(BLOCK_SIZE);
    blkdev_read(dev, 0, sb_read);
    superblock_t *sb2 = (superblock_t *)sb_read;

    block_allocator_t *balloc = block_alloc_init(dev, sb2->data_bitmap_start,
                                                  sb2->data_bitmap_blocks,
                                                  sb2->data_start,
                                                  sb2->data_blocks);
    free(sb_read);

    inode_cache_t *icache = inode_cache_init(dev, balloc, 32, 16);
    assert(icache);

    printf("✅ 环境初始化完成\n\n");

    // 测试1: 分配Inode
    printf("1. 分配目录和文件Inode\n");
    inode_t_mem *dir = inode_alloc(icache, INODE_TYPE_DIR);
    assert(dir);
    printf("  目录Inode号: %u\n", dir->inum);

    inode_t_mem *file = inode_alloc(icache, INODE_TYPE_FILE);
    assert(file);
    printf("  文件Inode号: %u\n\n", file->inum);

    // 测试2: 添加目录项
    printf("2. 添加目录项\n");
    printf("  目录初始大小: %lu\n", dir->disk.size);

    int ret = dir_add(icache, dir, "test.txt", file->inum);
    printf("  dir_add返回: %d\n", ret);
    printf("  目录写入后大小: %lu\n", dir->disk.size);
    printf("  目录块数: %lu\n", dir->disk.blocks);

    if (ret != MODERNFS_SUCCESS) {
        printf("  ❌ 添加失败\n");
        return 1;
    }
    printf("  ✅ 添加成功\n\n");

    // 测试3: 查找目录项
    printf("3. 查找目录项\n");
    inode_t found;
    ret = dir_lookup(icache, dir, "test.txt", &found);
    printf("  dir_lookup返回: %d\n", ret);

    if (ret == MODERNFS_SUCCESS) {
        printf("  找到Inode: %u\n", found);
        if (found == file->inum) {
            printf("  ✅ 查找成功\n");
        } else {
            printf("  ❌ Inode不匹配\n");
        }
    } else {
        printf("  ❌ 查找失败\n");
    }

    // 清理
    inode_cache_destroy(icache);
    block_alloc_destroy(balloc);
    blkdev_close(dev);
    remove(TEST_IMG);

    printf("\n✅ 测试完成\n");
    return 0;
}
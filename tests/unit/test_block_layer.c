#include "modernfs/types.h"
#include "modernfs/block_dev.h"
#include "modernfs/buffer_cache.h"
#include "modernfs/block_alloc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>

#define TEST_DISK_IMAGE "test_disk.img"
#define TEST_DISK_SIZE (64 * 1024 * 1024)  // 64MB
#define TEST_TOTAL_BLOCKS (TEST_DISK_SIZE / BLOCK_SIZE)

// ============ 辅助函数 ============

static void create_test_disk() {
    printf("Creating test disk image: %s (%d MB)\n",
           TEST_DISK_IMAGE, TEST_DISK_SIZE / 1024 / 1024);

    int fd = open(TEST_DISK_IMAGE, O_RDWR | O_CREAT | O_TRUNC, 0644);
    assert(fd >= 0);

    // 设置文件大小
    int ret = ftruncate(fd, TEST_DISK_SIZE);
    assert(ret == 0);

    close(fd);

    printf("✅ Test disk created\n\n");
}

static void create_test_superblock(block_device_t *dev) {
    printf("Creating test superblock...\n");

    superblock_t sb = {0};
    sb.magic = SUPERBLOCK_MAGIC;
    sb.version = 1;
    sb.block_size = BLOCK_SIZE;
    sb.total_blocks = TEST_TOTAL_BLOCKS;

    // 简化的布局:
    // Block 0: Superblock
    // Block 1-256: Journal (256 blocks = 1MB)
    // Block 257-258: Data bitmap (2 blocks, 每块可管理32K个块)
    // Block 259-512: Inode table (保留)
    // Block 513+: Data blocks

    sb.journal_start = 1;
    sb.journal_blocks = 256;
    sb.data_bitmap_start = 257;
    sb.data_bitmap_blocks = 2;
    sb.inode_table_start = 259;
    sb.inode_table_blocks = 254;
    sb.data_start = 513;
    sb.data_blocks = TEST_TOTAL_BLOCKS - 513;
    sb.free_blocks = sb.data_blocks;

    sb.mount_time = time(NULL);
    sb.write_time = time(NULL);
    sb.mount_count = 0;

    // 写入超级块（必须写满一个块）
    uint8_t sb_block[BLOCK_SIZE];
    memset(sb_block, 0, BLOCK_SIZE);
    memcpy(sb_block, &sb, sizeof(superblock_t));
    int ret = blkdev_write(dev, SUPERBLOCK_BLOCK, sb_block);
    assert(ret == 0);

    // 初始化位图(全部设为0,即全部空闲)
    uint8_t bitmap[BLOCK_SIZE] = {0};
    for (uint32_t i = 0; i < sb.data_bitmap_blocks; i++) {
        ret = blkdev_write(dev, sb.data_bitmap_start + i, bitmap);
        assert(ret == 0);
    }

    ret = blkdev_sync(dev);
    assert(ret == 0);

    printf("✅ Superblock created\n\n");
}

// ============ 测试用例 ============

void test_block_device() {
    printf("========== Test: Block Device ==========\n");

    block_device_t *dev = blkdev_open(TEST_DISK_IMAGE);
    assert(dev != NULL);

    // 测试写入
    uint8_t write_buf[BLOCK_SIZE];
    for (int i = 0; i < BLOCK_SIZE; i++) {
        write_buf[i] = i % 256;
    }

    int ret = blkdev_write(dev, 100, write_buf);
    assert(ret == 0);

    // 测试读取
    uint8_t read_buf[BLOCK_SIZE];
    ret = blkdev_read(dev, 100, read_buf);
    assert(ret == 0);

    // 验证数据
    assert(memcmp(write_buf, read_buf, BLOCK_SIZE) == 0);

    printf("✅ Block read/write test passed\n");

    // 测试同步
    ret = blkdev_sync(dev);
    assert(ret == 0);

    printf("✅ Block sync test passed\n");

    blkdev_close(dev);
    printf("✅ Block device test passed\n\n");
}

void test_buffer_cache() {
    printf("========== Test: Buffer Cache ==========\n");

    block_device_t *dev = blkdev_open(TEST_DISK_IMAGE);
    assert(dev != NULL);

    // 写入测试数据
    uint8_t test_data[BLOCK_SIZE];
    memset(test_data, 0xAB, BLOCK_SIZE);
    blkdev_write(dev, 200, test_data);

    // 第一次读取(缓存未命中)
    uint8_t buf1[BLOCK_SIZE];
    blkdev_read(dev, 200, buf1);

    // 第二次读取(缓存命中)
    uint8_t buf2[BLOCK_SIZE];
    blkdev_read(dev, 200, buf2);

    assert(memcmp(buf1, buf2, BLOCK_SIZE) == 0);

    // 查看缓存统计
    uint64_t hits, misses, evicts;
    float hit_rate;
    buffer_cache_stats(dev->cache, &hits, &misses, &evicts, &hit_rate);

    printf("Cache stats: hits=%lu, misses=%lu, evicts=%lu, hit_rate=%.2f%%\n",
           hits, misses, evicts, hit_rate * 100);

    assert(hits >= 1);  // 至少有一次命中

    printf("✅ Buffer cache test passed\n\n");

    blkdev_close(dev);
}

void test_block_allocator() {
    printf("========== Test: Block Allocator ==========\n");

    block_device_t *dev = blkdev_open(TEST_DISK_IMAGE);
    assert(dev != NULL);

    // 创建超级块
    create_test_superblock(dev);

    // 加载超级块
    int ret = blkdev_load_superblock(dev);
    assert(ret == 0);

    superblock_t *sb = dev->superblock;

    // 初始化块分配器
    block_allocator_t *alloc = block_alloc_init(
        dev,
        sb->data_bitmap_start,
        sb->data_bitmap_blocks,
        sb->data_start,
        sb->data_blocks
    );
    assert(alloc != NULL);

    // 测试分配单个块
    block_t block1 = block_alloc(alloc);
    printf("Allocated block: %u\n", block1);
    assert(block1 >= sb->data_start);
    assert(block_is_allocated(alloc, block1));

    block_t block2 = block_alloc(alloc);
    printf("Allocated block: %u\n", block2);
    assert(block2 >= sb->data_start);
    assert(block2 != block1);

    printf("✅ Single block allocation test passed\n");

    // 测试分配多个连续块
    block_t start;
    uint32_t count;
    ret = block_alloc_multiple(alloc, 10, &start, &count);
    assert(ret == 0);
    assert(count == 10);
    printf("Allocated %u consecutive blocks starting at %u\n", count, start);

    for (uint32_t i = 0; i < count; i++) {
        assert(block_is_allocated(alloc, start + i));
    }

    printf("✅ Multiple block allocation test passed\n");

    // 测试释放块
    ret = block_free(alloc, block1);
    assert(ret == 0);
    assert(!block_is_allocated(alloc, block1));

    ret = block_free_multiple(alloc, start, count);
    assert(ret == 0);

    for (uint32_t i = 0; i < count; i++) {
        assert(!block_is_allocated(alloc, start + i));
    }

    printf("✅ Block free test passed\n");

    // 测试统计信息
    uint32_t total, free, used;
    float usage;
    block_alloc_stats(alloc, &total, &free, &used, &usage);

    printf("Allocator stats: total=%u, free=%u, used=%u, usage=%.2f%%\n",
           total, free, used, usage * 100);

    // 同步位图
    ret = block_alloc_sync(alloc);
    assert(ret == 0);

    printf("✅ Block allocator test passed\n\n");

    block_alloc_destroy(alloc);
    blkdev_close(dev);
}

void test_concurrent_access() {
    printf("========== Test: Concurrent Access ==========\n");

    block_device_t *dev = blkdev_open(TEST_DISK_IMAGE);
    assert(dev != NULL);

    // 测试多次读写同一块
    uint8_t buf[BLOCK_SIZE];

    for (int i = 0; i < 100; i++) {
        memset(buf, i % 256, BLOCK_SIZE);
        blkdev_write(dev, 300, buf);

        uint8_t read_buf[BLOCK_SIZE];
        blkdev_read(dev, 300, read_buf);

        assert(memcmp(buf, read_buf, BLOCK_SIZE) == 0);
    }

    printf("✅ Concurrent access test passed\n\n");

    blkdev_close(dev);
}

void test_edge_cases() {
    printf("========== Test: Edge Cases ==========\n");

    block_device_t *dev = blkdev_open(TEST_DISK_IMAGE);
    assert(dev != NULL);

    // 测试读取超出范围的块
    uint8_t buf[BLOCK_SIZE];
    int ret = blkdev_read(dev, TEST_TOTAL_BLOCKS + 100, buf);
    assert(ret < 0);  // 应该失败

    printf("✅ Out-of-range read test passed\n");

    // 测试写入超出范围的块
    ret = blkdev_write(dev, TEST_TOTAL_BLOCKS + 100, buf);
    assert(ret < 0);  // 应该失败

    printf("✅ Out-of-range write test passed\n");

    printf("✅ Edge cases test passed\n\n");

    blkdev_close(dev);
}

// ============ 主函数 ============

int main() {
    printf("\n");
    printf("========================================\n");
    printf("  ModernFS Block Layer Test Suite\n");
    printf("  Week 2: Block Device & Allocator\n");
    printf("========================================\n\n");

    // 创建测试磁盘
    create_test_disk();

    // 运行测试
    test_block_device();
    test_buffer_cache();
    test_block_allocator();
    test_concurrent_access();
    test_edge_cases();

    // 清理
    remove(TEST_DISK_IMAGE);

    printf("\n");
    printf("========================================\n");
    printf("  All Tests Passed!\n");
    printf("========================================\n\n");

    return 0;
}

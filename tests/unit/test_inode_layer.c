#include "modernfs/types.h"
#include "modernfs/block_dev.h"
#include "modernfs/block_alloc.h"
#include "modernfs/inode.h"
#include "modernfs/directory.h"
#include "modernfs/path.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>

#define TEST_DISK_IMAGE "test_inode_disk.img"
#define TEST_DISK_SIZE (64 * 1024 * 1024)  // 64MB
#define TEST_TOTAL_BLOCKS (TEST_DISK_SIZE / BLOCK_SIZE)

// ============ 全局变量 ============

static block_device_t *g_dev = NULL;
static block_allocator_t *g_balloc = NULL;
static inode_cache_t *g_icache = NULL;

// ============ 辅助函数 ============

static void create_test_disk() {
    printf("创建测试磁盘镜像: %s (%d MB)\n",
           TEST_DISK_IMAGE, TEST_DISK_SIZE / 1024 / 1024);

    FILE *f = fopen(TEST_DISK_IMAGE, "wb");
    assert(f != NULL);

    fseek(f, TEST_DISK_SIZE - 1, SEEK_SET);
    fputc(0, f);
    fclose(f);

    printf("✅ 测试磁盘创建成功\n\n");
}

static void create_test_superblock(block_device_t *dev) {
    printf("创建测试超级块...\n");

    superblock_t sb = {0};
    sb.magic = SUPERBLOCK_MAGIC;
    sb.version = 1;
    sb.block_size = BLOCK_SIZE;
    sb.total_blocks = TEST_TOTAL_BLOCKS;

    // 布局:
    // Block 0: Superblock
    // Block 1-256: Journal
    // Block 257-258: Inode bitmap
    // Block 259-260: Data bitmap
    // Block 261-1284: Inode table (1024 inodes, 128 bytes each)
    // Block 1285+: Data blocks

    sb.journal_start = 1;
    sb.journal_blocks = 256;

    sb.inode_bitmap_start = 257;
    sb.inode_bitmap_blocks = 2;

    sb.data_bitmap_start = 259;
    sb.data_bitmap_blocks = 2;

    sb.inode_table_start = 261;
    sb.total_inodes = 1024;
    sb.inode_table_blocks = (sb.total_inodes * INODE_SIZE + BLOCK_SIZE - 1) / BLOCK_SIZE;

    sb.data_start = sb.inode_table_start + sb.inode_table_blocks;
    sb.data_blocks = TEST_TOTAL_BLOCKS - sb.data_start;

    sb.free_blocks = sb.data_blocks;
    sb.free_inodes = sb.total_inodes;

    sb.mount_time = time(NULL);
    sb.write_time = sb.mount_time;
    sb.mount_count = 0;

    // 写入超级块
    uint8_t *buf = malloc(BLOCK_SIZE);
    memset(buf, 0, BLOCK_SIZE);
    memcpy(buf, &sb, sizeof(superblock_t));

    int ret = blkdev_write(dev, SUPERBLOCK_BLOCK, buf);
    assert(ret == 0);

    free(buf);

    // 初始化Inode位图（全部空闲）
    uint8_t *bitmap = calloc(sb.inode_bitmap_blocks, BLOCK_SIZE);
    for (uint32_t i = 0; i < sb.inode_bitmap_blocks; i++) {
        ret = blkdev_write(dev, sb.inode_bitmap_start + i, bitmap + i * BLOCK_SIZE);
        assert(ret == 0);
    }
    free(bitmap);

    // 初始化数据位图（全部空闲）
    bitmap = calloc(sb.data_bitmap_blocks, BLOCK_SIZE);
    for (uint32_t i = 0; i < sb.data_bitmap_blocks; i++) {
        ret = blkdev_write(dev, sb.data_bitmap_start + i, bitmap + i * BLOCK_SIZE);
        assert(ret == 0);
    }
    free(bitmap);

    // 初始化Inode表
    uint8_t *inode_block = calloc(1, BLOCK_SIZE);
    for (uint32_t i = 0; i < sb.inode_table_blocks; i++) {
        ret = blkdev_write(dev, sb.inode_table_start + i, inode_block);
        assert(ret == 0);
    }
    free(inode_block);

    blkdev_sync(dev);

    printf("✅ 超级块创建成功\n");
    printf("  总块数: %u\n", sb.total_blocks);
    printf("  总Inode数: %u\n", sb.total_inodes);
    printf("  数据区起始块: %u\n", sb.data_start);
    printf("  数据块数: %u\n\n", sb.data_blocks);
}

static void setup_test_env() {
    printf("========================================\n");
    printf("初始化测试环境\n");
    printf("========================================\n\n");

    create_test_disk();

    g_dev = blkdev_open(TEST_DISK_IMAGE);
    assert(g_dev != NULL);

    create_test_superblock(g_dev);

    // 读取超级块以获取位图信息
    uint8_t *sb_buf = malloc(BLOCK_SIZE);
    assert(blkdev_read(g_dev, SUPERBLOCK_BLOCK, sb_buf) == 0);
    superblock_t *sb = (superblock_t *)sb_buf;

    g_balloc = block_alloc_init(g_dev, sb->data_bitmap_start,
                                sb->data_bitmap_blocks, sb->data_start,
                                sb->data_blocks);
    assert(g_balloc != NULL);

    g_icache = inode_cache_init(g_dev, g_balloc, 64, 32);
    assert(g_icache != NULL);

    free(sb_buf);

    printf("✅ 测试环境初始化完成\n\n");
}

static void teardown_test_env() {
    printf("\n========================================\n");
    printf("清理测试环境\n");
    printf("========================================\n\n");

    if (g_icache) {
        inode_cache_destroy(g_icache);
        g_icache = NULL;
    }

    if (g_balloc) {
        block_alloc_destroy(g_balloc);
        g_balloc = NULL;
    }

    if (g_dev) {
        blkdev_close(g_dev);
        g_dev = NULL;
    }

    remove(TEST_DISK_IMAGE);

    printf("✅ 测试环境清理完成\n\n");
}

// ============ 测试用例 ============

static void test_inode_alloc_free() {
    printf("========================================\n");
    printf("测试1: Inode分配和释放\n");
    printf("========================================\n\n");

    printf("1. 分配文件Inode\n");
    inode_t_mem *file_inode = inode_alloc(g_icache, INODE_TYPE_FILE);
    assert(file_inode != NULL);
    printf("  分配成功，Inode号: %u\n", file_inode->inum);
    assert(file_inode->disk.type == INODE_TYPE_FILE);
    assert(file_inode->disk.nlink == 1);

    printf("2. 分配目录Inode\n");
    inode_t_mem *dir_inode = inode_alloc(g_icache, INODE_TYPE_DIR);
    assert(dir_inode != NULL);
    printf("  分配成功，Inode号: %u\n", dir_inode->inum);
    assert(dir_inode->disk.type == INODE_TYPE_DIR);

    printf("3. 同步Inode到磁盘\n");
    int ret = inode_sync(g_icache, file_inode);
    assert(ret == MODERNFS_SUCCESS);
    ret = inode_sync(g_icache, dir_inode);
    assert(ret == MODERNFS_SUCCESS);
    printf("  同步成功\n");

    printf("4. 释放Inode\n");
    ret = inode_free(g_icache, file_inode);
    assert(ret == MODERNFS_SUCCESS);
    ret = inode_free(g_icache, dir_inode);
    assert(ret == MODERNFS_SUCCESS);
    printf("  释放成功\n");

    printf("\n✅ 测试1通过\n\n");
}

static void test_inode_read_write() {
    printf("========================================\n");
    printf("测试2: Inode读写\n");
    printf("========================================\n\n");

    printf("1. 分配文件Inode\n");
    inode_t_mem *inode = inode_alloc(g_icache, INODE_TYPE_FILE);
    assert(inode != NULL);

    printf("2. 写入小数据（单块）\n");
    const char *data1 = "Hello, ModernFS!";
    size_t len1 = strlen(data1);
    ssize_t written = inode_write(g_icache, inode, data1, 0, len1, NULL);
    assert(written == (ssize_t)len1);
    printf("  写入 %zd 字节\n", written);

    printf("3. 读取数据\n");
    char buf1[256] = {0};
    ssize_t read = inode_read(g_icache, inode, buf1, 0, len1);
    assert(read == (ssize_t)len1);
    assert(memcmp(buf1, data1, len1) == 0);
    printf("  读取 %zd 字节，内容匹配\n", read);

    printf("4. 写入大数据（跨块）\n");
    char *large_data = malloc(BLOCK_SIZE * 3);
    for (size_t i = 0; i < BLOCK_SIZE * 3; i++) {
        large_data[i] = 'A' + (i % 26);
    }
    written = inode_write(g_icache, inode, large_data, 0, BLOCK_SIZE * 3, NULL);
    assert(written == BLOCK_SIZE * 3);
    printf("  写入 %zd 字节 (3个块)\n", written);

    printf("5. 读取大数据\n");
    char *read_buf = malloc(BLOCK_SIZE * 3);
    read = inode_read(g_icache, inode, read_buf, 0, BLOCK_SIZE * 3);
    assert(read == BLOCK_SIZE * 3);
    assert(memcmp(read_buf, large_data, BLOCK_SIZE * 3) == 0);
    printf("  读取 %zd 字节，内容匹配\n", read);

    free(large_data);
    free(read_buf);

    printf("6. 清理\n");
    inode_free(g_icache, inode);

    printf("\n✅ 测试2通过\n\n");
}

static void test_directory_ops() {
    printf("========================================\n");
    printf("测试3: 目录操作\n");
    printf("========================================\n\n");

    printf("1. 分配目录Inode\n");
    inode_t_mem *dir = inode_alloc(g_icache, INODE_TYPE_DIR);
    assert(dir != NULL);

    printf("2. 分配文件Inode\n");
    inode_t_mem *file1 = inode_alloc(g_icache, INODE_TYPE_FILE);
    inode_t_mem *file2 = inode_alloc(g_icache, INODE_TYPE_FILE);
    inode_t_mem *file3 = inode_alloc(g_icache, INODE_TYPE_FILE);
    assert(file1 && file2 && file3);

    printf("3. 添加目录项\n");
    int ret = dir_add(g_icache, dir, "file1.txt", file1->inum);
    assert(ret == MODERNFS_SUCCESS);
    ret = dir_add(g_icache, dir, "file2.txt", file2->inum);
    assert(ret == MODERNFS_SUCCESS);
    ret = dir_add(g_icache, dir, "file3.txt", file3->inum);
    assert(ret == MODERNFS_SUCCESS);
    printf("  添加了3个文件\n");

    printf("4. 查找目录项\n");
    inode_t found_inum;
    ret = dir_lookup(g_icache, dir, "file1.txt", &found_inum);
    assert(ret == MODERNFS_SUCCESS);
    assert(found_inum == file1->inum);
    ret = dir_lookup(g_icache, dir, "file2.txt", &found_inum);
    assert(ret == MODERNFS_SUCCESS);
    assert(found_inum == file2->inum);
    printf("  查找成功\n");

    printf("5. 删除目录项\n");
    ret = dir_remove(g_icache, dir, "file2.txt");
    assert(ret == MODERNFS_SUCCESS);
    printf("  删除file2.txt\n");

    printf("6. 验证删除\n");
    ret = dir_lookup(g_icache, dir, "file2.txt", &found_inum);
    assert(ret == MODERNFS_ENOENT);
    printf("  file2.txt已不存在\n");

    ret = dir_lookup(g_icache, dir, "file1.txt", &found_inum);
    assert(ret == MODERNFS_SUCCESS);
    ret = dir_lookup(g_icache, dir, "file3.txt", &found_inum);
    assert(ret == MODERNFS_SUCCESS);
    printf("  其他文件仍存在\n");

    printf("7. 清理\n");
    inode_free(g_icache, file1);
    inode_free(g_icache, file2);
    inode_free(g_icache, file3);
    inode_free(g_icache, dir);

    printf("\n✅ 测试3通过\n\n");
}

static void test_path_operations() {
    printf("========================================\n");
    printf("测试4: 路径操作\n");
    printf("========================================\n\n");

    printf("1. 测试路径规范化\n");
    char normalized[256];

    path_normalize("/a/b/../c/./d", normalized, sizeof(normalized));
    printf("  /a/b/../c/./d -> %s\n", normalized);
    assert(strcmp(normalized, "/a/c/d") == 0);

    path_normalize("a/./b/../c", normalized, sizeof(normalized));
    printf("  a/./b/../c -> %s\n", normalized);
    assert(strcmp(normalized, "a/c") == 0);

    printf("2. 测试basename\n");
    const char *base = path_basename("/foo/bar/test.txt");
    printf("  /foo/bar/test.txt -> %s\n", base);
    assert(strcmp(base, "test.txt") == 0);

    base = path_basename("/foo/bar/");
    printf("  /foo/bar/ -> %s\n", base);
    assert(strcmp(base, "bar") == 0);

    printf("3. 测试dirname\n");
    char dirname[256];
    path_dirname("/foo/bar/test.txt", dirname, sizeof(dirname));
    printf("  /foo/bar/test.txt -> %s\n", dirname);
    assert(strcmp(dirname, "/foo/bar") == 0);

    path_dirname("/test.txt", dirname, sizeof(dirname));
    printf("  /test.txt -> %s\n", dirname);
    assert(strcmp(dirname, "/") == 0);

    printf("\n✅ 测试4通过\n\n");
}

static void test_data_block_mapping() {
    printf("========================================\n");
    printf("测试5: 数据块映射\n");
    printf("========================================\n\n");

    printf("1. 分配文件Inode\n");
    inode_t_mem *inode = inode_alloc(g_icache, INODE_TYPE_FILE);
    assert(inode != NULL);

    printf("2. 测试直接块映射（前12块）\n");
    for (int i = 0; i < INODE_DIRECT_BLOCKS; i++) {
        block_t block;
        int ret = inode_bmap(g_icache, inode, i * BLOCK_SIZE, true, &block);
        assert(ret == MODERNFS_SUCCESS);
        assert(block != 0);
    }
    printf("  直接块映射成功\n");

    printf("3. 测试一级间接块映射\n");
    block_t block;
    int ret = inode_bmap(g_icache, inode, INODE_DIRECT_BLOCKS * BLOCK_SIZE, true, &block);
    assert(ret == MODERNFS_SUCCESS);
    assert(block != 0);
    printf("  一级间接块映射成功\n");

    printf("4. 写入跨越间接块的数据\n");
    uint64_t offset = (INODE_DIRECT_BLOCKS - 1) * BLOCK_SIZE;
    char *data = malloc(BLOCK_SIZE * 3);
    memset(data, 'X', BLOCK_SIZE * 3);

    ssize_t written = inode_write(g_icache, inode, data, offset, BLOCK_SIZE * 3, NULL);
    assert(written == BLOCK_SIZE * 3);
    printf("  写入 %zd 字节\n", written);

    printf("5. 读取并验证\n");
    char *read_buf = malloc(BLOCK_SIZE * 3);
    ssize_t read = inode_read(g_icache, inode, read_buf, offset, BLOCK_SIZE * 3);
    assert(read == BLOCK_SIZE * 3);
    assert(memcmp(read_buf, data, BLOCK_SIZE * 3) == 0);
    printf("  数据验证成功\n");

    free(data);
    free(read_buf);

    printf("6. 清理\n");
    inode_free(g_icache, inode);

    printf("\n✅ 测试5通过\n\n");
}

// ============ 主函数 ============

int main() {
    printf("\n");
    printf("╔══════════════════════════════════════╗\n");
    printf("║  ModernFS Inode层测试套件 (Week 3)  ║\n");
    printf("╚══════════════════════════════════════╝\n");
    printf("\n");

    setup_test_env();

    test_inode_alloc_free();
    test_inode_read_write();
    test_directory_ops();
    test_path_operations();
    test_data_block_mapping();

    teardown_test_env();

    printf("╔══════════════════════════════════════╗\n");
    printf("║        所有测试通过！ ✅             ║\n");
    printf("╚══════════════════════════════════════╝\n");
    printf("\n");

    return 0;
}
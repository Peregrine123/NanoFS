#define _GNU_SOURCE
#include "modernfs/types.h"
#include "modernfs/block_dev.h"
#include "modernfs/superblock.h"
#include "modernfs/buffer_cache.h"
#include "modernfs/block_alloc.h"
#include "modernfs/inode.h"
#include "modernfs/directory.h"
#include <stdio.h>
#include <stdlib.h>

int print_entry(const char *name, inode_t inum, void *arg) {
    printf("  Entry: %s (inum=%u)\n", name, inum);
    return 0;
}

int main() {
    const char *img = "/tmp/debug.img";

    // 打开设备
    block_device_t *dev = blkdev_open(img);
    if (!dev) {
        fprintf(stderr, "Failed to open %s\n", img);
        return 1;
    }

    // 读取超级块
    superblock_t sb;
    if (superblock_read(dev, &sb) < 0) {
        fprintf(stderr, "Failed to read superblock\n");
        return 1;
    }

    printf("Superblock: magic=0x%x, root_inum=%u\n", sb.magic, sb.root_inum);

    // 初始化块分配器
    block_allocator_t *balloc = block_alloc_init(dev, sb.data_bitmap_start,
                                                  sb.data_bitmap_blocks,
                                                  sb.data_start, sb.data_blocks);

    // 初始化inode缓存
    inode_cache_t *icache = inode_cache_init(dev, balloc, 64, 32);
    if (!icache) {
        fprintf(stderr, "Failed to init inode cache\n");
        return 1;
    }

    // 获取根目录inode
    inode_t_mem *root = inode_get(icache, sb.root_inum);
    if (!root) {
        fprintf(stderr, "Failed to get root inode\n");
        return 1;
    }

    inode_lock(root);
    printf("Root inode: type=%u, size=%lu, blocks=%lu\n",
           root->disk.type, root->disk.size, root->disk.blocks);

    // 遍历根目录
    printf("Root directory entries:\n");

    // 先直接读取数据块看看
    uint8_t block_buf[4096];
    ssize_t read_ret = inode_read(icache, root, block_buf, 0, 4096);
    printf("inode_read returned: %ld\n", read_ret);
    printf("First 64 bytes of root dir:\n");
    for (int i = 0; i < 64; i++) {
        printf("%02x ", block_buf[i]);
        if ((i+1) % 16 == 0) printf("\n");
    }
    printf("\n");

    int ret = dir_iterate(icache, root, print_entry, NULL);
    printf("dir_iterate returned: %d\n", ret);

    inode_unlock(root);
    inode_put(icache, root);

    inode_cache_destroy(icache);
    block_alloc_destroy(balloc);
    blkdev_close(dev);

    return 0;
}
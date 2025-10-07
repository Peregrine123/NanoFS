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
#include <string.h>

int main() {
    const char *img = "/tmp/write_test.img";

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

    // 获取根目录
    inode_t_mem *root = inode_get(icache, sb.root_inum);
    if (!root) {
        fprintf(stderr, "Failed to get root inode\n");
        return 1;
    }

    inode_lock(root);

    // 查找test.txt文件
    inode_t test_inum;
    if (dir_lookup(icache, root, "test.txt", &test_inum) < 0) {
        printf("test.txt not found\n");
        inode_unlock(root);
        inode_put(icache, root);
        goto cleanup;
    }

    printf("Found test.txt: inum=%u\n", test_inum);

    inode_unlock(root);
    inode_put(icache, root);

    // 获取test.txt的inode
    inode_t_mem *file = inode_get(icache, test_inum);
    if (!file) {
        fprintf(stderr, "Failed to get file inode\n");
        goto cleanup;
    }

    inode_lock(file);
    printf("File inode: type=%u, size=%lu, blocks=%lu, nlink=%u\n",
           file->disk.type, file->disk.size, file->disk.blocks, file->disk.nlink);
    printf("File direct[0]=%u, dirty=%d\n", file->disk.direct[0], file->dirty);

    // 尝试读取内容
    if (file->disk.size > 0) {
        uint8_t buf[256];
        ssize_t read_ret = inode_read(icache, file, buf, 0, sizeof(buf)-1);
        if (read_ret > 0) {
            buf[read_ret] = '\0';
            printf("File content: %s\n", buf);
        } else {
            printf("Read failed: %ld\n", read_ret);
        }
    } else {
        printf("File is empty\n");
    }

    inode_unlock(file);
    inode_put(icache, file);

cleanup:
    inode_cache_destroy(icache);
    block_alloc_destroy(balloc);
    blkdev_close(dev);

    return 0;
}
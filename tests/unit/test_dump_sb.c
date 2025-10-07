#define _GNU_SOURCE
#include "modernfs/superblock.h"
#include "modernfs/block_dev.h"
#include <stdio.h>

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <device>\n", argv[0]);
        return 1;
    }

    block_device_t *dev = blkdev_open(argv[1]);
    if (!dev) {
        fprintf(stderr, "Failed to open %s\n", argv[1]);
        return 1;
    }

    superblock_t sb;
    if (superblock_read(dev, &sb) < 0) {
        fprintf(stderr, "Failed to read superblock\n");
        blkdev_close(dev);
        return 1;
    }

    printf("Superblock contents:\n");
    printf("  magic: 0x%x\n", sb.magic);
    printf("  version: %u\n", sb.version);
    printf("  total_blocks: %u\n", sb.total_blocks);
    printf("  journal_start: %u\n", sb.journal_start);
    printf("  journal_blocks: %u\n", sb.journal_blocks);
    printf("  inode_bitmap_start: %u\n", sb.inode_bitmap_start);
    printf("  data_bitmap_start: %u\n", sb.data_bitmap_start);
    printf("  inode_table_start: %u\n", sb.inode_table_start);
    printf("  data_start: %u\n", sb.data_start);

    blkdev_close(dev);
    return 0;
}

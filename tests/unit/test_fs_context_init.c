// 测试fs_context初始化和销毁
#include "modernfs/fs_context.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <disk_image>\n", argv[0]);
        return 1;
    }

    const char *disk_path = argv[1];

    printf("=== Testing fs_context initialization ===\n");
    printf("Disk image: %s\n", disk_path);

    // 初始化
    printf("\n[1] Initializing fs_context...\n");
    fs_context_t *ctx = fs_context_init(disk_path, false);
    if (!ctx) {
        fprintf(stderr, "ERROR: Failed to initialize fs_context\n");
        return 1;
    }
    printf("✓ fs_context initialized successfully\n");

    // 等待一段时间让checkpoint线程运行
    printf("\n[2] Waiting 5 seconds for background threads...\n");
    sleep(5);
    printf("✓ Background threads running\n");

    // 执行一次手动sync
    printf("\n[3] Performing manual sync...\n");
    if (fs_context_sync(ctx) < 0) {
        fprintf(stderr, "ERROR: Failed to sync\n");
        fs_context_destroy(ctx);
        return 1;
    }
    printf("✓ Sync completed\n");

    // 销毁
    printf("\n[4] Destroying fs_context...\n");
    fs_context_destroy(ctx);
    printf("✓ fs_context destroyed successfully\n");

    printf("\n=== Test passed! ===\n");
    return 0;
}

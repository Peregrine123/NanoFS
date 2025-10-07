/*
 * 并发测试: 多线程Extent分配
 * 验证Extent Allocator的线程安全性
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include "modernfs/rust_ffi.h"
#include "modernfs/superblock.h"

#define NUM_THREADS 8
#define ALLOCS_PER_THREAD 50

typedef struct {
    int thread_id;
    RustExtentAllocator* alloc;
    int success_count;
    int fail_count;
    uint32_t allocated_blocks;
} thread_arg_t;

void* alloc_worker(void* arg) {
    thread_arg_t* targ = (thread_arg_t*)arg;

    for (int i = 0; i < ALLOCS_PER_THREAD; i++) {
        uint32_t start, len;

        // 随机分配10-50个块
        uint32_t min_len = 10;
        uint32_t max_len = 50;

        int ret = rust_extent_alloc(
            targ->alloc,
            0,  // hint
            min_len,
            max_len,
            &start,
            &len
        );

        if (ret == 0) {
            targ->success_count++;
            targ->allocated_blocks += len;

            if ((i + 1) % 10 == 0) {
                printf("[Thread %d] Allocated: %d extents, %u blocks total\n",
                       targ->thread_id, i + 1, targ->allocated_blocks);
            }
        } else {
            targ->fail_count++;
            fprintf(stderr, "[Thread %d] Allocation %d failed\n",
                    targ->thread_id, i);
        }

        // 小延迟，增加并发度
        usleep(100);
    }

    printf("[Thread %d] Completed: %d success, %d failed, %u blocks\n",
           targ->thread_id, targ->success_count, targ->fail_count,
           targ->allocated_blocks);

    return NULL;
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <image>\n", argv[0]);
        return 1;
    }

    printf("╔════════════════════════════════════════╗\n");
    printf("║  并发Extent分配测试                    ║\n");
    printf("║  %d threads × %d allocs                ║\n",
           NUM_THREADS, ALLOCS_PER_THREAD);
    printf("╚════════════════════════════════════════╝\n\n");

    // 打开镜像
    int fd = open(argv[1], O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    // 读取超级块
    superblock_t sb;
    if (read_superblock(fd, &sb) != 0) {
        fprintf(stderr, "Failed to read superblock\n");
        close(fd);
        return 1;
    }

    // 初始化Extent Allocator
    RustExtentAllocator* alloc = rust_extent_alloc_init(
        fd,
        sb.data_bitmap_start,
        sb.data_blocks
    );

    if (!alloc) {
        fprintf(stderr, "Failed to init extent allocator\n");
        close(fd);
        return 1;
    }

    printf("[INFO] Extent Allocator initialized\n");
    printf("[INFO] Total blocks: %u\n\n", sb.data_blocks);

    // 获取初始统计
    uint32_t init_total, init_free, init_allocated;
    rust_extent_get_stats(alloc, &init_total, &init_free, &init_allocated);
    printf("[STATS] Initial: total=%u, free=%u, allocated=%u\n\n",
           init_total, init_free, init_allocated);

    // 创建线程
    pthread_t threads[NUM_THREADS];
    thread_arg_t thread_args[NUM_THREADS];

    struct timespec start_time, end_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);

    printf("[INFO] Starting %d threads...\n\n", NUM_THREADS);

    for (int i = 0; i < NUM_THREADS; i++) {
        thread_args[i].thread_id = i;
        thread_args[i].alloc = alloc;
        thread_args[i].success_count = 0;
        thread_args[i].fail_count = 0;
        thread_args[i].allocated_blocks = 0;

        if (pthread_create(&threads[i], NULL, alloc_worker, &thread_args[i]) != 0) {
            fprintf(stderr, "Failed to create thread %d\n", i);
            return 1;
        }
    }

    // 等待所有线程
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    clock_gettime(CLOCK_MONOTONIC, &end_time);
    double elapsed = (end_time.tv_sec - start_time.tv_sec) +
                     (end_time.tv_nsec - start_time.tv_nsec) / 1e9;

    printf("\n");
    printf("════════════════════════════════════════\n");
    printf("  测试统计\n");
    printf("════════════════════════════════════════\n");

    int total_success = 0;
    int total_failed = 0;
    uint32_t total_allocated_blocks = 0;

    for (int i = 0; i < NUM_THREADS; i++) {
        total_success += thread_args[i].success_count;
        total_failed += thread_args[i].fail_count;
        total_allocated_blocks += thread_args[i].allocated_blocks;
    }

    printf("  总分配次数: %d\n", total_success + total_failed);
    printf("  成功:       %d\n", total_success);
    printf("  失败:       %d\n", total_failed);
    printf("  分配块数:   %u\n", total_allocated_blocks);
    printf("  耗时:       %.2f 秒\n", elapsed);
    printf("  吞吐量:     %.1f 分配/秒\n", total_success / elapsed);
    printf("════════════════════════════════════════\n\n");

    // 获取最终统计
    uint32_t final_total, final_free, final_allocated;
    rust_extent_get_stats(alloc, &final_total, &final_free, &final_allocated);

    printf("[STATS] Final: total=%u, free=%u, allocated=%u\n",
           final_total, final_free, final_allocated);
    printf("[STATS] Allocated change: %u -> %u (+%u)\n",
           init_allocated, final_allocated, final_allocated - init_allocated);

    // 验证统计一致性
    if (final_allocated - init_allocated == total_allocated_blocks) {
        printf("  ✅ 统计一致: 分配的块数匹配\n");
    } else {
        printf("  ❌ 统计不一致: 预期 %u, 实际 %u\n",
               total_allocated_blocks, final_allocated - init_allocated);
    }

    // 获取碎片率
    float frag = rust_extent_fragmentation(alloc);
    printf("[STATS] Fragmentation: %.2f%%\n", frag * 100);

    // 清理
    rust_extent_alloc_destroy(alloc);
    close(fd);

    printf("\n");
    if (total_failed == 0 &&
        final_allocated - init_allocated == total_allocated_blocks) {
        printf("╔════════════════════════════════════════╗\n");
        printf("║  测试结果: ✅ PASS                     ║\n");
        printf("║  Extent Allocator是线程安全的          ║\n");
        printf("╚════════════════════════════════════════╝\n");
        return 0;
    } else {
        printf("╔════════════════════════════════════════╗\n");
        printf("║  测试结果: ❌ FAIL                     ║\n");
        printf("╚════════════════════════════════════════╝\n");
        return 1;
    }
}

/*
 * 并发测试: 多线程写入
 * 验证Journal Manager的线程安全性
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include "modernfs/rust_ffi.h"
#include "modernfs/superblock.h"
#include "modernfs/block_dev.h"

#define BLOCK_SIZE 4096
#define NUM_THREADS 10
#define WRITES_PER_THREAD 100

typedef struct {
    int thread_id;
    RustJournalManager* jm;
    int success_count;
    int fail_count;
} thread_arg_t;

void* worker_thread(void* arg) {
    thread_arg_t* targ = (thread_arg_t*)arg;
    uint8_t data[BLOCK_SIZE];

    // 每个线程填充不同的数据模式
    memset(data, 0xC0 + targ->thread_id, BLOCK_SIZE);

    for (int i = 0; i < WRITES_PER_THREAD; i++) {
        // 开始事务
        RustTransaction* txn = rust_journal_begin(targ->jm);
        if (!txn) {
            fprintf(stderr, "[Thread %d] Failed to begin transaction %d\n",
                    targ->thread_id, i);
            targ->fail_count++;
            continue;
        }

        // 写入1个块（每个线程有自己的块范围，避免冲突）
        uint32_t block_num = 10000 + targ->thread_id * WRITES_PER_THREAD + i;
        int ret = rust_journal_write(txn, block_num, data);
        if (ret != 0) {
            fprintf(stderr, "[Thread %d] Failed to write block %u\n",
                    targ->thread_id, block_num);
            rust_journal_abort(txn);
            targ->fail_count++;
            continue;
        }

        // 提交事务
        ret = rust_journal_commit(targ->jm, txn);
        if (ret != 0) {
            fprintf(stderr, "[Thread %d] Failed to commit transaction %d\n",
                    targ->thread_id, i);
            targ->fail_count++;
            continue;
        }

        targ->success_count++;

        // 每10次提交打印进度
        if ((i + 1) % 10 == 0) {
            printf("[Thread %d] Progress: %d/%d\n",
                   targ->thread_id, i + 1, WRITES_PER_THREAD);
        }
    }

    printf("[Thread %d] Completed: %d success, %d failed\n",
           targ->thread_id, targ->success_count, targ->fail_count);

    return NULL;
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <image>\n", argv[0]);
        return 1;
    }

    printf("╔════════════════════════════════════════╗\n");
    printf("║  并发写入测试                          ║\n");
    printf("║  %d threads × %d writes = %d total     ║\n",
           NUM_THREADS, WRITES_PER_THREAD, NUM_THREADS * WRITES_PER_THREAD);
    printf("╚════════════════════════════════════════╝\n\n");

    // 打开块设备
    block_device_t *dev = blkdev_open(argv[1]);
    if (!dev) {
        fprintf(stderr, "Failed to open device\n");
        return 1;
    }

    // 读取超级块
    superblock_t sb;
    if (superblock_read(dev, &sb) != 0) {
        fprintf(stderr, "Failed to read superblock\n");
        blkdev_close(dev);
        return 1;
    }

    // 初始化Journal Manager
    RustJournalManager* jm = rust_journal_init(
        dev->fd,
        sb.journal_start,
        sb.journal_blocks
    );

    if (!jm) {
        fprintf(stderr, "Failed to init journal\n");
        blkdev_close(dev);
        return 1;
    }

    printf("[INFO] Journal Manager initialized\n\n");

    // 创建线程
    pthread_t threads[NUM_THREADS];
    thread_arg_t thread_args[NUM_THREADS];

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    printf("[INFO] Starting %d threads...\n\n", NUM_THREADS);

    for (int i = 0; i < NUM_THREADS; i++) {
        thread_args[i].thread_id = i;
        thread_args[i].jm = jm;
        thread_args[i].success_count = 0;
        thread_args[i].fail_count = 0;

        if (pthread_create(&threads[i], NULL, worker_thread, &thread_args[i]) != 0) {
            fprintf(stderr, "Failed to create thread %d\n", i);
            return 1;
        }
    }

    // 等待所有线程完成
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    clock_gettime(CLOCK_MONOTONIC, &end);
    double elapsed = (end.tv_sec - start.tv_sec) +
                     (end.tv_nsec - start.tv_nsec) / 1e9;

    printf("\n");
    printf("════════════════════════════════════════\n");
    printf("  测试统计\n");
    printf("════════════════════════════════════════\n");

    int total_success = 0;
    int total_failed = 0;

    for (int i = 0; i < NUM_THREADS; i++) {
        total_success += thread_args[i].success_count;
        total_failed += thread_args[i].fail_count;
    }

    printf("  总事务数:   %d\n", total_success + total_failed);
    printf("  成功:       %d\n", total_success);
    printf("  失败:       %d\n", total_failed);
    printf("  耗时:       %.2f 秒\n", elapsed);
    printf("  吞吐量:     %.1f 事务/秒\n", total_success / elapsed);
    printf("════════════════════════════════════════\n\n");

    // 执行 checkpoint 将数据从 journal 写入最终位置
    printf("[INFO] 执行 checkpoint...\n");
    if (rust_journal_checkpoint(jm) != 0) {
        fprintf(stderr, "  ❌ Checkpoint 失败\n");
        blkdev_close(dev);
        rust_journal_destroy(jm);
        return 1;
    }
    printf("[INFO] Checkpoint 完成\n\n");

    // 验证数据
    printf("[INFO] 验证数据完整性...\n");
    int verify_errors = 0;

    for (int t = 0; t < NUM_THREADS; t++) {
        uint8_t expected = 0xC0 + t;
        uint8_t data[BLOCK_SIZE];

        for (int i = 0; i < WRITES_PER_THREAD; i++) {
            uint32_t block_num = 10000 + t * WRITES_PER_THREAD + i;
            off_t offset = (off_t)block_num * BLOCK_SIZE;

            if (pread(dev->fd, data, BLOCK_SIZE, offset) != BLOCK_SIZE) {
                fprintf(stderr, "  ❌ 读取块 %u 失败\n", block_num);
                verify_errors++;
                continue;
            }

            // 检查数据
            int block_ok = 1;
            for (int j = 0; j < BLOCK_SIZE; j++) {
                if (data[j] != expected) {
                    block_ok = 0;
                    break;
                }
            }

            if (!block_ok) {
                fprintf(stderr, "  ❌ 块 %u: 数据不匹配\n", block_num);
                verify_errors++;
            }
        }

        if (verify_errors == 0) {
            printf("  ✅ 线程 %d: 所有 %d 个块数据正确\n", t, WRITES_PER_THREAD);
        }
    }

    if (verify_errors == 0) {
        printf("\n✅ 数据验证通过: 无数据竞争\n");
    } else {
        printf("\n❌ 数据验证失败: %d 个错误\n", verify_errors);
    }

    // 清理
    rust_journal_destroy(jm);
    blkdev_close(dev);

    printf("\n");
    if (total_failed == 0 && verify_errors == 0) {
        printf("╔════════════════════════════════════════╗\n");
        printf("║  测试结果: ✅ PASS                     ║\n");
        printf("║  Journal Manager是线程安全的           ║\n");
        printf("╚════════════════════════════════════════╝\n");
        return 0;
    } else {
        printf("╔════════════════════════════════════════╗\n");
        printf("║  测试结果: ❌ FAIL                     ║\n");
        printf("╚════════════════════════════════════════╝\n");
        return 1;
    }
}

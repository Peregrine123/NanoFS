/**
 * Week 7 集成测试: Journal + Extent + fs_context
 *
 * 测试内容:
 * 1. fs_context初始化Journal和Extent
 * 2. 崩溃恢复机制
 * 3. Checkpoint线程
 * 4. Journal+Extent协同工作
 */

#define _GNU_SOURCE
#include "modernfs/fs_context.h"
#include "modernfs/rust_ffi.h"
#include "modernfs/inode.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>

#define TEST_IMG "test_week7.img"

// 辅助函数: 创建测试镜像
static int create_test_image() {
    printf("Creating test image...\n");

    // 使用系统命令创建镜像
    const char *cmd = "./build/mkfs.modernfs test_week7.img 64 > /dev/null 2>&1";
    int ret = system(cmd);
    if (ret != 0) {
        fprintf(stderr, "Failed to format filesystem (mkfs.modernfs not found or failed)\n");
        fprintf(stderr, "Please build mkfs.modernfs first\n");
        return -1;
    }

    printf("  ✓ Test image created\n");
    return 0;
}

// 测试1: fs_context初始化和销毁
static int test_fs_context_init() {
    printf("\n[测试1] fs_context初始化和销毁\n");

    fs_context_t *ctx = fs_context_init(TEST_IMG, false);
    if (!ctx) {
        fprintf(stderr, "  ✗ Failed to init fs_context\n");
        return -1;
    }

    printf("  ✓ fs_context初始化成功\n");

    // 验证Journal和Extent已初始化
    if (!ctx->journal) {
        fprintf(stderr, "  ✗ Journal not initialized\n");
        fs_context_destroy(ctx);
        return -1;
    }
    printf("  ✓ Journal Manager已初始化\n");

    if (!ctx->extent_alloc) {
        fprintf(stderr, "  ✗ Extent Allocator not initialized\n");
        fs_context_destroy(ctx);
        return -1;
    }
    printf("  ✓ Extent Allocator已初始化\n");

    // 验证Checkpoint线程已启动
    if (!ctx->checkpoint_running) {
        fprintf(stderr, "  ✗ Checkpoint thread not running\n");
        fs_context_destroy(ctx);
        return -1;
    }
    printf("  ✓ Checkpoint线程已启动\n");

    fs_context_destroy(ctx);
    printf("  ✓ fs_context销毁成功\n");

    return 0;
}

// 测试2: Journal事务基础操作
static int test_journal_transaction() {
    printf("\n[测试2] Journal事务基础操作\n");

    fs_context_t *ctx = fs_context_init(TEST_IMG, false);
    if (!ctx) {
        fprintf(stderr, "  ✗ Failed to init fs_context\n");
        return -1;
    }

    // 开始事务
    RustTransaction *txn = rust_journal_begin(ctx->journal);
    if (!txn) {
        fprintf(stderr, "  ✗ Failed to begin transaction\n");
        fs_context_destroy(ctx);
        return -1;
    }
    printf("  ✓ 事务开始成功\n");

    // 写入一些块
    uint8_t data[4096];
    memset(data, 0xAB, sizeof(data));

    for (int i = 0; i < 5; i++) {
        if (rust_journal_write(txn, 1000 + i, data) < 0) {
            fprintf(stderr, "  ✗ Failed to write block %d\n", i);
            rust_journal_abort(txn);
            fs_context_destroy(ctx);
            return -1;
        }
    }
    printf("  ✓ 已写入5个块到事务\n");

    // 提交事务
    if (rust_journal_commit(ctx->journal, txn) < 0) {
        fprintf(stderr, "  ✗ Failed to commit transaction\n");
        fs_context_destroy(ctx);
        return -1;
    }
    printf("  ✓ 事务提交成功\n");

    fs_context_destroy(ctx);
    return 0;
}

// 测试3: Extent分配和释放
static int test_extent_allocation() {
    printf("\n[测试3] Extent分配和释放\n");

    fs_context_t *ctx = fs_context_init(TEST_IMG, false);
    if (!ctx) {
        fprintf(stderr, "  ✗ Failed to init fs_context\n");
        return -1;
    }

    uint32_t start, len;

    // 分配extent
    if (rust_extent_alloc(ctx->extent_alloc, 0, 10, 20, &start, &len) < 0) {
        fprintf(stderr, "  ✗ Failed to allocate extent\n");
        fs_context_destroy(ctx);
        return -1;
    }
    printf("  ✓ 分配extent成功: [%u, +%u]\n", start, len);

    // 获取统计信息
    uint32_t total, free, allocated;
    if (rust_extent_get_stats(ctx->extent_alloc, &total, &free, &allocated) < 0) {
        fprintf(stderr, "  ✗ Failed to get stats\n");
        fs_context_destroy(ctx);
        return -1;
    }
    printf("  ✓ 统计信息: total=%u, free=%u, allocated=%u\n", total, free, allocated);

    // 释放extent
    if (rust_extent_free(ctx->extent_alloc, start, len) < 0) {
        fprintf(stderr, "  ✗ Failed to free extent\n");
        fs_context_destroy(ctx);
        return -1;
    }
    printf("  ✓ 释放extent成功\n");

    // 同步位图
    if (rust_extent_sync(ctx->extent_alloc) < 0) {
        fprintf(stderr, "  ✗ Failed to sync extent allocator\n");
        fs_context_destroy(ctx);
        return -1;
    }
    printf("  ✓ 位图同步成功\n");

    fs_context_destroy(ctx);
    return 0;
}

// 测试4: Checkpoint功能
static int test_checkpoint() {
    printf("\n[测试4] Checkpoint功能\n");

    fs_context_t *ctx = fs_context_init(TEST_IMG, false);
    if (!ctx) {
        fprintf(stderr, "  ✗ Failed to init fs_context\n");
        return -1;
    }

    // 创建一些事务
    for (int i = 0; i < 3; i++) {
        RustTransaction *txn = rust_journal_begin(ctx->journal);
        if (!txn) {
            fprintf(stderr, "  ✗ Failed to begin transaction %d\n", i);
            fs_context_destroy(ctx);
            return -1;
        }

        uint8_t data[4096];
        memset(data, 0xCD, sizeof(data));

        if (rust_journal_write(txn, 2000 + i, data) < 0) {
            fprintf(stderr, "  ✗ Failed to write block\n");
            rust_journal_abort(txn);
            fs_context_destroy(ctx);
            return -1;
        }

        if (rust_journal_commit(ctx->journal, txn) < 0) {
            fprintf(stderr, "  ✗ Failed to commit transaction %d\n", i);
            fs_context_destroy(ctx);
            return -1;
        }
    }
    printf("  ✓ 已创建3个事务\n");

    // 执行checkpoint
    if (rust_journal_checkpoint(ctx->journal) < 0) {
        fprintf(stderr, "  ✗ Checkpoint failed\n");
        fs_context_destroy(ctx);
        return -1;
    }
    printf("  ✓ Checkpoint执行成功\n");

    fs_context_destroy(ctx);
    return 0;
}

// 测试5: 崩溃恢复
static int test_crash_recovery() {
    printf("\n[测试5] 崩溃恢复\n");

    // 阶段1: 创建事务但不checkpoint
    {
        fs_context_t *ctx = fs_context_init(TEST_IMG, false);
        if (!ctx) {
            fprintf(stderr, "  ✗ Failed to init fs_context\n");
            return -1;
        }

        RustTransaction *txn = rust_journal_begin(ctx->journal);
        if (!txn) {
            fprintf(stderr, "  ✗ Failed to begin transaction\n");
            fs_context_destroy(ctx);
            return -1;
        }

        uint8_t data[4096];
        memset(data, 0xEF, sizeof(data));

        if (rust_journal_write(txn, 3000, data) < 0) {
            fprintf(stderr, "  ✗ Failed to write block\n");
            rust_journal_abort(txn);
            fs_context_destroy(ctx);
            return -1;
        }

        if (rust_journal_commit(ctx->journal, txn) < 0) {
            fprintf(stderr, "  ✗ Failed to commit transaction\n");
            fs_context_destroy(ctx);
            return -1;
        }

        printf("  ✓ 阶段1: 事务已提交（模拟崩溃前）\n");

        // 不执行checkpoint，直接销毁（模拟崩溃）
        // 注意: 我们需要手动停止checkpoint线程，避免自动checkpoint
        pthread_mutex_lock(&ctx->checkpoint_lock);
        ctx->checkpoint_running = false;
        pthread_cond_signal(&ctx->checkpoint_cond);
        pthread_mutex_unlock(&ctx->checkpoint_lock);
        pthread_join(ctx->checkpoint_thread, NULL);

        // 销毁但不调用fs_context_sync
        if (ctx->extent_alloc) {
            rust_extent_alloc_destroy(ctx->extent_alloc);
        }
        if (ctx->journal) {
            rust_journal_destroy(ctx->journal);
        }
        if (ctx->icache) {
            inode_cache_destroy(ctx->icache);
        }
        if (ctx->balloc) {
            block_alloc_destroy(ctx->balloc);
        }
        // 注意: ctx->sb 会被 blkdev_close 释放，不要重复释放
        if (ctx->dev) {
            blkdev_close(ctx->dev);
        }
        free(ctx);
    }

    // 阶段2: 重新挂载，触发恢复
    {
        printf("  ✓ 阶段2: 重新挂载，触发恢复...\n");

        fs_context_t *ctx = fs_context_init(TEST_IMG, false);
        if (!ctx) {
            fprintf(stderr, "  ✗ Failed to init fs_context after crash\n");
            return -1;
        }

        printf("  ✓ 恢复完成（具体恢复数量见上方输出）\n");

        fs_context_destroy(ctx);
    }

    return 0;
}

// 测试6: fs_context_sync
static int test_fs_context_sync() {
    printf("\n[测试6] fs_context_sync\n");

    fs_context_t *ctx = fs_context_init(TEST_IMG, false);
    if (!ctx) {
        fprintf(stderr, "  ✗ Failed to init fs_context\n");
        return -1;
    }

    // 创建一些事务
    RustTransaction *txn = rust_journal_begin(ctx->journal);
    if (!txn) {
        fprintf(stderr, "  ✗ Failed to begin transaction\n");
        fs_context_destroy(ctx);
        return -1;
    }

    uint8_t data[4096];
    memset(data, 0x12, sizeof(data));

    if (rust_journal_write(txn, 4000, data) < 0) {
        fprintf(stderr, "  ✗ Failed to write block\n");
        rust_journal_abort(txn);
        fs_context_destroy(ctx);
        return -1;
    }

    if (rust_journal_commit(ctx->journal, txn) < 0) {
        fprintf(stderr, "  ✗ Failed to commit transaction\n");
        fs_context_destroy(ctx);
        return -1;
    }

    printf("  ✓ 事务已提交\n");

    // 调用fs_context_sync
    if (fs_context_sync(ctx) < 0) {
        fprintf(stderr, "  ✗ fs_context_sync failed\n");
        fs_context_destroy(ctx);
        return -1;
    }

    printf("  ✓ fs_context_sync成功（包含checkpoint和extent sync）\n");

    fs_context_destroy(ctx);
    return 0;
}

int main() {
    printf("╔════════════════════════════════════════╗\n");
    printf("║  ModernFS Week 7 集成测试套件         ║\n");
    printf("║  Journal + Extent + fs_context        ║\n");
    printf("╚════════════════════════════════════════╝\n");

    // 创建测试镜像
    if (create_test_image() < 0) {
        return 1;
    }

    // 运行所有测试
    int failed = 0;

    if (test_fs_context_init() < 0) {
        fprintf(stderr, "✗ 测试1失败\n");
        failed++;
    }

    if (test_journal_transaction() < 0) {
        fprintf(stderr, "✗ 测试2失败\n");
        failed++;
    }

    if (test_extent_allocation() < 0) {
        fprintf(stderr, "✗ 测试3失败\n");
        failed++;
    }

    if (test_checkpoint() < 0) {
        fprintf(stderr, "✗ 测试4失败\n");
        failed++;
    }

    if (test_crash_recovery() < 0) {
        fprintf(stderr, "✗ 测试5失败\n");
        failed++;
    }

    if (test_fs_context_sync() < 0) {
        fprintf(stderr, "✗ 测试6失败\n");
        failed++;
    }

    // 清理测试文件
    unlink(TEST_IMG);

    printf("\n╔════════════════════════════════════════╗\n");
    if (failed == 0) {
        printf("║  所有测试通过！ ✅                     ║\n");
    } else {
        printf("║  %d 个测试失败 ✗                      ║\n", failed);
    }
    printf("╚════════════════════════════════════════╝\n");

    return failed == 0 ? 0 : 1;
}

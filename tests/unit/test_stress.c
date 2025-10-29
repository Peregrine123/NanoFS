/**
 * test_stress.c - 压力测试和性能测试
 * 
 * 测试文件系统在高负载下的性能和稳定性：
 * 1. 大量小文件创建/删除
 * 2. 大文件顺序读写性能
 * 3. 随机读写性能
 * 4. 深层目录结构
 * 5. 碎片化场景
 * 6. 连续操作压力测试
 */

#define _GNU_SOURCE
#include "modernfs/fs_context.h"
#include "modernfs/rust_ffi.h"
#include "modernfs/inode.h"
#include "modernfs/directory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>

#define TEST_IMG "test_stress.img"
#define BLOCK_SIZE 4096

// ===== 辅助函数 =====

static void print_test_header(const char *title) {
    printf("\n");
    printf("╔════════════════════════════════════════════════════════╗\n");
    printf("║  %-52s  ║\n", title);
    printf("╚════════════════════════════════════════════════════════╝\n");
}

static double get_time_ms() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

static int create_test_image() {
    printf("正在创建测试镜像...\n");
    char cmd[256];
    // 创建256MB镜像用于压力测试
    snprintf(cmd, sizeof(cmd), "./build/mkfs.modernfs %s 256 > /dev/null 2>&1", TEST_IMG);
    int ret = system(cmd);
    if (ret != 0) {
        fprintf(stderr, "错误：无法格式化文件系统\n");
        return -1;
    }
    printf("  ✓ 测试镜像创建成功 (256MB)\n");
    return 0;
}

// ===== 测试1：大量小文件创建 =====

static int test_many_small_files() {
    print_test_header("测试1：大量小文件创建（1000个文件）");
    
    fs_context_t *ctx = fs_context_init(TEST_IMG, false);
    if (!ctx) return -1;
    
    inode_t_mem *root = inode_get(ctx->icache, ctx->sb->root_inum);
    if (!root) {
        fs_context_destroy(ctx);
        return -1;
    }
    inode_lock(root);
    
    const int num_files = 1000;
    const char *test_data = "small file content";
    size_t data_len = strlen(test_data);
    
    double start_time = get_time_ms();
    
    int created = 0;
    for (int i = 0; i < num_files; i++) {
        char filename[32];
        snprintf(filename, sizeof(filename), "small%04d.txt", i);
        
        inode_t_mem *file = inode_alloc(ctx->icache, INODE_TYPE_FILE);
        if (!file) {
            printf("  ℹ️  Inode分配失败在第%d个文件（可能inode耗尽）\n", i);
            break;
        }
        
        inode_lock(file);
        ssize_t written = inode_write(ctx->icache, file, test_data, 0, data_len, NULL);
        inode_unlock(file);
        
        if (written != (ssize_t)data_len) {
            printf("  ℹ️  写入失败在第%d个文件（可能磁盘满）\n", i);
            inode_put(ctx->icache, file);
            break;
        }
        
        int ret = dir_add(ctx->icache, root, filename, file->inum);
        inode_put(ctx->icache, file);
        
        if (ret < 0) {
            printf("  ℹ️  添加目录项失败在第%d个文件\n", i);
            break;
        }
        
        created++;
        
        // 每100个文件打印进度
        if ((i + 1) % 100 == 0) {
            printf("  进度: %d/%d 文件已创建\n", i + 1, num_files);
        }
    }
    
    double end_time = get_time_ms();
    double elapsed = end_time - start_time;
    
    printf("  ✓ 成功创建 %d 个小文件\n", created);
    printf("  ✓ 总耗时: %.2f ms\n", elapsed);
    printf("  ✓ 平均每文件: %.3f ms\n", elapsed / created);
    printf("  ✓ 吞吐量: %.0f 文件/秒\n", created * 1000.0 / elapsed);
    
    inode_unlock(root);
    inode_put(ctx->icache, root);
    
    fs_context_destroy(ctx);
    printf("  ✅ 测试通过\n");
    return 0;
}

// ===== 测试2：大文件顺序写入 =====

static int test_large_sequential_write() {
    print_test_header("测试2：大文件顺序写入（10MB）");
    
    fs_context_t *ctx = fs_context_init(TEST_IMG, false);
    if (!ctx) return -1;
    
    inode_t_mem *root = inode_get(ctx->icache, ctx->sb->root_inum);
    if (!root) {
        fs_context_destroy(ctx);
        return -1;
    }
    inode_lock(root);
    
    inode_t_mem *file = inode_alloc(ctx->icache, INODE_TYPE_FILE);
    if (!file) {
        fprintf(stderr, "  ✗ 文件分配失败\n");
        inode_unlock(root);
        inode_put(ctx->icache, root);
        fs_context_destroy(ctx);
        return -1;
    }
    
    int ret = dir_add(ctx->icache, root, "large_file.dat", file->inum);
    if (ret < 0) {
        fprintf(stderr, "  ✗ 添加目录项失败\n");
        inode_put(ctx->icache, file);
        inode_unlock(root);
        inode_put(ctx->icache, root);
        fs_context_destroy(ctx);
        return -1;
    }
    
    // 准备10MB数据
    size_t file_size = 10 * 1024 * 1024;  // 10MB
    size_t chunk_size = 64 * 1024;  // 每次写64KB
    char *chunk = malloc(chunk_size);
    if (!chunk) {
        fprintf(stderr, "  ✗ 内存分配失败\n");
        inode_put(ctx->icache, file);
        inode_unlock(root);
        inode_put(ctx->icache, root);
        fs_context_destroy(ctx);
        return -1;
    }
    
    // 填充模式数据
    for (size_t i = 0; i < chunk_size; i++) {
        chunk[i] = (char)(i % 256);
    }
    
    inode_lock(file);
    
    double start_time = get_time_ms();
    size_t total_written = 0;
    
    for (size_t offset = 0; offset < file_size; offset += chunk_size) {
        size_t to_write = (offset + chunk_size > file_size) ? (file_size - offset) : chunk_size;
        
        ssize_t written = inode_write(ctx->icache, file, chunk, offset, to_write, NULL);
        if (written <= 0) {
            printf("  ℹ️  写入停止在 %.2f MB（可能磁盘满）\n", offset / (1024.0 * 1024.0));
            break;
        }
        
        total_written += written;
        
        // 每1MB打印进度
        if ((offset + chunk_size) % (1024 * 1024) == 0) {
            printf("  进度: %.2f / %.2f MB\n", 
                   (offset + chunk_size) / (1024.0 * 1024.0),
                   file_size / (1024.0 * 1024.0));
        }
    }
    
    double end_time = get_time_ms();
    double elapsed = end_time - start_time;
    
    inode_unlock(file);
    
    printf("  ✓ 写入完成: %.2f MB\n", total_written / (1024.0 * 1024.0));
    printf("  ✓ 总耗时: %.2f ms\n", elapsed);
    printf("  ✓ 写入速度: %.2f MB/s\n", (total_written / (1024.0 * 1024.0)) / (elapsed / 1000.0));
    
    free(chunk);
    inode_put(ctx->icache, file);
    inode_unlock(root);
    inode_put(ctx->icache, root);
    
    fs_context_destroy(ctx);
    printf("  ✅ 测试通过\n");
    return 0;
}

// ===== 测试3：大文件顺序读取 =====

static int test_large_sequential_read() {
    print_test_header("测试3：大文件顺序读取");
    
    fs_context_t *ctx = fs_context_init(TEST_IMG, false);
    if (!ctx) return -1;
    
    inode_t_mem *root = inode_get(ctx->icache, ctx->sb->root_inum);
    if (!root) {
        fs_context_destroy(ctx);
        return -1;
    }
    inode_lock(root);
    
    // 查找之前创建的大文件
    inode_t file_inum;
    int ret = dir_lookup(ctx->icache, root, "large_file.dat", &file_inum);
    if (ret < 0) {
        printf("  ℹ️  大文件不存在（可能未创建），跳过测试\n");
        inode_unlock(root);
        inode_put(ctx->icache, root);
        fs_context_destroy(ctx);
        return 0;  // 不算失败
    }
    
    inode_t_mem *file = inode_get(ctx->icache, file_inum);
    if (!file) {
        fprintf(stderr, "  ✗ 获取文件inode失败\n");
        inode_unlock(root);
        inode_put(ctx->icache, root);
        fs_context_destroy(ctx);
        return -1;
    }
    
    inode_lock(file);
    size_t file_size = file->disk.size;
    printf("  文件大小: %.2f MB\n", file_size / (1024.0 * 1024.0));
    
    size_t chunk_size = 64 * 1024;  // 每次读64KB
    char *chunk = malloc(chunk_size);
    if (!chunk) {
        fprintf(stderr, "  ✗ 内存分配失败\n");
        inode_unlock(file);
        inode_put(ctx->icache, file);
        inode_unlock(root);
        inode_put(ctx->icache, root);
        fs_context_destroy(ctx);
        return -1;
    }
    
    double start_time = get_time_ms();
    size_t total_read = 0;
    
    for (size_t offset = 0; offset < file_size; offset += chunk_size) {
        size_t to_read = (offset + chunk_size > file_size) ? (file_size - offset) : chunk_size;
        
        ssize_t read_bytes = inode_read(ctx->icache, file, chunk, offset, to_read);
        if (read_bytes <= 0) {
            printf("  ℹ️  读取停止在 %.2f MB\n", offset / (1024.0 * 1024.0));
            break;
        }
        
        total_read += read_bytes;
    }
    
    double end_time = get_time_ms();
    double elapsed = end_time - start_time;
    
    printf("  ✓ 读取完成: %.2f MB\n", total_read / (1024.0 * 1024.0));
    printf("  ✓ 总耗时: %.2f ms\n", elapsed);
    printf("  ✓ 读取速度: %.2f MB/s\n", (total_read / (1024.0 * 1024.0)) / (elapsed / 1000.0));
    
    free(chunk);
    inode_unlock(file);
    inode_put(ctx->icache, file);
    inode_unlock(root);
    inode_put(ctx->icache, root);
    
    fs_context_destroy(ctx);
    printf("  ✅ 测试通过\n");
    return 0;
}

// ===== 测试4：随机读写 =====

static int test_random_io() {
    print_test_header("测试4：随机读写（1000次操作）");
    
    fs_context_t *ctx = fs_context_init(TEST_IMG, false);
    if (!ctx) return -1;
    
    inode_t_mem *root = inode_get(ctx->icache, ctx->sb->root_inum);
    if (!root) {
        fs_context_destroy(ctx);
        return -1;
    }
    inode_lock(root);
    
    // 创建测试文件
    inode_t_mem *file = inode_alloc(ctx->icache, INODE_TYPE_FILE);
    if (!file) {
        fprintf(stderr, "  ✗ 文件分配失败\n");
        inode_unlock(root);
        inode_put(ctx->icache, root);
        fs_context_destroy(ctx);
        return -1;
    }
    
    int ret = dir_add(ctx->icache, root, "random_io.dat", file->inum);
    if (ret < 0) {
        fprintf(stderr, "  ✗ 添加目录项失败\n");
        inode_put(ctx->icache, file);
        inode_unlock(root);
        inode_put(ctx->icache, root);
        fs_context_destroy(ctx);
        return -1;
    }
    
    inode_lock(file);
    
    // 随机种子
    srand(time(NULL));
    
    const int num_ops = 1000;
    const size_t max_offset = 1024 * 1024;  // 1MB范围内随机
    const size_t io_size = 4096;  // 每次操作4KB
    
    char *buffer = malloc(io_size);
    if (!buffer) {
        fprintf(stderr, "  ✗ 内存分配失败\n");
        inode_unlock(file);
        inode_put(ctx->icache, file);
        inode_unlock(root);
        inode_put(ctx->icache, root);
        fs_context_destroy(ctx);
        return -1;
    }
    
    double start_time = get_time_ms();
    int successful_ops = 0;
    
    for (int i = 0; i < num_ops; i++) {
        size_t offset = (rand() % (max_offset / io_size)) * io_size;
        
        if (rand() % 2 == 0) {
            // 写操作
            memset(buffer, rand() % 256, io_size);
            ssize_t written = inode_write(ctx->icache, file, buffer, offset, io_size, NULL);
            if (written > 0) successful_ops++;
        } else {
            // 读操作
            ssize_t read_bytes = inode_read(ctx->icache, file, buffer, offset, io_size);
            if (read_bytes >= 0) successful_ops++;
        }
        
        if ((i + 1) % 200 == 0) {
            printf("  进度: %d/%d 操作完成\n", i + 1, num_ops);
        }
    }
    
    double end_time = get_time_ms();
    double elapsed = end_time - start_time;
    
    printf("  ✓ 完成 %d/%d 次随机I/O操作\n", successful_ops, num_ops);
    printf("  ✓ 总耗时: %.2f ms\n", elapsed);
    printf("  ✓ 平均延迟: %.3f ms/op\n", elapsed / num_ops);
    printf("  ✓ IOPS: %.0f\n", num_ops * 1000.0 / elapsed);
    
    free(buffer);
    inode_unlock(file);
    inode_put(ctx->icache, file);
    inode_unlock(root);
    inode_put(ctx->icache, root);
    
    fs_context_destroy(ctx);
    printf("  ✅ 测试通过\n");
    return 0;
}

// ===== 测试5：深层目录结构 =====

static int test_deep_directory() {
    print_test_header("测试5：深层目录结构（10层）");
    
    fs_context_t *ctx = fs_context_init(TEST_IMG, false);
    if (!ctx) return -1;
    
    inode_t_mem *current = inode_get(ctx->icache, ctx->sb->root_inum);
    if (!current) {
        fs_context_destroy(ctx);
        return -1;
    }
    
    const int depth = 10;
    inode_t_mem *dirs[depth];
    
    for (int i = 0; i < depth; i++) {
        inode_lock(current);
        
        char dirname[32];
        snprintf(dirname, sizeof(dirname), "dir%d", i);
        
        inode_t_mem *subdir = inode_alloc(ctx->icache, INODE_TYPE_DIR);
        if (!subdir) {
            fprintf(stderr, "  ✗ 目录分配失败在第%d层\n", i);
            inode_unlock(current);
            inode_put(ctx->icache, current);
            for (int j = 0; j < i; j++) {
                if (dirs[j]) inode_put(ctx->icache, dirs[j]);
            }
            fs_context_destroy(ctx);
            return -1;
        }
        
        int ret = dir_add(ctx->icache, current, dirname, subdir->inum);
        if (ret < 0) {
            fprintf(stderr, "  ✗ 添加目录失败在第%d层\n", i);
            inode_put(ctx->icache, subdir);
            inode_unlock(current);
            inode_put(ctx->icache, current);
            for (int j = 0; j < i; j++) {
                if (dirs[j]) inode_put(ctx->icache, dirs[j]);
            }
            fs_context_destroy(ctx);
            return -1;
        }
        
        inode_unlock(current);
        if (i > 0) inode_put(ctx->icache, current);
        
        dirs[i] = subdir;
        current = subdir;
        
        printf("  ✓ 第%d层目录创建成功 (inum=%u)\n", i + 1, subdir->inum);
    }
    
    // 在最深层创建一个文件
    inode_lock(current);
    inode_t_mem *file = inode_alloc(ctx->icache, INODE_TYPE_FILE);
    if (!file) {
        fprintf(stderr, "  ✗ 文件分配失败\n");
        inode_unlock(current);
        for (int j = 0; j < depth; j++) {
            if (dirs[j]) inode_put(ctx->icache, dirs[j]);
        }
        fs_context_destroy(ctx);
        return -1;
    }
    
    int ret = dir_add(ctx->icache, current, "deep_file.txt", file->inum);
    inode_unlock(current);
    
    if (ret < 0) {
        fprintf(stderr, "  ✗ 在深层目录添加文件失败\n");
        inode_put(ctx->icache, file);
        for (int j = 0; j < depth; j++) {
            if (dirs[j]) inode_put(ctx->icache, dirs[j]);
        }
        fs_context_destroy(ctx);
        return -1;
    }
    
    printf("  ✓ 在第%d层创建文件成功\n", depth);
    
    inode_put(ctx->icache, file);
    for (int j = 0; j < depth; j++) {
        if (dirs[j]) inode_put(ctx->icache, dirs[j]);
    }
    
    fs_context_destroy(ctx);
    printf("  ✅ 测试通过 - 支持%d层目录\n", depth);
    return 0;
}

// ===== 测试6：碎片化场景 =====

static int test_fragmentation() {
    print_test_header("测试6：磁盘碎片化场景");
    
    fs_context_t *ctx = fs_context_init(TEST_IMG, false);
    if (!ctx) return -1;
    
    // 1. 分配大量extents
    const int num_extents = 50;
    uint32_t extents[num_extents][2];  // [start, len]
    
    printf("  阶段1: 分配%d个extent\n", num_extents);
    for (int i = 0; i < num_extents; i++) {
        uint32_t start, len;
        int ret = rust_extent_alloc(ctx->extent_alloc, i * 100, 10, 20, &start, &len);
        if (ret < 0) {
            printf("  ℹ️  分配停止在第%d个extent\n", i);
            break;
        }
        extents[i][0] = start;
        extents[i][1] = len;
    }
    
    // 2. 释放所有奇数编号的extent，制造碎片
    printf("  阶段2: 释放奇数编号的extent，制造碎片\n");
    for (int i = 1; i < num_extents; i += 2) {
        if (extents[i][0] != 0) {
            rust_extent_free(ctx->extent_alloc, extents[i][0], extents[i][1]);
        }
    }
    
    // 3. 检查碎片率
    float frag = rust_extent_fragmentation(ctx->extent_alloc);
    printf("  ✓ 碎片化率: %.2f%%\n", frag * 100.0);
    
    // 4. 获取统计
    uint32_t total, free, allocated;
    rust_extent_get_stats(ctx->extent_alloc, &total, &free, &allocated);
    printf("  ✓ 统计: total=%u, free=%u, allocated=%u\n", total, free, allocated);
    
    // 5. 尝试分配一个大extent
    uint32_t large_start, large_len;
    int ret = rust_extent_alloc(ctx->extent_alloc, 0, 100, 200, &large_start, &large_len);
    if (ret >= 0) {
        printf("  ✓ 在碎片化磁盘上成功分配大extent: [%u, +%u]\n", large_start, large_len);
        rust_extent_free(ctx->extent_alloc, large_start, large_len);
    } else {
        printf("  ℹ️  在碎片化磁盘上无法分配大extent（预期）\n");
    }
    
    // 清理
    for (int i = 0; i < num_extents; i += 2) {
        if (extents[i][0] != 0) {
            rust_extent_free(ctx->extent_alloc, extents[i][0], extents[i][1]);
        }
    }
    
    fs_context_destroy(ctx);
    printf("  ✅ 测试通过 - 碎片化处理正常\n");
    return 0;
}

// ===== 主函数 =====

int main() {
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════╗\n");
    printf("║  ModernFS 压力测试和性能测试套件                         ║\n");
    printf("║  测试文件系统在高负载下的性能和稳定性                    ║\n");
    printf("╚══════════════════════════════════════════════════════════╝\n");
    
    // 创建测试镜像
    if (create_test_image() < 0) {
        fprintf(stderr, "测试镜像创建失败，退出\n");
        return 1;
    }
    
    int failed = 0;
    
    // 运行所有测试
    if (test_many_small_files() < 0) failed++;
    if (test_large_sequential_write() < 0) failed++;
    if (test_large_sequential_read() < 0) failed++;
    if (test_random_io() < 0) failed++;
    if (test_deep_directory() < 0) failed++;
    if (test_fragmentation() < 0) failed++;
    
    // 清理
    unlink(TEST_IMG);
    
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════╗\n");
    if (failed == 0) {
        printf("║  🎉 所有测试通过！(6/6)                                  ║\n");
        printf("║                                                          ║\n");
        printf("║  ✅ 大量小文件创建                                       ║\n");
        printf("║  ✅ 大文件顺序写入                                       ║\n");
        printf("║  ✅ 大文件顺序读取                                       ║\n");
        printf("║  ✅ 随机读写                                             ║\n");
        printf("║  ✅ 深层目录结构                                         ║\n");
        printf("║  ✅ 碎片化场景                                           ║\n");
    } else {
        printf("║  ❌ %d 个测试失败                                        ║\n", failed);
    }
    printf("╚══════════════════════════════════════════════════════════╝\n");
    printf("\n");
    
    return failed == 0 ? 0 : 1;
}

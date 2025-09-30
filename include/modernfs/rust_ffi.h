#ifndef MODERNFS_RUST_FFI_H
#define MODERNFS_RUST_FFI_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============ Hello World测试 ============

/**
 * 测试Rust FFI - 返回字符串
 */
const char* rust_hello_world(void);

/**
 * 测试Rust FFI - 简单加法
 */
int32_t rust_add(int32_t a, int32_t b);

// ============ 不透明类型 ============

typedef struct RustJournalManager RustJournalManager;
typedef struct RustTransaction RustTransaction;
typedef struct RustExtentAllocator RustExtentAllocator;

// ============ Journal API ============

/**
 * 初始化日志管理器
 * @return NULL表示失败
 */
RustJournalManager* rust_journal_init(
    int device_fd,
    uint32_t journal_start,
    uint32_t journal_blocks
);

/**
 * 开始事务
 */
RustTransaction* rust_journal_begin(RustJournalManager* jm);

/**
 * 记录块写入
 * @param data 必须是BLOCK_SIZE(4096)字节
 * @return 0成功，负数为errno
 */
int rust_journal_write(
    RustTransaction* txn,
    uint32_t block_num,
    const uint8_t* data
);

/**
 * 提交事务（txn会被消费，不能再使用）
 */
int rust_journal_commit(
    RustJournalManager* jm,
    RustTransaction* txn
);

/**
 * 中止事务
 */
void rust_journal_abort(RustTransaction* txn);

/**
 * 检查点
 */
int rust_journal_checkpoint(RustJournalManager* jm);

/**
 * 崩溃恢复
 * @return 恢复的事务数量，负数为错误
 */
int rust_journal_recover(RustJournalManager* jm);

/**
 * 销毁
 */
void rust_journal_destroy(RustJournalManager* jm);

// ============ Extent Allocator API ============

RustExtentAllocator* rust_extent_alloc_init(
    int device_fd,
    uint32_t bitmap_start,
    uint32_t total_blocks
);

/**
 * 分配Extent
 */
int rust_extent_alloc(
    RustExtentAllocator* alloc,
    uint32_t hint,
    uint32_t min_len,
    uint32_t max_len,
    uint32_t* out_start,  // 输出参数
    uint32_t* out_len
);

/**
 * 释放Extent
 */
int rust_extent_free(
    RustExtentAllocator* alloc,
    uint32_t start,
    uint32_t len
);

/**
 * 获取碎片率（0.0-1.0）
 */
float rust_extent_fragmentation(RustExtentAllocator* alloc);

void rust_extent_alloc_destroy(RustExtentAllocator* alloc);

#ifdef __cplusplus
}
#endif

#endif // MODERNFS_RUST_FFI_H
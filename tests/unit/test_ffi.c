// FFI测试程序

#include "modernfs/rust_ffi.h"
#include <stdio.h>

int main(void) {
    printf("=== ModernFS FFI Test ===\n\n");

    // 测试1: 字符串返回
    printf("[Test 1] rust_hello_world()\n");
    const char* msg = rust_hello_world();
    printf("  Result: %s\n\n", msg);

    // 测试2: 简单计算
    printf("[Test 2] rust_add(42, 58)\n");
    int result = rust_add(42, 58);
    printf("  Result: %d\n", result);
    printf("  Expected: 100\n");
    if (result == 100) {
        printf("  ✓ PASSED\n\n");
    } else {
        printf("  ✗ FAILED\n\n");
        return 1;
    }

    // 测试3: Journal Manager占位符
    printf("[Test 3] Journal Manager placeholders\n");
    RustJournalManager* jm = rust_journal_init(-1, 0, 0);
    if (jm == NULL) {
        printf("  ✓ Correctly returned NULL (placeholder)\n");
    }
    rust_journal_destroy(jm);
    printf("\n");

    // 测试4: Extent Allocator占位符
    printf("[Test 4] Extent Allocator placeholders\n");
    RustExtentAllocator* alloc = rust_extent_alloc_init(-1, 0, 0);
    if (alloc == NULL) {
        printf("  ✓ Correctly returned NULL (placeholder)\n");
    }
    rust_extent_alloc_destroy(alloc);
    printf("\n");

    printf("=========================\n");
    printf("✅ All FFI tests passed!\n");
    printf("=========================\n");

    return 0;
}
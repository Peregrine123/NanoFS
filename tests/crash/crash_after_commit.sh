#!/bin/bash
# 崩溃测试场景2: 提交后未checkpoint即崩溃
# 验证已提交的事务在恢复后能正确重放

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
cd "$PROJECT_ROOT"

TEST_IMG="crash_test_commit.img"
TEST_SIZE="64M"

echo "╔════════════════════════════════════════╗"
echo "║  崩溃测试: 提交后未checkpoint          ║"
echo "╚════════════════════════════════════════╝"
echo ""

cleanup() {
    rm -f "$TEST_IMG" 2>/dev/null || true
}

trap cleanup EXIT

# 1. 创建文件系统
echo "[1/4] 创建测试镜像..."
./target/release/mkfs-modernfs "$TEST_IMG" --size "$TEST_SIZE" --force > /dev/null 2>&1
echo "  ✅ 镜像创建成功"

# 2. 提交多个事务但不checkpoint
echo ""
echo "[2/4] 提交多个事务（不执行checkpoint）..."

cat > /tmp/crash_commit_test.c <<'EOF'
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include "modernfs/rust_ffi.h"
#include "modernfs/superblock.h"

#define BLOCK_SIZE 4096

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <image>\n", argv[0]);
        return 1;
    }

    int fd = open(argv[1], O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    superblock_t sb;
    if (read_superblock(fd, &sb) != 0) {
        fprintf(stderr, "Failed to read superblock\n");
        close(fd);
        return 1;
    }

    RustJournalManager* jm = rust_journal_init(fd, sb.journal_start, sb.journal_blocks);
    if (!jm) {
        fprintf(stderr, "Failed to init journal\n");
        close(fd);
        return 1;
    }

    // 提交5个事务
    for (int txn_id = 0; txn_id < 5; txn_id++) {
        RustTransaction* txn = rust_journal_begin(jm);
        if (!txn) {
            fprintf(stderr, "Failed to begin transaction %d\n", txn_id);
            rust_journal_destroy(jm);
            close(fd);
            return 1;
        }

        // 每个事务写入3个块
        uint8_t data[BLOCK_SIZE];
        for (int i = 0; i < 3; i++) {
            uint32_t block_num = 6000 + txn_id * 3 + i;
            memset(data, 0xB0 + txn_id, BLOCK_SIZE);

            int ret = rust_journal_write(txn, block_num, data);
            if (ret != 0) {
                fprintf(stderr, "Failed to write block %u\n", block_num);
                rust_journal_abort(txn);
                rust_journal_destroy(jm);
                close(fd);
                return 1;
            }
        }

        // 提交事务
        int ret = rust_journal_commit(jm, txn);
        if (ret != 0) {
            fprintf(stderr, "Failed to commit transaction %d\n", txn_id);
            rust_journal_destroy(jm);
            close(fd);
            return 1;
        }

        printf("  ✅ 事务 %d 已提交 (blocks %d-%d)\n",
               txn_id, 6000 + txn_id * 3, 6000 + txn_id * 3 + 2);
    }

    printf("  ⚠️  模拟崩溃：5个事务已提交但未checkpoint\n");

    // 不执行checkpoint，直接退出
    close(fd);
    return 0;
}
EOF

gcc -o /tmp/crash_commit_test /tmp/crash_commit_test.c \
    -I"$PROJECT_ROOT/include" \
    -L"$PROJECT_ROOT/target/release" \
    "$PROJECT_ROOT/src/superblock.c" \
    "$PROJECT_ROOT/src/block_dev.c" \
    -lrust_core -lpthread -ldl -lm 2>/dev/null

/tmp/crash_commit_test "$TEST_IMG"
if [ $? -ne 0 ]; then
    echo "  ❌ 测试失败"
    exit 1
fi

# 3. 恢复
echo ""
echo "[3/4] 触发恢复..."

cat > /tmp/crash_commit_recover.c <<'EOF'
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include "modernfs/rust_ffi.h"
#include "modernfs/superblock.h"

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <image>\n", argv[0]);
        return 1;
    }

    int fd = open(argv[1], O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    superblock_t sb;
    if (read_superblock(fd, &sb) != 0) {
        close(fd);
        return 1;
    }

    RustJournalManager* jm = rust_journal_init(fd, sb.journal_start, sb.journal_blocks);
    if (!jm) {
        close(fd);
        return 1;
    }

    int recovered = rust_journal_recover(jm);
    printf("  恢复了 %d 个事务\n", recovered);

    if (recovered != 5) {
        fprintf(stderr, "  ⚠️  预期恢复5个事务，实际恢复 %d 个\n", recovered);
    }

    if (recovered >= 5) {
        printf("  ✅ 所有事务已恢复\n");
    }

    rust_journal_destroy(jm);
    close(fd);
    return recovered < 5 ? 1 : 0;
}
EOF

gcc -o /tmp/crash_commit_recover /tmp/crash_commit_recover.c \
    -I"$PROJECT_ROOT/include" \
    -L"$PROJECT_ROOT/target/release" \
    "$PROJECT_ROOT/src/superblock.c" \
    "$PROJECT_ROOT/src/block_dev.c" \
    -lrust_core -lpthread -ldl -lm 2>/dev/null

/tmp/crash_commit_recover "$TEST_IMG"
if [ $? -ne 0 ]; then
    echo "  ❌ 恢复失败"
    exit 1
fi

# 4. 验证
echo ""
echo "[4/4] 验证数据..."

cat > /tmp/crash_commit_verify.c <<'EOF'
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#define BLOCK_SIZE 4096

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <image>\n", argv[0]);
        return 1;
    }

    int fd = open(argv[1], O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    uint8_t data[BLOCK_SIZE];
    int errors = 0;

    // 验证5个事务的15个块
    for (int txn_id = 0; txn_id < 5; txn_id++) {
        for (int i = 0; i < 3; i++) {
            uint32_t block_num = 6000 + txn_id * 3 + i;
            off_t offset = (off_t)block_num * BLOCK_SIZE;

            if (pread(fd, data, BLOCK_SIZE, offset) != BLOCK_SIZE) {
                fprintf(stderr, "  ❌ 读取块 %u 失败\n", block_num);
                errors++;
                continue;
            }

            uint8_t expected = 0xB0 + txn_id;
            int block_ok = 1;
            for (int j = 0; j < BLOCK_SIZE; j++) {
                if (data[j] != expected) {
                    block_ok = 0;
                    break;
                }
            }

            if (!block_ok) {
                printf("  ❌ 块 %u: 数据错误\n", block_num);
                errors++;
            }
        }

        if (errors == 0) {
            printf("  ✅ 事务 %d: 所有数据正确\n", txn_id);
        }
    }

    close(fd);
    return errors > 0 ? 1 : 0;
}
EOF

gcc -o /tmp/crash_commit_verify /tmp/crash_commit_verify.c 2>/dev/null

/tmp/crash_commit_verify "$TEST_IMG"
if [ $? -ne 0 ]; then
    echo "  ❌ 数据验证失败"
    exit 1
fi

# 清理
rm -f /tmp/crash_commit_test /tmp/crash_commit_test.c
rm -f /tmp/crash_commit_recover /tmp/crash_commit_recover.c
rm -f /tmp/crash_commit_verify /tmp/crash_commit_verify.c

echo ""
echo "╔════════════════════════════════════════╗"
echo "║  测试结果: ✅ PASS                     ║"
echo "║  已提交事务在恢复后正确重放            ║"
echo "╚════════════════════════════════════════╝"

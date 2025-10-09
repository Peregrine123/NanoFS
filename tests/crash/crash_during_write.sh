#!/bin/bash
# 崩溃测试场景1: 写入过程中断
# 验证WAL日志的崩溃一致性

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
cd "$PROJECT_ROOT"

TEST_IMG="crash_test_write.img"
TEST_SIZE="64M"

echo "╔════════════════════════════════════════╗"
echo "║  崩溃测试: 写入过程中断                ║"
echo "╚════════════════════════════════════════╝"
echo ""

# 清理旧文件
cleanup() {
    rm -f "$TEST_IMG" 2>/dev/null || true
}

trap cleanup EXIT

# 1. 创建文件系统
echo "[1/5] 创建测试镜像..."
./target/release/mkfs-modernfs "$TEST_IMG" --size "$TEST_SIZE" --journal-size 8M --force > /dev/null 2>&1
if [ $? -ne 0 ]; then
    echo "  ❌ 创建镜像失败"
    exit 1
fi
echo "  ✅ 镜像创建成功"

# 2. 打开设备并开始事务
echo ""
echo "[2/5] 模拟写入事务..."

# 创建测试程序来模拟写入中断
cat > /tmp/crash_test_write.c <<'EOF'
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

    // 打开镜像
    int fd = open(argv[1], O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    // 读取超级块获取journal信息
    superblock_t sb;
    if (read_superblock(fd, &sb) != 0) {
        fprintf(stderr, "Failed to read superblock\n");
        close(fd);
        return 1;
    }

    // 初始化Journal Manager
    RustJournalManager* jm = rust_journal_init(
        fd,
        sb.journal_start,
        sb.journal_blocks
    );

    if (!jm) {
        fprintf(stderr, "Failed to init journal\n");
        close(fd);
        return 1;
    }

    // 开始事务
    RustTransaction* txn = rust_journal_begin(jm);
    if (!txn) {
        fprintf(stderr, "Failed to begin transaction\n");
        rust_journal_destroy(jm);
        close(fd);
        return 1;
    }

    // 写入10个块
    uint8_t data[BLOCK_SIZE];
    for (int i = 0; i < 10; i++) {
        memset(data, 0xAA + i, BLOCK_SIZE);
        int ret = rust_journal_write(txn, 5000 + i, data);
        if (ret != 0) {
            fprintf(stderr, "Failed to write block %d\n", i);
            rust_journal_abort(txn);
            rust_journal_destroy(jm);
            close(fd);
            return 1;
        }
        printf("  写入块 %d\n", 5000 + i);
    }

    // 提交事务
    printf("  提交事务...\n");
    int ret = rust_journal_commit(jm, txn);
    if (ret != 0) {
        fprintf(stderr, "Failed to commit transaction\n");
        rust_journal_destroy(jm);
        close(fd);
        return 1;
    }

    printf("  ✅ 事务已提交到日志\n");

    // 故意不执行checkpoint，模拟在checkpoint前崩溃
    printf("  ⚠️  模拟崩溃：未执行checkpoint直接退出\n");

    // 不调用rust_journal_destroy，直接退出
    // 这模拟了进程突然终止
    close(fd);
    return 0;
}
EOF

# 编译测试程序
echo "  编译测试程序..."
gcc -o /tmp/crash_test_write /tmp/crash_test_write.c \
    -I"$PROJECT_ROOT/include" \
    "$PROJECT_ROOT/src/superblock.c" \
    "$PROJECT_ROOT/src/block_dev.c" \
    "$PROJECT_ROOT/target/release/librust_core.a" \
    -lpthread -ldl -lm 2>/dev/null

if [ $? -ne 0 ]; then
    echo "  ❌ 编译失败"
    exit 1
fi

# 运行测试程序
/tmp/crash_test_write "$TEST_IMG"
if [ $? -ne 0 ]; then
    echo "  ❌ 测试程序失败"
    exit 1
fi

echo "  ✅ 事务已提交但未checkpoint"

# 3. 模拟恢复
echo ""
echo "[3/5] 模拟系统重启，触发恢复..."

cat > /tmp/crash_test_recover.c <<'EOF'
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
        fprintf(stderr, "Failed to read superblock\n");
        close(fd);
        return 1;
    }

    // 重新初始化Journal Manager（应自动触发恢复）
    RustJournalManager* jm = rust_journal_init(
        fd,
        sb.journal_start,
        sb.journal_blocks
    );

    if (!jm) {
        fprintf(stderr, "Failed to init journal\n");
        close(fd);
        return 1;
    }

    // 显式调用恢复
    int recovered = rust_journal_recover(jm);
    printf("  恢复了 %d 个事务\n", recovered);

    if (recovered < 0) {
        fprintf(stderr, "  ❌ 恢复失败\n");
        rust_journal_destroy(jm);
        close(fd);
        return 1;
    }

    printf("  ✅ 恢复成功\n");

    // 清理
    rust_journal_destroy(jm);
    close(fd);
    return 0;
}
EOF

gcc -o /tmp/crash_test_recover /tmp/crash_test_recover.c \
    -I"$PROJECT_ROOT/include" \
    "$PROJECT_ROOT/src/superblock.c" \
    "$PROJECT_ROOT/src/block_dev.c" \
    "$PROJECT_ROOT/target/release/librust_core.a" \
    -lpthread -ldl -lm 2>/dev/null

if [ $? -ne 0 ]; then
    echo "  ❌ 编译恢复程序失败"
    exit 1
fi

/tmp/crash_test_recover "$TEST_IMG"
if [ $? -ne 0 ]; then
    echo "  ❌ 恢复失败"
    exit 1
fi

# 4. 验证数据
echo ""
echo "[4/5] 验证数据完整性..."

cat > /tmp/crash_test_verify.c <<'EOF'
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

    // 验证10个块
    for (int i = 0; i < 10; i++) {
        uint32_t block_num = 5000 + i;
        off_t offset = (off_t)block_num * BLOCK_SIZE;

        if (pread(fd, data, BLOCK_SIZE, offset) != BLOCK_SIZE) {
            fprintf(stderr, "  ❌ 读取块 %u 失败\n", block_num);
            errors++;
            continue;
        }

        // 验证数据
        uint8_t expected = 0xAA + i;
        int block_ok = 1;
        for (int j = 0; j < BLOCK_SIZE; j++) {
            if (data[j] != expected) {
                block_ok = 0;
                break;
            }
        }

        if (block_ok) {
            printf("  ✅ 块 %u: 数据正确 (0x%02X)\n", block_num, expected);
        } else {
            printf("  ❌ 块 %u: 数据错误\n", block_num);
            errors++;
        }
    }

    close(fd);
    return errors > 0 ? 1 : 0;
}
EOF

gcc -o /tmp/crash_test_verify /tmp/crash_test_verify.c 2>/dev/null

/tmp/crash_test_verify "$TEST_IMG"
VERIFY_RESULT=$?

if [ $VERIFY_RESULT -eq 0 ]; then
    echo "  ✅ 所有数据完整"
else
    echo "  ❌ 数据验证失败"
    exit 1
fi

# 5. 清理
echo ""
echo "[5/5] 清理..."
rm -f /tmp/crash_test_write /tmp/crash_test_write.c
rm -f /tmp/crash_test_recover /tmp/crash_test_recover.c
rm -f /tmp/crash_test_verify /tmp/crash_test_verify.c
echo "  ✅ 清理完成"

echo ""
echo "╔════════════════════════════════════════╗"
echo "║  测试结果: ✅ PASS                     ║"
echo "║  WAL日志保证了崩溃一致性               ║"
echo "╚════════════════════════════════════════╝"

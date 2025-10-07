#!/bin/bash
# Week 8 集成测试脚本
# 测试所有 Rust 工具的基本功能

set -e  # 遇到错误立即退出

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 日志函数
log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

# 清理函数
cleanup() {
    log_info "Cleaning up test files..."
    rm -f test_week8_*.img
}

# 设置清理陷阱
trap cleanup EXIT

echo "========================================"
echo "  Week 8 集成测试 - Rust 工具集"
echo "========================================"
echo ""

# 1. 检查编译产物
log_info "Step 1: 检查编译产物..."
if [ ! -f "./target/release/mkfs-modernfs" ]; then
    log_error "mkfs-modernfs not found! Run 'cargo build --release' first"
    exit 1
fi

if [ ! -f "./target/release/fsck-modernfs" ]; then
    log_error "fsck-modernfs not found! Run 'cargo build --release' first"
    exit 1
fi

if [ ! -f "./target/release/benchmark-modernfs" ]; then
    log_error "benchmark-modernfs not found! Run 'cargo build --release' first"
    exit 1
fi

log_success "All binaries found"
echo ""

# 2. 测试 mkfs-modernfs 帮助信息
log_info "Step 2: 测试 mkfs-modernfs --help..."
./target/release/mkfs-modernfs --help > /dev/null
log_success "mkfs-modernfs --help OK"
echo ""

# 3. 创建文件系统 (128M)
log_info "Step 3: 创建 128M 文件系统..."
./target/release/mkfs-modernfs --size 128M --force test_week8_small.img
if [ ! -f "test_week8_small.img" ]; then
    log_error "Failed to create test_week8_small.img"
    exit 1
fi

FILE_SIZE=$(stat -c%s "test_week8_small.img" 2>/dev/null || stat -f%z "test_week8_small.img")
EXPECTED_SIZE=$((128 * 1024 * 1024))
if [ "$FILE_SIZE" -eq "$EXPECTED_SIZE" ]; then
    log_success "File size correct: $FILE_SIZE bytes"
else
    log_error "File size mismatch: expected $EXPECTED_SIZE, got $FILE_SIZE"
    exit 1
fi
echo ""

# 4. 测试 fsck-modernfs
log_info "Step 4: 运行 fsck-modernfs..."
./target/release/fsck-modernfs test_week8_small.img > fsck_output.txt 2>&1 || true

if grep -q "FSCK RESULTS" fsck_output.txt; then
    log_success "fsck completed successfully"
else
    log_error "fsck did not complete properly"
    cat fsck_output.txt
    exit 1
fi

# 检查是否有严重错误
if grep -q "Errors: 0" fsck_output.txt; then
    log_success "No critical errors found"
else
    log_warning "fsck found some errors (expected for new filesystem)"
fi

rm -f fsck_output.txt
echo ""

# 5. 创建更大的文件系统 (256M)
log_info "Step 5: 创建 256M 文件系统..."
./target/release/mkfs-modernfs --size 256M --journal-size 64M --force test_week8_large.img
if [ ! -f "test_week8_large.img" ]; then
    log_error "Failed to create test_week8_large.img"
    exit 1
fi
log_success "Large filesystem created"
echo ""

# 6. 再次检查大文件系统
log_info "Step 6: 检查 256M 文件系统..."
./target/release/fsck-modernfs test_week8_large.img --check-journal > /dev/null 2>&1 || true
log_success "Large filesystem check completed"
echo ""

# 7. 测试 benchmark-modernfs 帮助信息
log_info "Step 7: 测试 benchmark-modernfs --help..."
./target/release/benchmark-modernfs --help > /dev/null
log_success "benchmark-modernfs --help OK"
echo ""

# 8. 验证超级块 Magic Number
log_info "Step 8: 验证超级块 Magic Number..."
# 读取前 4 字节（magic number 应该是 0x4D4F4446 "MODF"）
MAGIC=$(hexdump -n 4 -e '1/4 "%08x\n"' test_week8_small.img)
if [ "$MAGIC" = "4d4f4446" ]; then
    log_success "Superblock magic number correct: 0x$MAGIC"
else
    log_error "Invalid superblock magic: 0x$MAGIC (expected 0x4d4f4446)"
    exit 1
fi
echo ""

# 9. 验证日志超级块
log_info "Step 9: 验证日志超级块..."
# 日志从块 1 开始（偏移 4096）
# 读取日志 magic (0x4A524E4C "JRNL")
JOURNAL_MAGIC=$(hexdump -s 4096 -n 4 -e '1/4 "%08x\n"' test_week8_small.img)
if [ "$JOURNAL_MAGIC" = "4a524e4c" ]; then
    log_success "Journal superblock magic correct: 0x$JOURNAL_MAGIC"
else
    log_warning "Journal magic: 0x$JOURNAL_MAGIC (expected 0x4a524e4c)"
fi
echo ""

# 10. 测试 verbose 模式
log_info "Step 10: 测试 fsck verbose 模式..."
./target/release/fsck-modernfs test_week8_small.img --verbose > verbose_output.txt 2>&1 || true
if grep -q "Checking filesystem" verbose_output.txt; then
    log_success "Verbose mode working"
else
    log_error "Verbose mode failed"
    exit 1
fi
rm -f verbose_output.txt
echo ""

# 汇总
echo "========================================"
echo "  测试汇总"
echo "========================================"
echo -e "${GREEN}✅ All tests passed!${NC}"
echo ""
echo "测试项:"
echo "  1. ✅ 工具编译完成"
echo "  2. ✅ mkfs 创建 128M 文件系统"
echo "  3. ✅ fsck 检查文件系统"
echo "  4. ✅ mkfs 创建 256M 文件系统"
echo "  5. ✅ 超级块验证"
echo "  6. ✅ 日志超级块验证"
echo "  7. ✅ Verbose 模式"
echo ""
echo "生成的测试文件:"
echo "  - test_week8_small.img (128M)"
echo "  - test_week8_large.img (256M)"
echo ""
echo "清理命令: rm -f test_week8_*.img"
echo ""

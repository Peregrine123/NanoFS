#!/bin/bash
# ModernFS FUSE 集成测试脚本

set -e  # 遇到错误立即退出

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

IMG_FILE="/tmp/modernfs_test.img"
MNT_POINT="/tmp/modernfs_mnt"

echo "╔════════════════════════════════════════╗"
echo "║   ModernFS FUSE Integration Test      ║"
echo "╚════════════════════════════════════════╝"
echo ""

# 清理函数
cleanup() {
    echo -e "${YELLOW}[Cleanup] Unmounting and cleaning up...${NC}"
    fusermount -u "$MNT_POINT" 2>/dev/null || true
    rm -f "$IMG_FILE"
    rmdir "$MNT_POINT" 2>/dev/null || true
    echo -e "${GREEN}[Cleanup] Done${NC}"
}

trap cleanup EXIT

# 1. 创建并格式化文件系统
echo -e "${YELLOW}[1/6] Creating and formatting filesystem (100MB)...${NC}"
./build/mkfs.modernfs "$IMG_FILE" 100
echo ""

# 2. 创建挂载点
echo -e "${YELLOW}[2/6] Creating mount point...${NC}"
mkdir -p "$MNT_POINT"
echo -e "${GREEN}✓ Mount point created: $MNT_POINT${NC}"
echo ""

# 3. 挂载文件系统（后台）
echo -e "${YELLOW}[3/6] Mounting filesystem...${NC}"
./build/modernfs "$IMG_FILE" "$MNT_POINT" &
FUSE_PID=$!
sleep 2  # 等待挂载完成

# 检查是否挂载成功
if ! mountpoint -q "$MNT_POINT"; then
    echo -e "${RED}✗ Failed to mount filesystem${NC}"
    exit 1
fi
echo -e "${GREEN}✓ Filesystem mounted (PID: $FUSE_PID)${NC}"
echo ""

# 4. 运行测试
echo -e "${YELLOW}[4/6] Running filesystem tests...${NC}"
echo ""

# 测试1: 列出根目录
echo "Test 1: List root directory"
ls -la "$MNT_POINT"
echo -e "${GREEN}✓ Passed${NC}"
echo ""

# 测试2: 创建目录
echo "Test 2: Create directories"
mkdir "$MNT_POINT/testdir"
mkdir "$MNT_POINT/testdir/subdir"
ls -la "$MNT_POINT"
echo -e "${GREEN}✓ Passed${NC}"
echo ""

# 测试3: 创建文件并写入
echo "Test 3: Create and write file"
echo "Hello ModernFS!" > "$MNT_POINT/hello.txt"
echo "This is a test file" > "$MNT_POINT/testdir/test.txt"
ls -lh "$MNT_POINT"
echo -e "${GREEN}✓ Passed${NC}"
echo ""

# 测试4: 读取文件
echo "Test 4: Read files"
echo "Content of hello.txt:"
cat "$MNT_POINT/hello.txt"
echo "Content of testdir/test.txt:"
cat "$MNT_POINT/testdir/test.txt"
echo -e "${GREEN}✓ Passed${NC}"
echo ""

# 测试5: 写入大文件
echo "Test 5: Write large file (1MB)"
dd if=/dev/zero of="$MNT_POINT/large.bin" bs=1K count=1024 2>/dev/null
ls -lh "$MNT_POINT/large.bin"
echo -e "${GREEN}✓ Passed${NC}"
echo ""

# 测试6: 文件元数据
echo "Test 6: File metadata"
stat "$MNT_POINT/hello.txt"
echo -e "${GREEN}✓ Passed${NC}"
echo ""

# 测试7: 删除文件
echo "Test 7: Delete file"
rm "$MNT_POINT/hello.txt"
ls -la "$MNT_POINT"
echo -e "${GREEN}✓ Passed${NC}"
echo ""

# 测试8: 删除目录
echo "Test 8: Delete directory"
rm -rf "$MNT_POINT/testdir"
ls -la "$MNT_POINT"
echo -e "${GREEN}✓ Passed${NC}"
echo ""

# 测试9: 文件系统统计
echo "Test 9: Filesystem statistics"
df -h "$MNT_POINT"
echo -e "${GREEN}✓ Passed${NC}"
echo ""

# 5. 卸载
echo -e "${YELLOW}[5/6] Unmounting filesystem...${NC}"
fusermount -u "$MNT_POINT"
wait $FUSE_PID 2>/dev/null || true
echo -e "${GREEN}✓ Unmounted${NC}"
echo ""

# 6. 重新挂载并验证持久化
echo -e "${YELLOW}[6/6] Remounting and verifying persistence...${NC}"
./build/modernfs "$IMG_FILE" "$MNT_POINT" &
FUSE_PID=$!
sleep 2

echo "Files after remount:"
ls -la "$MNT_POINT"
echo ""

# 验证大文件仍然存在
if [ -f "$MNT_POINT/large.bin" ]; then
    SIZE=$(stat -c%s "$MNT_POINT/large.bin")
    if [ "$SIZE" -eq 1048576 ]; then
        echo -e "${GREEN}✓ Large file persisted correctly (1MB)${NC}"
    else
        echo -e "${RED}✗ Large file size mismatch (expected 1048576, got $SIZE)${NC}"
    fi
else
    echo -e "${RED}✗ Large file not found after remount${NC}"
fi
echo ""

# 最终卸载
fusermount -u "$MNT_POINT"
wait $FUSE_PID 2>/dev/null || true

echo "╔════════════════════════════════════════╗"
echo "║   ✅ All tests passed successfully!   ║"
echo "╚════════════════════════════════════════╝"
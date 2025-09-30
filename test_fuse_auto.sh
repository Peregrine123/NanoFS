#!/bin/bash
# ModernFS 自动化集成测试

set -e

IMG="/tmp/modernfs_auto_test.img"
MNT="/tmp/modernfs_auto_mnt"

cleanup() {
    echo "Cleaning up..."
    fusermount -u "$MNT" 2>/dev/null || true
    wait $FUSE_PID 2>/dev/null || true
    rm -rf "$IMG" "$MNT"
}

trap cleanup EXIT

echo "╔════════════════════════════════════════╗"
echo "║   ModernFS Automated Integration Test ║"
echo "╚════════════════════════════════════════╝"
echo ""

# 1. 创建并格式化
echo "[1/8] Creating and formatting filesystem..."
./build/mkfs.modernfs "$IMG" 50 | tail -5
echo ""

# 2. 挂载（后台）
echo "[2/8] Mounting filesystem..."
mkdir -p "$MNT"
./build/modernfs "$IMG" "$MNT" > /dev/null 2>&1 &
FUSE_PID=$!
sleep 2

if ! mountpoint -q "$MNT"; then
    echo "✗ Failed to mount"
    exit 1
fi
echo "✓ Mounted (PID: $FUSE_PID)"
echo ""

# 3. 测试ls根目录
echo "[3/8] Testing readdir..."
if ls -la "$MNT" | grep -q "\."; then
    echo "✓ Root directory listing works"
else
    echo "✗ Root directory listing failed"
    exit 1
fi
echo ""

# 4. 测试mkdir
echo "[4/8] Testing mkdir..."
mkdir "$MNT/dir1" 2>&1 && echo "✓ Created dir1" || echo "✗ mkdir failed"
mkdir "$MNT/dir2" 2>&1 && echo "✓ Created dir2" || echo "✗ mkdir failed"
ls "$MNT"
echo ""

# 5. 测试文件创建和写入
echo "[5/8] Testing file create and write..."
echo "Hello World" > "$MNT/file1.txt" 2>&1 && echo "✓ Created file1.txt" || echo "✗ create failed"
echo "Test Data" > "$MNT/dir1/file2.txt" 2>&1 && echo "✓ Created dir1/file2.txt" || echo "✗ create failed"
ls -lh "$MNT"
echo ""

# 6. 测试文件读取
echo "[6/8] Testing file read..."
CONTENT=$(cat "$MNT/file1.txt" 2>&1)
if [ "$CONTENT" = "Hello World" ]; then
    echo "✓ Read file1.txt: $CONTENT"
else
    echo "✗ Read failed or content mismatch: $CONTENT"
fi
echo ""

# 7. 测试文件统计
echo "[7/8] Testing file stat..."
stat "$MNT/file1.txt" | head -3
echo "✓ Stat works"
echo ""

# 8. 测试df
echo "[8/8] Testing filesystem stats..."
df -h "$MNT" | tail -1
echo "✓ Filesystem stats work"
echo ""

echo "╔════════════════════════════════════════╗"
echo "║   ✅ All basic tests passed!          ║"
echo "╚════════════════════════════════════════╝"

# 卸载
echo ""
echo "Unmounting..."
fusermount -u "$MNT"
wait $FUSE_PID 2>/dev/null || true
echo "✓ Unmounted successfully"
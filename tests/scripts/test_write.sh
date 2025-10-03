#!/bin/bash
# 测试文件写入功能

set -e

IMG="/tmp/write_test.img"
MNT="/tmp/write_mnt"

cleanup() {
    fusermount -u "$MNT" 2>/dev/null || true
    rm -rf "$IMG" "$MNT"
}

trap cleanup EXIT

echo "=== 创建文件系统 ==="
./build/mkfs.modernfs "$IMG" 10

echo ""
echo "=== 挂载文件系统 ==="
mkdir -p "$MNT"
./build/modernfs "$IMG" "$MNT" &
FUSE_PID=$!
sleep 2

echo ""
echo "=== 测试1: 使用echo写入 ==="
echo "Hello World" > "$MNT/test.txt"
echo "文件大小: $(stat -c%s "$MNT/test.txt")"
echo "文件内容: $(cat "$MNT/test.txt" 2>&1 || echo '读取失败')"

echo ""
echo "=== 测试2: 使用printf写入 ==="
printf "Test Data\n" > "$MNT/test2.txt"
echo "文件大小: $(stat -c%s "$MNT/test2.txt")"
echo "文件内容: $(cat "$MNT/test2.txt" 2>&1 || echo '读取失败')"

echo ""
echo "=== 测试3: 查看inode信息 ==="
stat "$MNT/test.txt"

echo ""
echo "=== 卸载 ==="
fusermount -u "$MNT"
wait $FUSE_PID 2>/dev/null || true
echo "完成"
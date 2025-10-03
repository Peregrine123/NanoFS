#!/bin/bash
# 测试文件写入 - 保持挂载状态检查

set -e

IMG="/tmp/write_debug.img"
MNT="/tmp/write_debug_mnt"

cleanup() {
    echo "清理中..."
    fusermount -u "$MNT" 2>/dev/null || true
    # 不删除IMG，用于后续检查
}

trap cleanup EXIT

rm -f "$IMG"
mkdir -p "$MNT"

echo "=== 1. 创建并格式化文件系统 ==="
./build/mkfs.modernfs "$IMG" 10 | tail -5

echo ""
echo "=== 2. 挂载文件系统 ==="
./build/modernfs "$IMG" "$MNT" &
FUSE_PID=$!
sleep 2

if ! mountpoint -q "$MNT"; then
    echo "挂载失败"
    exit 1
fi
echo "挂载成功 (PID: $FUSE_PID)"

echo ""
echo "=== 3. 写入测试文件 ==="
echo "Hello World" > "$MNT/test.txt"
sync
sleep 1

echo ""
echo "=== 4. 检查文件状态(挂载时) ==="
ls -lh "$MNT/test.txt"
stat "$MNT/test.txt" | grep -E "Size|Inode"
echo "尝试读取: '$(cat "$MNT/test.txt" 2>&1)'"

echo ""
echo "=== 5. 卸载 ==="
fusermount -u "$MNT"
wait $FUSE_PID 2>/dev/null || true

echo ""
echo "=== 6. 重新挂载检查持久化 ==="
./build/modernfs "$IMG" "$MNT" &
FUSE_PID=$!
sleep 2

ls -lh "$MNT/"
if [ -f "$MNT/test.txt" ]; then
    echo "文件存在，大小: $(stat -c%s "$MNT/test.txt")"
    echo "内容: '$(cat "$MNT/test.txt" 2>&1)'"
else
    echo "文件不存在"
fi

fusermount -u "$MNT"
wait $FUSE_PID 2>/dev/null || true

# 不删除IMG
trap - EXIT
echo "镜像保存在: $IMG"
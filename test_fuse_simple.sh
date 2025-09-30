#!/bin/bash
# 简化的FUSE测试

set -e

IMG="/tmp/test_simple.img"
MNT="/tmp/test_mnt"

cleanup() {
    fusermount -u "$MNT" 2>/dev/null || true
    rm -rf "$IMG" "$MNT"
}

trap cleanup EXIT

echo "=== Creating filesystem ==="
./build/mkfs.modernfs "$IMG" 10
echo ""

echo "=== Creating mount point ==="
mkdir -p "$MNT"
echo ""

echo "=== Mounting (foreground, 10 seconds) ==="
timeout 10 ./build/modernfs "$IMG" "$MNT" -f -d || true
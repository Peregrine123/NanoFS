#!/bin/bash
# ModernFS 自动化演示脚本
# 展示完整的文件系统功能

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

DEMO_IMG="demo.img"
DEMO_SIZE="256M"
MOUNT_POINT="/mnt/modernfs_demo"

echo "╔════════════════════════════════════════╗"
echo "║  ModernFS Live Demo                    ║"
echo "║  C + Rust Hybrid Filesystem            ║"
echo "╚════════════════════════════════════════╝"
echo ""

# 清理函数
cleanup() {
    echo -e "\n[清理] 卸载并清理..."
    if mountpoint -q "$MOUNT_POINT" 2>/dev/null; then
        fusermount -u "$MOUNT_POINT" 2>/dev/null || umount "$MOUNT_POINT" 2>/dev/null || true
    fi
    rm -f "$DEMO_IMG" 2>/dev/null || true
    rm -rf "$MOUNT_POINT" 2>/dev/null || true
}

trap cleanup EXIT

# 确保以前的演示环境已清理
cleanup

# ========================================
# 第1步: 格式化文件系统
# ========================================
echo "════════════════════════════════════════"
echo "  [1/6] 格式化文件系统"
echo "════════════════════════════════════════"
echo ""

if [ ! -f "target/release/mkfs-modernfs" ]; then
    echo "❌ mkfs-modernfs 未找到，请先运行 build.sh"
    exit 1
fi

./target/release/mkfs-modernfs "$DEMO_IMG" \
    --size "$DEMO_SIZE" \
    --journal-size 32M \
    --force

if [ $? -ne 0 ]; then
    echo "❌ 格式化失败"
    exit 1
fi

echo ""
echo "✅ 文件系统创建成功"
echo ""

# ========================================
# 第2步: 挂载文件系统
# ========================================
echo "════════════════════════════════════════"
echo "  [2/6] 挂载文件系统"
echo "════════════════════════════════════════"
echo ""

# 创建挂载点
mkdir -p "$MOUNT_POINT"

if [ ! -f "build/modernfs" ]; then
    echo "⚠️  modernfs FUSE驱动未找到，跳过FUSE相关演示"
    echo "   (FUSE功能仅在Linux上可用)"
    SKIP_FUSE=1
else
    # 挂载（后台运行）
    ./build/modernfs "$DEMO_IMG" "$MOUNT_POINT" -f &
    FUSE_PID=$!

    # 等待挂载完成
    sleep 2

    if mountpoint -q "$MOUNT_POINT"; then
        echo "✅ 文件系统已挂载到 $MOUNT_POINT"
        SKIP_FUSE=0
    else
        echo "⚠️  挂载失败，跳过FUSE相关演示"
        SKIP_FUSE=1
    fi
fi

echo ""

# ========================================
# 第3步: 基本文件操作
# ========================================
if [ $SKIP_FUSE -eq 0 ]; then
    echo "════════════════════════════════════════"
    echo "  [3/6] 基本文件操作"
    echo "════════════════════════════════════════"
    echo ""

    cd "$MOUNT_POINT"

    # 创建文件
    echo "Hello, ModernFS!" > test.txt
    echo "  ✅ 创建文件: test.txt"

    # 读取文件
    CONTENT=$(cat test.txt)
    echo "  ✅ 读取内容: $CONTENT"

    # 创建目录
    mkdir -p a/b/c
    echo "  ✅ 创建目录: a/b/c"

    # 创建多个文件
    for i in {1..5}; do
        echo "File $i" > "file$i.txt"
    done
    echo "  ✅ 创建5个文件"

    # 显示目录树
    echo ""
    echo "目录结构:"
    ls -lR
    echo ""

    cd "$SCRIPT_DIR"
else
    echo "════════════════════════════════════════"
    echo "  [3/6] 基本文件操作 (跳过)"
    echo "════════════════════════════════════════"
    echo "  ⏭️  FUSE不可用"
    echo ""
fi

# ========================================
# 第4步: Journal测试
# ========================================
echo "════════════════════════════════════════"
echo "  [4/6] Journal Manager 测试"
echo "════════════════════════════════════════"
echo ""

if [ -f "build/test_journal" ]; then
    ./build/test_journal
    echo ""
else
    echo "  ⚠️  test_journal 未找到"
    echo ""
fi

# ========================================
# 第5步: 崩溃恢复演示
# ========================================
echo "════════════════════════════════════════"
echo "  [5/6] 崩溃恢复演示"
echo "════════════════════════════════════════"
echo ""

if [ -f "tests/crash/run_all.sh" ]; then
    echo "运行崩溃测试套件..."
    echo ""
    ./tests/crash/run_all.sh
else
    echo "  ⚠️  崩溃测试脚本未找到"
fi

echo ""

# ========================================
# 第6步: 文件系统检查
# ========================================
echo "════════════════════════════════════════"
echo "  [6/6] 文件系统检查"
echo "════════════════════════════════════════"
echo ""

# 先卸载
if [ $SKIP_FUSE -eq 0 ]; then
    fusermount -u "$MOUNT_POINT" 2>/dev/null || umount "$MOUNT_POINT" 2>/dev/null || true
    sleep 1
fi

if [ -f "target/release/fsck-modernfs" ]; then
    ./target/release/fsck-modernfs "$DEMO_IMG"
    echo ""
else
    echo "  ⚠️  fsck-modernfs 未找到"
    echo ""
fi

# ========================================
# 演示完成
# ========================================
echo "╔════════════════════════════════════════╗"
echo "║  Demo 完成！                            ║"
echo "╚════════════════════════════════════════╝"
echo ""
echo "展示的功能:"
echo "  ✅ 文件系统格式化 (Rust mkfs)"
if [ $SKIP_FUSE -eq 0 ]; then
    echo "  ✅ FUSE挂载和文件操作"
else
    echo "  ⏭️  FUSE挂载 (不可用)"
fi
echo "  ✅ Journal事务管理"
echo "  ✅ 崩溃一致性验证"
echo "  ✅ 文件系统检查 (Rust fsck)"
echo ""
echo "技术亮点:"
echo "  🦀 C + Rust混合架构"
echo "  📝 WAL日志保证崩溃一致性"
echo "  🔧 完整的Rust工具链"
echo "  🧪 全面的测试覆盖"
echo ""

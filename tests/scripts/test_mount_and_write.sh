#!/bin/bash
# 测试文件系统挂载和基本读写操作
# 模拟用户的实际使用场景

set -e  # 遇到错误立即退出

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# 配置
DISK_IMAGE="test_mount.img"
MOUNT_POINT="/mnt/modernfs_test"
MKFS="./build/mkfs.modernfs"
MODERNFS="./build/modernfs"

echo "=========================================="
echo "ModernFS 挂载和读写测试"
echo "=========================================="

# 清理函数
cleanup() {
    echo -e "\n${YELLOW}[清理] 清理测试环境...${NC}"

    # 卸载文件系统
    if mountpoint -q "$MOUNT_POINT" 2>/dev/null; then
        echo "[清理] 卸载文件系统..."
        sudo fusermount -u "$MOUNT_POINT" 2>/dev/null || true
        sleep 1
    fi

    # 删除挂载点
    if [ -d "$MOUNT_POINT" ]; then
        echo "[清理] 删除挂载点..."
        sudo rmdir "$MOUNT_POINT" 2>/dev/null || true
    fi

    # 删除磁盘镜像
    if [ -f "$DISK_IMAGE" ]; then
        echo "[清理] 删除磁盘镜像..."
        rm -f "$DISK_IMAGE"
    fi

    echo -e "${GREEN}[清理] 完成${NC}"
}

# 注册清理函数
trap cleanup EXIT INT TERM

# 步骤1: 清理旧环境
echo -e "\n${YELLOW}[步骤1] 清理旧环境...${NC}"
cleanup

# 步骤2: 创建磁盘镜像
echo -e "\n${YELLOW}[步骤2] 创建磁盘镜像 (128MB)...${NC}"
if [ ! -f "$MKFS" ]; then
    echo -e "${RED}错误: mkfs.modernfs 不存在,请先执行 ./build.sh${NC}"
    exit 1
fi

$MKFS "$DISK_IMAGE" 128M
if [ $? -ne 0 ]; then
    echo -e "${RED}错误: 创建磁盘镜像失败${NC}"
    exit 1
fi
echo -e "${GREEN}✓ 磁盘镜像创建成功${NC}"

# 步骤3: 创建挂载点
echo -e "\n${YELLOW}[步骤3] 创建挂载点...${NC}"
sudo mkdir -p "$MOUNT_POINT"
echo -e "${GREEN}✓ 挂载点创建成功: $MOUNT_POINT${NC}"

# 步骤4: 挂载文件系统 (前台模式,便于看到日志)
echo -e "\n${YELLOW}[步骤4] 挂载文件系统 (5秒后开始测试)...${NC}"
if [ ! -f "$MODERNFS" ]; then
    echo -e "${RED}错误: modernfs 不存在,请先执行 ./build.sh${NC}"
    exit 1
fi

# 后台挂载,保存PID
sudo $MODERNFS "$DISK_IMAGE" "$MOUNT_POINT" -f -d > /tmp/modernfs.log 2>&1 &
FUSE_PID=$!
echo "FUSE PID: $FUSE_PID"

# 等待挂载完成
sleep 3

# 检查挂载是否成功
if ! mountpoint -q "$MOUNT_POINT"; then
    echo -e "${RED}错误: 文件系统挂载失败${NC}"
    echo "查看日志:"
    tail -20 /tmp/modernfs.log
    sudo kill -9 $FUSE_PID 2>/dev/null || true
    exit 1
fi
echo -e "${GREEN}✓ 文件系统挂载成功${NC}"

# 步骤5: 测试基本操作
echo -e "\n${YELLOW}[步骤5] 执行基本文件操作...${NC}"

# 测试1: 写入小文件
echo -e "\n[测试1] 写入小文件..."
TEST_FILE="$MOUNT_POINT/test.txt"
TEST_CONTENT="Hello, ModernFS!"

echo "$TEST_CONTENT" | sudo tee "$TEST_FILE" > /dev/null
if [ $? -ne 0 ]; then
    echo -e "${RED}✗ 写入失败${NC}"
    tail -20 /tmp/modernfs.log
    exit 1
fi
echo -e "${GREEN}✓ 写入成功${NC}"

# 测试2: 读取文件
echo -e "\n[测试2] 读取文件..."
READ_CONTENT=$(sudo cat "$TEST_FILE")
if [ "$READ_CONTENT" != "$TEST_CONTENT" ]; then
    echo -e "${RED}✗ 读取内容不匹配${NC}"
    echo "期望: $TEST_CONTENT"
    echo "实际: $READ_CONTENT"
    exit 1
fi
echo -e "${GREEN}✓ 读取成功,内容匹配${NC}"

# 测试3: 写入多个文件
echo -e "\n[测试3] 写入多个文件..."
for i in {1..10}; do
    FILE="$MOUNT_POINT/file_$i.txt"
    echo "This is file number $i" | sudo tee "$FILE" > /dev/null
    if [ $? -ne 0 ]; then
        echo -e "${RED}✗ 写入 file_$i.txt 失败${NC}"
        exit 1
    fi
done
echo -e "${GREEN}✓ 10个文件写入成功${NC}"

# 测试4: 列出目录
echo -e "\n[测试4] 列出目录..."
FILES=$(sudo ls -la "$MOUNT_POINT")
echo "$FILES"
FILE_COUNT=$(echo "$FILES" | grep -c "file_" || true)
if [ $FILE_COUNT -ne 10 ]; then
    echo -e "${RED}✗ 文件数量不对,期望10个,实际${FILE_COUNT}个${NC}"
    exit 1
fi
echo -e "${GREEN}✓ 目录列表正确${NC}"

# 测试5: 写入较大文件
echo -e "\n[测试5] 写入较大文件 (1MB)..."
LARGE_FILE="$MOUNT_POINT/large.txt"
sudo dd if=/dev/zero of="$LARGE_FILE" bs=1024 count=1024 2>/dev/null
if [ $? -ne 0 ]; then
    echo -e "${RED}✗ 写入大文件失败${NC}"
    tail -20 /tmp/modernfs.log
    exit 1
fi

LARGE_SIZE=$(sudo stat -c %s "$LARGE_FILE")
if [ $LARGE_SIZE -ne 1048576 ]; then
    echo -e "${RED}✗ 大文件大小不对,期望1048576,实际${LARGE_SIZE}${NC}"
    exit 1
fi
echo -e "${GREEN}✓ 大文件写入成功 (${LARGE_SIZE} bytes)${NC}"

# 测试6: 创建目录
echo -e "\n[测试6] 创建目录..."
TEST_DIR="$MOUNT_POINT/testdir"
sudo mkdir "$TEST_DIR"
if [ $? -ne 0 ]; then
    echo -e "${RED}✗ 创建目录失败${NC}"
    exit 1
fi

# 在目录中创建文件
sudo touch "$TEST_DIR/file_in_dir.txt"
if [ ! -f "$TEST_DIR/file_in_dir.txt" ]; then
    echo -e "${RED}✗ 在目录中创建文件失败${NC}"
    exit 1
fi
echo -e "${GREEN}✓ 目录创建和文件写入成功${NC}"

# 测试7: 删除文件
echo -e "\n[测试7] 删除文件..."
sudo rm "$MOUNT_POINT/file_1.txt"
if [ -f "$MOUNT_POINT/file_1.txt" ]; then
    echo -e "${RED}✗ 删除文件失败${NC}"
    exit 1
fi
echo -e "${GREEN}✓ 文件删除成功${NC}"

# 测试8: statfs
echo -e "\n[测试8] 查看文件系统状态..."
FS_STATS=$(sudo df -h "$MOUNT_POINT")
echo "$FS_STATS"
echo -e "${GREEN}✓ statfs成功${NC}"

# 步骤6: 检查Journal日志
echo -e "\n${YELLOW}[步骤6] 检查Journal日志...${NC}"
echo "最近20条日志:"
tail -20 /tmp/modernfs.log | grep -E "\[Journal\]|\[ExtentAllocator\]|Error|error" || echo "无错误信息"

# 步骤7: 卸载文件系统
echo -e "\n${YELLOW}[步骤7] 卸载文件系统...${NC}"
sudo fusermount -u "$MOUNT_POINT"
sleep 1

# 等待FUSE进程退出
wait $FUSE_PID 2>/dev/null || true
echo -e "${GREEN}✓ 文件系统卸载成功${NC}"

# 步骤8: 重新挂载验证数据持久性
echo -e "\n${YELLOW}[步骤8] 重新挂载验证数据持久性...${NC}"
sudo $MODERNFS "$DISK_IMAGE" "$MOUNT_POINT" -f -d > /tmp/modernfs2.log 2>&1 &
FUSE_PID2=$!
sleep 3

if ! mountpoint -q "$MOUNT_POINT"; then
    echo -e "${RED}错误: 重新挂载失败${NC}"
    tail -20 /tmp/modernfs2.log
    exit 1
fi

# 验证文件是否还在
if [ ! -f "$MOUNT_POINT/test.txt" ]; then
    echo -e "${RED}✗ test.txt 丢失${NC}"
    exit 1
fi

READ_CONTENT2=$(sudo cat "$MOUNT_POINT/test.txt")
if [ "$READ_CONTENT2" != "$TEST_CONTENT" ]; then
    echo -e "${RED}✗ 数据不一致${NC}"
    exit 1
fi

echo -e "${GREEN}✓ 数据持久性验证成功${NC}"

# 最终卸载
sudo fusermount -u "$MOUNT_POINT"
wait $FUSE_PID2 2>/dev/null || true

# 成功
echo -e "\n=========================================="
echo -e "${GREEN}所有测试通过! ✓${NC}"
echo -e "=========================================="

exit 0

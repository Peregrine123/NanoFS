#!/bin/bash

# run_full_test_suite.sh - 运行完整的测试套件
# 
# 运行所有Rust和C组件的测试，确保文件系统功能完整

set -e  # 遇到错误立即退出

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 计数器
TOTAL_TESTS=0
PASSED_TESTS=0
FAILED_TESTS=0

echo ""
echo "╔══════════════════════════════════════════════════════════╗"
echo "║  ModernFS 完整测试套件                                   ║"
echo "║  测试所有Rust和C组件的完整功能                          ║"
echo "╚══════════════════════════════════════════════════════════╝"
echo ""

# 函数：运行单个测试
run_test() {
    local test_name=$1
    local test_cmd=$2
    
    TOTAL_TESTS=$((TOTAL_TESTS + 1))
    
    echo ""
    echo -e "${BLUE}[测试 $TOTAL_TESTS]${NC} $test_name"
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    
    if $test_cmd; then
        echo -e "${GREEN}✅ 通过${NC}"
        PASSED_TESTS=$((PASSED_TESTS + 1))
        return 0
    else
        echo -e "${RED}❌ 失败${NC}"
        FAILED_TESTS=$((FAILED_TESTS + 1))
        return 1
    fi
}

# 1. 检查构建
echo -e "${YELLOW}步骤1: 检查构建系统${NC}"
if [ ! -d "build" ]; then
    echo "  ℹ️  build目录不存在，正在创建..."
    mkdir -p build
    cd build
    cmake .. || { echo -e "${RED}CMake配置失败${NC}"; exit 1; }
    cd ..
fi

echo "  ✓ 构建系统就绪"

# 2. 编译Rust库
echo ""
echo -e "${YELLOW}步骤2: 编译Rust核心库${NC}"
cargo build --release || { echo -e "${RED}Rust编译失败${NC}"; exit 1; }
echo "  ✓ Rust库编译成功"

# 3. 编译C代码和测试
echo ""
echo -e "${YELLOW}步骤3: 编译C代码和测试${NC}"
cd build
make -j$(nproc) || { echo -e "${RED}C代码编译失败${NC}"; exit 1; }
cd ..
echo "  ✓ C代码编译成功"

# 4. 运行测试
echo ""
echo -e "${YELLOW}步骤4: 运行测试套件${NC}"

# 基础测试
run_test "FFI接口测试" "./build/test_ffi"
run_test "块设备层测试" "./build/test_block_layer"
run_test "Inode层测试" "./build/test_inode_layer"

# Rust组件测试
run_test "Journal Manager测试 (Rust)" "./build/test_journal"
run_test "Extent Allocator测试 (Rust)" "./build/test_extent"

# 集成测试
run_test "Week 7集成测试 (Journal + Extent)" "./build/test_week7_integration"

# 新的全面测试
run_test "完整覆盖测试" "./build/test_full_coverage"
run_test "错误处理测试" "./build/test_error_handling"
run_test "压力测试" "./build/test_stress"

# 5. Rust单元测试
echo ""
echo -e "${BLUE}[Rust单元测试]${NC} Cargo test"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
TOTAL_TESTS=$((TOTAL_TESTS + 1))

if cargo test --manifest-path rust_core/Cargo.toml --release; then
    echo -e "${GREEN}✅ 通过${NC}"
    PASSED_TESTS=$((PASSED_TESTS + 1))
else
    echo -e "${RED}❌ 失败${NC}"
    FAILED_TESTS=$((FAILED_TESTS + 1))
fi

# 6. 总结
echo ""
echo "╔══════════════════════════════════════════════════════════╗"
echo "║  测试结果汇总                                            ║"
echo "╚══════════════════════════════════════════════════════════╝"
echo ""
echo "  总测试数:   $TOTAL_TESTS"
echo -e "  ${GREEN}通过:       $PASSED_TESTS${NC}"

if [ $FAILED_TESTS -gt 0 ]; then
    echo -e "  ${RED}失败:       $FAILED_TESTS${NC}"
else
    echo -e "  ${GREEN}失败:       $FAILED_TESTS${NC}"
fi

echo ""

# 覆盖率统计
echo "╔══════════════════════════════════════════════════════════╗"
echo "║  测试覆盖范围                                            ║"
echo "╚══════════════════════════════════════════════════════════╝"
echo ""
echo "  ✅ 块设备与缓存 (C)"
echo "  ✅ Inode与目录管理 (C)"
echo "  ✅ 路径解析 (C)"
echo "  ✅ Journal Manager (Rust)"
echo "  ✅ Extent Allocator (Rust)"
echo "  ✅ Rust/C FFI集成"
echo "  ✅ 文件系统上下文 (fs_context)"
echo "  ✅ 文件基本操作（创建、读、写、删除）"
echo "  ✅ 目录操作"
echo "  ✅ 边界条件（空文件、大文件）"
echo "  ✅ 崩溃一致性（事务恢复）"
echo "  ✅ 错误处理（磁盘满、无效参数）"
echo "  ✅ 压力测试（大量文件、随机I/O）"
echo "  ✅ 性能测试（读写吞吐量）"
echo ""

# 退出码
if [ $FAILED_TESTS -eq 0 ]; then
    echo "╔══════════════════════════════════════════════════════════╗"
    echo "║  🎉 所有测试通过！文件系统功能完整！                    ║"
    echo "╚══════════════════════════════════════════════════════════╝"
    echo ""
    exit 0
else
    echo "╔══════════════════════════════════════════════════════════╗"
    echo "║  ⚠️  部分测试失败，请检查日志                           ║"
    echo "╚══════════════════════════════════════════════════════════╝"
    echo ""
    exit 1
fi

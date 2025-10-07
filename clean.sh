#!/bin/bash
# ModernFS 项目清理脚本
# 清理所有生成的文件和临时文件

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "🧹 ModernFS 项目清理工具"
echo "=========================="
echo ""

# 清理选项
CLEAN_BUILD=true
CLEAN_RUST=true
CLEAN_TEST=true
CLEAN_IMAGES=true

# 解析参数
while [[ $# -gt 0 ]]; do
    case $1 in
        --no-build)
            CLEAN_BUILD=false
            shift
            ;;
        --no-rust)
            CLEAN_RUST=false
            shift
            ;;
        --no-test)
            CLEAN_TEST=false
            shift
            ;;
        --no-images)
            CLEAN_IMAGES=false
            shift
            ;;
        --help)
            echo "用法: $0 [选项]"
            echo ""
            echo "选项:"
            echo "  --no-build    不清理 C 构建文件 (build/)"
            echo "  --no-rust     不清理 Rust 构建文件 (target/)"
            echo "  --no-test     不清理测试生成的文件"
            echo "  --no-images   不清理 .img 镜像文件"
            echo "  --help        显示此帮助信息"
            echo ""
            exit 0
            ;;
        *)
            echo "❌ 未知选项: $1"
            echo "使用 --help 查看帮助"
            exit 1
            ;;
    esac
done

# 1. 清理 C 构建文件
if [ "$CLEAN_BUILD" = true ]; then
    echo "📦 清理 C 构建文件..."
    if [ -d "build" ]; then
        rm -rf build/
        echo "  ✅ 已删除 build/"
    else
        echo "  ⏭️  build/ 目录不存在"
    fi
fi

# 2. 清理 Rust 构建文件
if [ "$CLEAN_RUST" = true ]; then
    echo "🦀 清理 Rust 构建文件..."
    if [ -d "target" ]; then
        # 只删除 debug 和构建缓存，保留 release 二进制
        if [ -d "target/debug" ]; then
            rm -rf target/debug/
            echo "  ✅ 已删除 target/debug/"
        fi
        if [ -d "target/incremental" ]; then
            rm -rf target/incremental/
            echo "  ✅ 已删除 target/incremental/"
        fi
        # 完全清理（可选）
        # rm -rf target/
        # echo "  ✅ 已删除 target/"
    else
        echo "  ⏭️  target/ 目录不存在"
    fi

    # 清理 Cargo.lock（如果不想提交）
    # if [ -f "Cargo.lock" ]; then
    #     rm Cargo.lock
    #     echo "  ✅ 已删除 Cargo.lock"
    # fi
fi

# 3. 清理测试生成的文件
if [ "$CLEAN_TEST" = true ]; then
    echo "🧪 清理测试生成的文件..."

    # 清理测试二进制
    rm -f test_ffi test_block_layer test_inode_layer test_journal test_extent 2>/dev/null
    echo "  ✅ 已删除测试可执行文件"

    # 清理测试输出
    rm -f *.log *.txt 2>/dev/null || true
    echo "  ✅ 已删除日志文件"
fi

# 4. 清理镜像文件
if [ "$CLEAN_IMAGES" = true ]; then
    echo "💿 清理磁盘镜像文件..."

    IMG_FILES=$(find . -maxdepth 1 -name "*.img" 2>/dev/null || true)
    if [ -n "$IMG_FILES" ]; then
        rm -f *.img
        echo "  ✅ 已删除所有 .img 文件"
    else
        echo "  ⏭️  没有 .img 文件需要清理"
    fi
fi

# 5. 清理其他临时文件
echo "🗑️  清理其他临时文件..."
rm -f *~ *.swp *.swo 2>/dev/null || true
rm -f core core.* 2>/dev/null || true
echo "  ✅ 已删除临时文件"

echo ""
echo "=========================="
echo "✅ 清理完成！"
echo ""

# 显示剩余文件大小
if command -v du &> /dev/null; then
    echo "📊 剩余文件大小:"
    if [ "$CLEAN_RUST" = true ] && [ -d "target" ]; then
        echo "  target/: $(du -sh target 2>/dev/null | cut -f1)"
    fi
    if [ "$CLEAN_BUILD" = false ] && [ -d "build" ]; then
        echo "  build/:  $(du -sh build 2>/dev/null | cut -f1)"
    fi
fi

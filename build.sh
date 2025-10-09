#!/bin/bash

set -e

# 加载Rust环境变量
source ~/.cargo/env

echo "🔨 Building ModernFS..."

# 创建构建目录
mkdir -p build
cd build

# 配置CMake
cmake -DCMAKE_BUILD_TYPE=Release ..

# 编译
make -j$(nproc)

echo "✅ Build complete!"
echo "Binary: build/test_ffi"
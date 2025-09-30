@echo off
REM Windows构建脚本

echo 🔨 Building ModernFS...

REM 创建构建目录
if not exist build mkdir build
cd build

REM 配置CMake (使用Visual Studio或MinGW)
cmake -DCMAKE_BUILD_TYPE=Release -G "MinGW Makefiles" ..

REM 编译
cmake --build . --config Release

echo ✅ Build complete!
echo Binary: build\test_ffi.exe

cd ..
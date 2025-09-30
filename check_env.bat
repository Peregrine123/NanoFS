@echo off
REM 测试环境配置脚本

echo ========================================
echo ModernFS 环境检查
echo ========================================
echo.

REM 检查Rust
echo [1/4] 检查 Rust...
where cargo >nul 2>&1
if %errorlevel% neq 0 (
    echo ❌ Cargo 未找到
    echo 请安装 Rust: https://rustup.rs/
    echo 安装后重启终端
    goto :end
) else (
    cargo --version
    echo ✅ Rust OK
)
echo.

REM 检查CMake
echo [2/4] 检查 CMake...
where cmake >nul 2>&1
if %errorlevel% neq 0 (
    echo ❌ CMake 未找到
    echo 请安装 CMake: https://cmake.org/download/
    goto :end
) else (
    cmake --version | findstr /C:"version"
    echo ✅ CMake OK
)
echo.

REM 检查C编译器
echo [3/4] 检查 C编译器...
where gcc >nul 2>&1
if %errorlevel% neq 0 (
    where cl >nul 2>&1
    if %errorlevel% neq 0 (
        echo ❌ C编译器未找到
        echo 请安装 MinGW-w64 或 Visual Studio
        goto :end
    ) else (
        echo ✅ MSVC 编译器 OK
    )
) else (
    gcc --version | findstr /C:"gcc"
    echo ✅ GCC OK
)
echo.

REM 检查项目文件
echo [4/4] 检查项目文件...
if not exist "Cargo.toml" (
    echo ❌ Cargo.toml 未找到
    goto :end
)
if not exist "CMakeLists.txt" (
    echo ❌ CMakeLists.txt 未找到
    goto :end
)
if not exist "rust_core\src\lib.rs" (
    echo ❌ rust_core/src/lib.rs 未找到
    goto :end
)
echo ✅ 项目文件完整
echo.

echo ========================================
echo ✅ 环境检查通过！
echo ========================================
echo.
echo 下一步运行:
echo   build.bat
echo.

:end
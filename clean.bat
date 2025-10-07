@echo off
REM ModernFS 项目清理脚本 (Windows)
REM 清理所有生成的文件和临时文件

echo ============================================
echo    ModernFS 项目清理工具 (Windows)
echo ============================================
echo.

REM 1. 清理 C 构建文件
echo [1/4] 清理 C 构建文件...
if exist build\ (
    rmdir /s /q build
    echo   ✓ 已删除 build\
) else (
    echo   → build\ 目录不存在
)

REM 2. 清理 Rust 构建文件（部分）
echo [2/4] 清理 Rust 构建文件...
if exist target\debug\ (
    rmdir /s /q target\debug
    echo   ✓ 已删除 target\debug\
)
if exist target\incremental\ (
    rmdir /s /q target\incremental
    echo   ✓ 已删除 target\incremental\
)
REM 如果要完全清理，取消下面两行的注释
REM if exist target\ (
REM     rmdir /s /q target
REM     echo   ✓ 已删除 target\
REM )

REM 3. 清理测试生成的文件
echo [3/4] 清理测试生成的文件...
del /q *.exe 2>nul
del /q *.log 2>nul
del /q *.txt 2>nul
echo   ✓ 已删除测试文件和日志

REM 4. 清理镜像文件
echo [4/4] 清理磁盘镜像文件...
del /q *.img 2>nul
echo   ✓ 已删除所有 .img 文件

REM 5. 清理其他临时文件
del /q *~ 2>nul
del /q *.tmp 2>nul

echo.
echo ============================================
echo    清理完成！
echo ============================================
echo.

pause

# 快速开始指南

## Phase 0 环境搭建已完成 ✅

本项目是一个C+Rust混合架构的文件系统，用于操作系统课程大作业。

---

## 第一步：验证环境

### Windows用户
```cmd
check_env.bat
```

### Linux/macOS用户
```bash
chmod +x check_env.sh
./check_env.sh
```

如果看到 `✅ 环境检查通过！`，说明环境配置正确。

---

## 第二步：测试编译

由于你还需要配置Rust环境变量，请按照以下步骤：

### Windows
1. 关闭当前终端
2. 重新打开命令提示符或PowerShell
3. 验证Rust安装：
   ```cmd
   rustc --version
   cargo --version
   ```
4. 如果显示版本号，运行：
   ```cmd
   build.bat
   ```

### Linux/macOS
1. 加载Rust环境：
   ```bash
   source $HOME/.cargo/env
   ```
2. 验证安装：
   ```bash
   rustc --version
   cargo --version
   ```
3. 运行构建：
   ```bash
   chmod +x build.sh
   ./build.sh
   ```

---

## 第三步：运行FFI测试

编译成功后，运行测试程序：

**Windows**:
```cmd
build\test_ffi.exe
```

**Linux/macOS**:
```bash
./build/test_ffi
```

### 预期输出

```
=== ModernFS FFI Test ===

[Test 1] rust_hello_world()
  Result: Hello from Rust!

[Test 2] rust_add(42, 58)
  Result: 100
  Expected: 100
  ✓ PASSED

[Test 3] Journal Manager placeholders
  ✓ Correctly returned NULL (placeholder)

[Test 4] Extent Allocator placeholders
  ✓ Correctly returned NULL (placeholder)

=========================
✅ All FFI tests passed!
=========================
```

看到这个输出，说明 **Phase 0 完成！** 🎉

---

## 当前项目状态

```
✅ Phase 0 (Week 1) - 环境搭建
   ├─ Rust工具链配置
   ├─ 项目结构创建
   ├─ FFI接口设计
   └─ 测试程序验证

⏳ Phase 1 (Week 2-4) - C基础实现
   ├─ 块设备层
   ├─ Inode管理
   ├─ 目录操作
   └─ FUSE集成

⏳ Phase 2 (Week 5-7) - Rust核心模块
   ├─ WAL日志系统
   ├─ Extent分配器
   └─ 事务管理

⏳ Phase 3 (Week 8) - Rust工具集
   ├─ mkfs.modernfs
   ├─ fsck.modernfs
   └─ benchmark工具
```

---

## 常见问题

### Q: Rust命令找不到？
A:
- **Windows**: 重启终端，或手动添加 `%USERPROFILE%\.cargo\bin` 到PATH
- **Linux/macOS**: 运行 `source $HOME/.cargo/env`

### Q: CMake版本过低？
A: 需要CMake 3.20+
- **Windows**: 从 https://cmake.org/download/ 下载最新版
- **Ubuntu**: `sudo apt install cmake`
- **macOS**: `brew install cmake`

### Q: 编译错误？
A:
1. 确认Rust和C编译器都已安装
2. 运行 `check_env.bat` 或 `check_env.sh` 检查环境
3. 删除 `build/` 和 `target/` 目录后重新编译

### Q: Windows没有FUSE怎么办？
A:
- Phase 0和Phase 1部分开发不需要FUSE
- 可以在WSL中开发，或安装WinFsp
- 或者只测试FFI和核心模块，展示时在Linux虚拟机中运行

---

## 下一步

Phase 0完成后，开始Phase 1开发：

1. 阅读 `ModernFS_Hybrid_Plan.md` 中的 Phase 1 部分
2. 实现块设备IO层（`src/block_dev.c`）
3. 实现Inode管理（`src/inode.c`）

详细文档见：`docs/Phase0_Report.md`

---

**祝开发顺利！** 🚀
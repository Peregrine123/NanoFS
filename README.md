# ModernFS - C + Rust 混合文件系统

**项目代号**: ModernFS Hybrid
**版本**: 0.1.0
**架构**: C (FUSE接口) + Rust (核心模块)

## 环境要求

### Windows
- **Rust**: 从 https://rustup.rs/ 安装
- **C编译器**: MinGW-w64 或 Visual Studio
- **CMake**: 3.20+
- **Git Bash**: 用于运行脚本

### Linux
- **Rust**: `curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh`
- **C编译器**: `sudo apt install build-essential`
- **CMake**: `sudo apt install cmake`
- **FUSE**: `sudo apt install libfuse3-dev`

## 快速开始

### 1. 安装Rust后配置环境变量

**Windows** (重启终端后执行):
```cmd
rustc --version
cargo --version
```

如果显示"command not found"，手动添加到PATH:
```
%USERPROFILE%\.cargo\bin
```

**Linux/macOS**:
```bash
source $HOME/.cargo/env
rustc --version
cargo --version
```

### 2. 构建项目

**Windows**:
```cmd
build.bat
```

**Linux/macOS**:
```bash
chmod +x build.sh
./build.sh
```

### 3. 运行FFI测试

**Windows**:
```cmd
build\test_ffi.exe
```

**Linux/macOS**:
```bash
./build/test_ffi
```

预期输出:
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

## 项目结构

```
modernfs/
├── Cargo.toml              # Rust workspace配置
├── CMakeLists.txt          # 顶层CMake
├── build.sh / build.bat    # 构建脚本
├── rust_core/              # Rust核心库
│   ├── Cargo.toml
│   └── src/
│       ├── lib.rs
│       ├── journal/        # WAL日志系统
│       ├── extent/         # Extent分配器
│       └── transaction/    # 事务管理
├── src/                    # C源代码
│   └── test_ffi.c          # FFI测试
├── include/
│   └── modernfs/
│       └── rust_ffi.h      # Rust FFI接口
├── tools/                  # Rust工具
│   ├── mkfs-rs/
│   ├── fsck-rs/
│   └── benchmark-rs/
└── tests/
    ├── unit/
    ├── integration/
    └── crash/
```

## 开发路线图

- [x] **Phase 0 (Week 1)**: 环境搭建 ✅
  - [x] Rust工具链
  - [x] 项目结构
  - [x] FFI测试
- [ ] **Phase 1 (Week 2-4)**: C基础实现
  - [ ] 块设备层
  - [ ] Inode管理
  - [ ] 目录管理
  - [ ] FUSE集成
- [ ] **Phase 2 (Week 5-7)**: Rust核心模块
  - [ ] Journal Manager (WAL)
  - [ ] Extent Allocator
  - [ ] FFI集成
- [ ] **Phase 3 (Week 8)**: Rust工具集
  - [ ] mkfs.modernfs
  - [ ] fsck.modernfs
  - [ ] benchmark工具

## 技术亮点

1. ⭐ **内存安全**: Rust编译器保证，零运行时开销
2. ⭐ **并发安全**: 类型系统防止数据竞争
3. ⭐ **崩溃一致性**: WAL日志 + 自动恢复
4. ⭐ **C/Rust混合**: FFI接口，发挥各自优势

## 故障排除

### Rust未找到
```bash
# 检查安装
rustc --version

# 如果失败，重新加载环境
source $HOME/.cargo/env  # Linux/macOS
# 或重启终端 (Windows)
```

### CMake错误
确保CMake版本 >= 3.20:
```bash
cmake --version
```

### 链接错误
Windows可能需要安装Visual Studio Build Tools或MinGW-w64。

## 下一步

完成Phase 0后，进入Phase 1开发：
1. 实现块设备IO层（`src/block_dev.c`）
2. 实现Inode管理（`src/inode.c`）
3. 实现目录管理（`src/directory.c`）

## 参考资料

- [FUSE文档](https://libfuse.github.io/)
- [Rust FFI指南](https://doc.rust-lang.org/nomicon/ffi.html)
- [xv6文件系统](https://pdos.csail.mit.edu/6.828/2020/xv6/book-riscv-rev1.pdf)

## 许可证

MIT License (待定)
# ModernFS - C + Rust 混合文件系统

**项目代号**: ModernFS Hybrid
**版本**: 0.2.0
**架构**: C (FUSE接口 + 块设备层) + Rust (核心模块)
**当前状态**: Week 2 完成 ✅

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

### 3. 运行测试

#### FFI测试 (Week 1)

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

#### 块设备层测试 (Week 2) ✅

**Linux/macOS/WSL**:
```bash
./build/test_block_layer
```

预期输出:
```
╔════════════════════════════════════════╗
║  ModernFS Block Layer Test Suite      ║
║  Week 2: Block Device & Allocator     ║
╚════════════════════════════════════════╝

✅ Block device test passed
✅ Buffer cache test passed (hit rate: 66.67%)
✅ Block allocator test passed
✅ Concurrent access test passed
✅ Edge cases test passed

╔════════════════════════════════════════╗
║  ✅ All Tests Passed!                  ║
╚════════════════════════════════════════╝
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
│       ├── journal/        # WAL日志系统 (待实现)
│       ├── extent/         # Extent分配器 (待实现)
│       └── transaction/    # 事务管理 (待实现)
├── src/                    # C源代码
│   ├── test_ffi.c          # FFI测试
│   ├── block_dev.c         # 块设备层 ✅
│   ├── buffer_cache.c      # 缓冲区缓存 ✅
│   ├── block_alloc.c       # 块分配器 ✅
│   └── test_block_layer.c  # 块设备层测试 ✅
├── include/
│   └── modernfs/
│       ├── rust_ffi.h      # Rust FFI接口
│       ├── types.h         # 类型定义 ✅
│       ├── block_dev.h     # 块设备API ✅
│       ├── buffer_cache.h  # 缓存API ✅
│       └── block_alloc.h   # 分配器API ✅
├── tools/                  # Rust工具 (待实现)
│   ├── mkfs-rs/
│   ├── fsck-rs/
│   └── benchmark-rs/
└── tests/                  # 测试目录
    ├── unit/
    ├── integration/
    └── crash/
```

## 开发路线图

- [x] **Phase 0 (Week 1)**: 环境搭建 ✅
  - [x] Rust工具链
  - [x] 项目结构
  - [x] FFI测试
- [x] **Week 2**: 块设备层与分配器 ✅
  - [x] 块设备IO层 (block_dev.c, 270行)
  - [x] 缓冲区缓存 (buffer_cache.c, 362行)
  - [x] 块分配器 (block_alloc.c, 350行)
  - [x] 完整测试套件 (5个测试用例全部通过)
  - [x] 代码总计: 1705行
- [ ] **Week 3**: Inode与目录管理
  - [ ] Inode管理
  - [ ] 目录管理
  - [ ] 路径解析
- [ ] **Week 4**: FUSE集成
  - [ ] FUSE操作实现
  - [ ] 基础文件操作
- [ ] **Phase 2 (Week 5-7)**: Rust核心模块
  - [ ] Journal Manager (WAL)
  - [ ] Extent Allocator
  - [ ] FFI集成
- [ ] **Phase 3 (Week 8)**: Rust工具集
  - [ ] mkfs.modernfs
  - [ ] fsck.modernfs
  - [ ] benchmark工具

## 技术亮点

### 整体架构
1. ⭐ **内存安全**: Rust编译器保证，零运行时开销
2. ⭐ **并发安全**: 类型系统防止数据竞争
3. ⭐ **崩溃一致性**: WAL日志 + 自动恢复
4. ⭐ **C/Rust混合**: FFI接口，发挥各自优势

### Week 2 实现亮点
- ✅ **LRU缓存**: 哈希表(O(1)查找) + 双向链表(O(1)淘汰)
- ✅ **线程安全**: 读写锁保护缓冲区，互斥锁保护分配器
- ✅ **位图分配**: 高效的块分配/释放，支持连续块分配
- ✅ **缓存统计**: 命中率监控，便于性能调优
- ✅ **错误处理**: 完善的边界检查和错误传播
- ✅ **测试覆盖**: 5个测试套件，覆盖正常/并发/边界场景

### 性能指标 (Week 2)
- 缓存容量: 1024个块 (4MB)
- 哈希表大小: 2048桶
- 缓存命中率: 66.67% (测试场景)
- 块大小: 4KB
- 支持磁盘大小: 最大2^32个块 (16TB)

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

✅ **Week 2完成**: 块设备层、缓存、分配器全部实现并通过测试

**Week 3计划**: Inode与目录管理
1. 实现Inode管理（`src/inode.c`）
   - Inode分配/释放
   - Inode缓存
   - 数据块映射（直接/间接块）
2. 实现目录管理（`src/directory.c`）
   - 目录项管理
   - 查找/添加/删除目录项
3. 实现路径解析（`src/path.c`）
   - 路径分割
   - 递归查找

## 参考资料

- [FUSE文档](https://libfuse.github.io/)
- [Rust FFI指南](https://doc.rust-lang.org/nomicon/ffi.html)
- [xv6文件系统](https://pdos.csail.mit.edu/6.828/2020/xv6/book-riscv-rev1.pdf)

## 许可证

MIT License (待定)
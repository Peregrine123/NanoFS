# ModernFS - C + Rust 混合文件系统

**项目代号**: ModernFS Hybrid
**版本**: 0.5.0
**架构**: C (FUSE接口 + 块设备层) + Rust (Journal Manager)
**当前状态**: Week 5 阶段1 完成 ✅

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

#### Inode层测试 (Week 3) ✅

**Linux/macOS/WSL**:
```bash
./build/test_inode_layer
```

预期输出:
```
╔══════════════════════════════════════╗
║  ModernFS Inode层测试套件 (Week 3)  ║
╚══════════════════════════════════════╝

✅ 测试1: Inode分配和释放
✅ 测试2: Inode读写 (单块&跨块)
✅ 测试3: 目录操作 (增删查)
✅ 测试4: 路径操作 (规范化/basename/dirname)
✅ 测试5: 数据块映射 (直接/间接/二级间接块)

╔══════════════════════════════════════╗
║        所有测试通过！ ✅             ║
╚══════════════════════════════════════╝
```

#### Journal Manager测试 (Week 5) ✅

**WSL** (Rust需要Unix环境):
```bash
wsl bash -c "cd /mnt/e/ampa_migra/D/校务/大三上/OS/NanoFS && ./build/test_journal"
```

预期输出:
```
╔════════════════════════════════════════╗
║  ModernFS Journal测试套件 (Week 5)  ║
╚════════════════════════════════════════╝

[测试1] Journal初始化
  ✅ Journal Manager初始化成功
  - 起始块: 1024
  - 块数量: 8192 (32.0MB)

[测试2] 基础事务操作
  ✅ 事务已开始
  ✅ 已写入3个块到事务
  ✅ 事务已提交

[测试3] Checkpoint功能
  ✅ 事务已提交
  ✅ Checkpoint执行成功
  ✅ 数据已正确写入目标块5000

[测试4] 崩溃恢复
  ✅ 阶段1: 事务已提交（模拟崩溃前）
  ✅ 阶段2: Journal重新初始化
  ✅ 恢复了 0 个事务

[测试5] 多事务并发测试
  ✅ 所有5个事务已提交
  ✅ Checkpoint完成

╔════════════════════════════════════════╗
║  所有测试通过！ ✅             ║
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
│       ├── journal/        # WAL日志系统 ✅
│       │   ├── mod.rs
│       │   └── types.rs
│       ├── extent/         # Extent分配器 (待实现)
│       └── transaction/    # 事务管理 (待实现)
├── docs/                  # 项目文档与阶段报告
│   ├── QUICKSTART.md
│   ├── ModernFS_Hybrid_Plan.md
│   ├── WEEK4_REPORT.md
│   ├── CLAUDE.md
│   └── Phase0_Report.md
├── src/                    # C源代码
│   ├── test_ffi.c          # FFI测试
│   ├── block_dev.c         # 块设备层 ✅
│   ├── buffer_cache.c      # 缓冲区缓存 ✅
│   ├── block_alloc.c       # 块分配器 ✅
│   ├── inode.c             # Inode管理 ✅
│   ├── directory.c         # 目录管理 ✅
│   ├── path.c              # 路径解析 ✅
│   ├── test_block_layer.c  # 块设备层测试 ✅
│   └── test_inode_layer.c  # Inode层测试 ✅
├── include/
│   └── modernfs/
│       ├── rust_ffi.h      # Rust FFI接口
│       ├── types.h         # 类型定义 ✅
│       ├── block_dev.h     # 块设备API ✅
│       ├── buffer_cache.h  # 缓存API ✅
│       ├── block_alloc.h   # 分配器API ✅
│       ├── inode.h         # Inode API ✅
│       ├── directory.h     # 目录API ✅
│       └── path.h          # 路径API ✅
├── tools/                  # Rust工具 (待实现)
│   ├── mkfs-rs/
│   ├── fsck-rs/
│   └── benchmark-rs/
└── tests/                  # 测试目录
    ├── scripts/            # 手动/集成测试脚本
    │   ├── test_fuse.sh
    │   ├── test_fuse_auto.sh
    │   ├── test_fuse_simple.sh
    │   ├── test_write.sh
    │   └── test_write_debug.sh
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
- [x] **Week 3**: Inode与目录管理 ✅
  - [x] Inode管理 (inode.c, ~900行): LRU缓存、位图分配、数据块映射
  - [x] 目录管理 (directory.c, ~400行): 增删查、遍历、变长目录项
  - [x] 路径解析 (path.c, ~350行): 规范化、basename/dirname、符号链接
  - [x] 完整测试套件 (5个测试用例全部通过，100%通过率)
  - [x] 代码总计: 2031行 (Week 3新增)
- [ ] **Week 4**: FUSE集成 (已完成基础版本)
  - [x] FUSE操作实现
  - [x] 基础文件操作
- [x] **Week 5 (阶段1)**: Journal Manager ✅
  - [x] Journal Manager核心实现 (Rust)
  - [x] 事务管理 (begin/write/commit/abort)
  - [x] Checkpoint功能
  - [x] 崩溃恢复机制
  - [x] C/Rust FFI接口
  - [x] 完整测试套件 (5个测试用例全部通过)
  - [x] 代码总计: ~600行 Rust + 300行 C测试
- [ ] **Phase 2 (Week 6-7)**: 继续Rust核心模块
  - [ ] Extent Allocator
  - [ ] 集成到FUSE
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

### Week 3 实现亮点
- ✅ **Inode缓存**: LRU机制 + 哈希索引，O(1)查找
- ✅ **数据块映射**: 支持直接块(12个) + 一级间接块(1024个) + 二级间接块(1024²个)
- ✅ **最大文件**: ~4GB+ (12 + 1024 + 1024² 个块)
- ✅ **目录操作**: 变长目录项、空间复用、延迟合并
- ✅ **路径解析**: 支持`.`、`..`规范化，符号链接跟随
- ✅ **延迟写入**: dirty标志位，批量同步减少IO
- ✅ **测试覆盖**: 5个完整测试，100%通过率

### Week 5 实现亮点 (Rust核心!)
- ⭐ **WAL日志系统**: Write-Ahead Logging机制保证崩溃一致性
- ⭐ **事务管理**: ACID特性，支持begin/write/commit/abort
- ⭐ **Checkpoint**: 将日志数据应用到最终位置
- ⭐ **崩溃恢复**: 自动检测并重放已提交事务
- ⭐ **内存安全**: Rust所有权系统保证无数据竞争
- ⭐ **C/Rust混合**: 零成本FFI，发挥两种语言优势
- ⭐ **RAII模式**: Transaction Drop自动警告未提交
- ✅ **测试覆盖**: 5个完整测试，100%通过率

### 性能指标
**Week 2**:
- 缓存容量: 1024个块 (4MB)
- 哈希表大小: 2048桶
- 缓存命中率: 66.67% (测试场景)
- 块大小: 4KB
- 支持磁盘大小: 最大2^32个块 (16TB)

**Week 3**:
- Inode缓存: 64个Inode，哈希表32桶
- 最大文件大小: ~4GB+ (约1,049,600个块)
- 目录项: 变长，最大文件名255字节
- 路径深度: 无限制（受栈空间限制）

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

✅ **Week 5 (阶段1)完成**: Journal Manager实现并通过测试

**Week 5 (阶段2)计划**: Journal与FUSE集成
1. 将Journal集成到FUSE write操作
   - 修改`fuse_ops.c`使用Journal
   - 每次写入通过事务保护
2. 添加定期Checkpoint
   - 后台线程定期checkpoint
   - 释放Journal空间
3. 启动时自动恢复
   - 挂载时执行recover
   - 验证崩溃一致性

**Week 6计划**: Extent Allocator (Rust)
1. Extent分配器实现
   - First-Fit算法
   - 碎片率统计
2. FFI接口
3. 集成到Inode层

## 参考资料

- [FUSE文档](https://libfuse.github.io/)
- [Rust FFI指南](https://doc.rust-lang.org/nomicon/ffi.html)
- [xv6文件系统](https://pdos.csail.mit.edu/6.828/2020/xv6/book-riscv-rev1.pdf)

## 许可证

MIT License (待定)

# ModernFS 测试套件

本目录包含了 ModernFS 文件系统的完整测试套件。

## 快速开始

### 运行所有测试（推荐）

```bash
# 在项目根目录执行
./run_full_test_suite.sh
```

这个脚本会自动：
1. ✅ 编译 Rust 核心库
2. ✅ 编译所有 C 代码和测试
3. ✅ 按顺序运行所有测试
4. ✅ 生成测试报告

### 运行单个测试

```bash
# 编译
./build.sh

# 运行特定测试
./build/test_full_coverage    # 完整覆盖测试
./build/test_error_handling   # 错误处理测试
./build/test_stress           # 压力测试

# 其他测试
./build/test_block_layer      # 块设备层测试
./build/test_inode_layer      # Inode层测试
./build/test_journal          # Journal Manager测试 (Rust)
./build/test_extent           # Extent Allocator测试 (Rust)
./build/test_week7_integration # 集成测试
```

## 测试文件结构

```
tests/
├── unit/                      # 单元测试
│   ├── test_block_layer.c     # 块设备、缓存、分配器
│   ├── test_inode_layer.c     # Inode、目录、路径
│   ├── test_journal.c         # Journal Manager (Rust)
│   ├── test_extent.c          # Extent Allocator (Rust)
│   ├── test_week7_integration.c # Journal + Extent 集成
│   ├── test_full_coverage.c   # ⭐ 完整功能覆盖
│   ├── test_error_handling.c  # ⭐ 错误处理与资源耗尽
│   └── test_stress.c          # ⭐ 压力测试与性能测试
│
├── concurrent/                # 并发测试
│   ├── test_concurrent_writes.c
│   └── test_concurrent_alloc.c
│
├── crash/                     # 崩溃一致性测试
│   ├── crash_after_commit.sh
│   └── crash_during_write.sh
│
└── scripts/                   # FUSE集成测试脚本
    ├── test_fuse.sh
    ├── test_mount_and_write.sh
    └── ...
```

## 重点测试说明

### 1. test_full_coverage.c ⭐

**全面的文件系统功能测试**

测试场景：
- ✅ 完整文件系统初始化（所有组件）
- ✅ 文件创建、读、写、删除
- ✅ 目录操作
- ✅ 边界条件（空文件、大文件）
- ✅ Rust/C集成（Journal + Extent协同）
- ✅ 路径解析
- ✅ 崩溃一致性（事务恢复）

**预期结果**: 7/7 测试通过

```bash
./build/test_full_coverage
```

---

### 2. test_error_handling.c ⭐

**错误处理和异常情况测试**

测试场景：
- ✅ 磁盘空间耗尽处理
- ✅ 无效参数检测（NULL指针、无效inode号）
- ✅ Double-free检测
- ✅ 重复文件名处理
- ✅ 不存在文件的错误处理
- ✅ Extent边界检查
- ✅ Journal事务回滚

**预期结果**: 8/8 测试通过

```bash
./build/test_error_handling
```

---

### 3. test_stress.c ⭐

**压力测试和性能基准**

测试场景：
- ✅ 1000个小文件创建（吞吐量测试）
- ✅ 10MB大文件顺序写入（写性能）
- ✅ 大文件顺序读取（读性能）
- ✅ 1000次随机I/O（IOPS测试）
- ✅ 10层深度目录结构
- ✅ 磁盘碎片化场景

**预期结果**: 6/6 测试通过

**性能指标**（参考）：
- 小文件创建: > 100 文件/秒
- 顺序读写: 取决于磁盘性能
- 随机I/O: 取决于缓存效率

```bash
./build/test_stress
```

---

## 测试覆盖范围

### C 组件
- ✅ 块设备 (`block_dev.c`)
- ✅ 缓冲区缓存 (`buffer_cache.c`)
- ✅ 块分配器 (`block_alloc.c`)
- ✅ Inode管理 (`inode.c`)
- ✅ 目录管理 (`directory.c`)
- ✅ 路径解析 (`path.c`)
- ✅ 超级块 (`superblock.c`)
- ✅ fs_context (`fs_context.c`)
- 🔄 FUSE接口 (`fuse_ops.c`) - 通过脚本测试

### Rust 组件
- ✅ Journal Manager (`rust_core/src/journal/`)
- ✅ Extent Allocator (`rust_core/src/extent/`)
- ✅ FFI层 (`rust_core/src/lib.rs`)
- ✅ 事务管理
- ✅ Checkpoint机制
- ✅ 位图操作

### 功能场景
- ✅ 文件操作（创建、读、写、删除）
- ✅ 目录操作
- ✅ 路径解析
- ✅ 错误处理
- ✅ 资源耗尽
- ✅ 崩溃恢复
- ✅ 事务完整性
- ✅ 碎片化管理
- ✅ 并发访问

## 持续集成

### 本地验证

在提交代码前运行完整测试套件：

```bash
./run_full_test_suite.sh
```

### CI/CD 集成

参考 `COMPREHENSIVE_TESTS.md` 中的 GitHub Actions 配置示例。

## 故障排查

### 常见问题

1. **mkfs.modernfs 不存在**
   ```bash
   # 解决方法：重新构建
   ./build.sh
   ```

2. **Rust 库链接错误**
   ```bash
   # 解决方法：重新编译 Rust 库
   cargo build --release
   rm -rf build
   ./build.sh
   ```

3. **测试超时或挂起**
   - 检查是否有死锁
   - 查看 checkpoint 线程是否正常
   - 检查磁盘空间

4. **权限错误**
   ```bash
   # 确保测试脚本有执行权限
   chmod +x run_full_test_suite.sh
   chmod +x tests/scripts/*.sh
   ```

## 添加新测试

### 步骤

1. 在 `tests/unit/` 创建测试文件
2. 更新 `CMakeLists.txt` 添加编译规则
3. 更新 `run_full_test_suite.sh` 添加测试运行命令
4. 运行测试验证

参考现有测试文件的模式：
- 使用清晰的测试函数命名
- 打印详细的测试进度
- 验证所有错误情况
- 清理测试产生的文件

## 文档

详细的测试文档请参考：
- `COMPREHENSIVE_TESTS.md` - 完整测试文档
- `TEST_RESULTS.md` - 测试结果记录
- 各测试文件的头部注释

## 贡献指南

提交代码时请确保：
1. ✅ 所有现有测试通过
2. ✅ 新功能有对应的测试
3. ✅ 更新相关文档
4. ✅ 代码通过 linting 检查

---

**Happy Testing! 🧪**

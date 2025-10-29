# ModernFS 测试清单

本文档提供了一个简明的清单，用于验证 ModernFS 文件系统的所有测试是否正常工作。

## ✅ 快速验证清单

### 1. 环境检查

- [ ] Rust 工具链已安装
  ```bash
  rustc --version
  cargo --version
  ```

- [ ] C 编译器已安装
  ```bash
  gcc --version
  cmake --version
  ```

- [ ] 项目已克隆/下载

---

### 2. 构建验证

- [ ] Rust 库编译成功
  ```bash
  cargo build --release
  # 应该看到: Finished `release` profile [optimized]
  ```

- [ ] C 代码编译成功
  ```bash
  ./build.sh
  # 或者
  mkdir -p build && cd build && cmake .. && make -j4
  ```

- [ ] 所有测试可执行文件存在
  ```bash
  ls -1 build/test_*
  # 应该列出 13 个测试文件
  ```

---

### 3. 基础测试验证

#### 3.1 FFI 接口测试

- [ ] 运行测试
  ```bash
  ./build/test_ffi
  ```

- [ ] 预期输出包含
  - `Hello from Rust!`
  - `✅ All FFI tests passed!`

---

#### 3.2 块设备层测试

- [ ] 运行测试
  ```bash
  ./build/test_block_layer
  ```

- [ ] 预期输出包含
  - `✅ Block device test passed`
  - `✅ Buffer cache test passed`
  - `✅ Block allocator test passed`
  - `✅ All Tests Passed!`

---

#### 3.3 Inode 层测试

- [ ] 运行测试
  ```bash
  ./build/test_inode_layer
  ```

- [ ] 预期输出包含
  - `✅ 测试1通过` (Inode 分配和释放)
  - `✅ 测试2通过` (Inode 读写)
  - `✅ 测试3通过` (目录操作)
  - `✅ 测试4通过` (路径操作)
  - `✅ 测试5通过` (数据块映射)

---

### 4. Rust 组件测试验证

#### 4.1 Journal Manager 测试

- [ ] 运行测试
  ```bash
  ./build/test_journal
  ```

- [ ] 预期输出包含
  - `✅ Journal Manager初始化成功`
  - `✅ 事务已提交`
  - `✅ Checkpoint执行成功`
  - `✅ 恢复了 X 个事务` (或 警告未恢复到事务)
  - `所有测试通过！ ✅`

---

#### 4.2 Extent Allocator 测试

- [ ] 运行测试
  ```bash
  ./build/test_extent
  ```

- [ ] 预期输出包含
  - `✅ Extent Allocator 初始化成功`
  - `✅ 分配成功`
  - `✅ 释放成功`
  - `✅ Double-free 被正确检测并拒绝`
  - `所有测试通过！ ✅`

---

### 5. 集成测试验证

#### 5.1 Week 7 集成测试

- [ ] 运行测试
  ```bash
  ./build/test_week7_integration
  ```

- [ ] 预期输出包含
  - `✓ fs_context初始化成功`
  - `✓ Journal Manager已初始化`
  - `✓ Extent Allocator已初始化`
  - `✓ Checkpoint线程已启动`
  - `所有测试通过！ ✅`

---

### 6. 新增全面测试验证 ⭐

#### 6.1 完整覆盖测试

- [ ] 运行测试
  ```bash
  ./build/test_full_coverage
  ```

- [ ] 预期输出包含
  - `测试1：完整文件系统初始化` ✅
  - `测试2：文件基本操作` ✅
  - `测试3：目录操作` ✅
  - `测试4：边界条件` ✅
  - `测试5：Rust/C集成` ✅
  - `测试6：路径解析` ✅
  - `测试7：崩溃一致性` ✅
  - `🎉 所有测试通过！(7/7)`

---

#### 6.2 错误处理测试

- [ ] 运行测试
  ```bash
  ./build/test_error_handling
  ```

- [ ] 预期输出包含
  - `测试1：磁盘空间耗尽` ✅
  - `测试2：无效参数检测` ✅
  - `测试3：Double-Free检测` ✅
  - `测试4：重复文件名检测` ✅
  - `测试5：读取不存在的文件` ✅
  - `测试6：删除不存在的文件` ✅
  - `测试7：Extent边界检查` ✅
  - `测试8：Journal回滚` ✅
  - `🎉 所有测试通过！(8/8)`

---

#### 6.3 压力测试

- [ ] 运行测试
  ```bash
  ./build/test_stress
  ```

- [ ] 预期输出包含
  - `测试1：大量小文件创建` ✅
  - `测试2：大文件顺序写入` ✅
  - `测试3：大文件顺序读取` ✅
  - `测试4：随机读写` ✅
  - `测试5：深层目录结构` ✅
  - `测试6：碎片化场景` ✅
  - `🎉 所有测试通过！(6/6)`

- [ ] 性能指标合理
  - 小文件创建速度 > 50 文件/秒
  - 随机 IOPS > 100
  - 没有崩溃或挂起

---

### 7. 完整测试套件验证 ⭐⭐⭐

#### 7.1 运行一键测试脚本

- [ ] 给脚本添加执行权限
  ```bash
  chmod +x run_full_test_suite.sh
  ```

- [ ] 运行完整测试套件
  ```bash
  ./run_full_test_suite.sh
  ```

- [ ] 预期输出包含
  - `步骤1: 检查构建系统` ✓
  - `步骤2: 编译Rust核心库` ✓
  - `步骤3: 编译C代码和测试` ✓
  - `步骤4: 运行测试套件`
  - 所有测试显示 `✅ 通过`

- [ ] 最终结果
  ```
  总测试数:   10
  通过:       10
  失败:       0
  
  🎉 所有测试通过！文件系统功能完整！
  ```

---

### 8. Rust 单元测试验证

- [ ] 运行 Rust 单元测试
  ```bash
  cargo test --manifest-path rust_core/Cargo.toml --release
  ```

- [ ] 所有测试通过
  - 没有 `FAILED` 输出
  - 看到 `test result: ok`

---

### 9. 文档验证

- [ ] 文档文件存在且内容正确
  - [ ] `COMPREHENSIVE_TESTS.md`
  - [ ] `tests/README.md`
  - [ ] `NEW_TESTS_SUMMARY.md`
  - [ ] `TESTING_CHECKLIST.md` (本文件)

- [ ] README.md 已更新
  - [ ] 包含 "完整测试套件" 章节
  - [ ] 提到 `run_full_test_suite.sh`

---

### 10. 代码质量检查

- [ ] C 代码编译无错误
  ```bash
  cd build && make 2>&1 | grep -i error
  # 应该没有输出（或只有预期的警告）
  ```

- [ ] Rust 代码 lint 通过
  ```bash
  cargo clippy --manifest-path rust_core/Cargo.toml --release
  # 应该没有严重警告
  ```

- [ ] Rust 代码格式正确
  ```bash
  cargo fmt --manifest-path rust_core/Cargo.toml --check
  # 应该没有格式问题
  ```

---

## 🎯 完整验证流程（推荐）

按以下顺序执行完整验证：

### 步骤 1: 环境准备

```bash
# 确保在项目根目录
cd /path/to/ModernFS

# 检查 Rust
rustc --version
cargo --version

# 检查 C 编译器
gcc --version
cmake --version
```

### 步骤 2: 清理并重新构建

```bash
# 清理旧的构建
rm -rf build target

# 重新构建
./build.sh

# 验证测试可执行文件
ls -l build/test_* | wc -l
# 应该输出: 13
```

### 步骤 3: 运行完整测试套件

```bash
# 这是最重要的一步
chmod +x run_full_test_suite.sh
./run_full_test_suite.sh
```

### 步骤 4: 检查结果

- ✅ 如果看到 "🎉 所有测试通过！" - 完美！
- ⚠️ 如果有失败 - 查看错误信息并参考故障排查部分

---

## 🔧 故障排查

### 问题 1: Rust 库链接失败

**症状**: `cannot find -lrust_core`

**解决方法**:
```bash
cargo build --release
rm -rf build
./build.sh
```

---

### 问题 2: mkfs.modernfs 不存在

**症状**: 测试提示 "mkfs.modernfs not found"

**解决方法**:
```bash
cd build
make mkfs.modernfs
cd ..
```

---

### 问题 3: 测试挂起或超时

**症状**: 测试长时间无响应

**可能原因**:
- Checkpoint 线程死锁
- 磁盘 I/O 问题
- 无限循环

**解决方法**:
```bash
# 强制停止
Ctrl+C

# 清理测试文件
rm -f test_*.img

# 重新运行
./build/test_name
```

---

### 问题 4: 权限错误

**症状**: "Permission denied"

**解决方法**:
```bash
chmod +x run_full_test_suite.sh
chmod +x build/test_*
```

---

### 问题 5: 编译警告过多

**症状**: 大量未使用变量警告

**注意**: 这些警告通常是安全的，但如果想修复：
```bash
# 查找未使用的变量
grep -r "warning: unused variable" build/
```

---

## 📊 测试覆盖统计

运行完整测试套件后，应该覆盖：

### C 组件
- ✅ 块设备 (block_dev.c) - 100%
- ✅ 缓冲区缓存 (buffer_cache.c) - 100%
- ✅ 块分配器 (block_alloc.c) - 100%
- ✅ Inode 管理 (inode.c) - 100%
- ✅ 目录管理 (directory.c) - 100%
- ✅ 路径解析 (path.c) - 100%
- ✅ 超级块 (superblock.c) - 100%
- ✅ fs_context (fs_context.c) - 100%

### Rust 组件
- ✅ Journal Manager - 100%
- ✅ Extent Allocator - 100%
- ✅ FFI 层 - 100%

### 功能场景
- ✅ 文件操作 - 完全覆盖
- ✅ 目录操作 - 完全覆盖
- ✅ 错误处理 - 完全覆盖
- ✅ 崩溃恢复 - 完全覆盖
- ✅ 性能测试 - 完全覆盖

---

## ✅ 验证完成标志

当你完成所有检查项后，应该能够：

1. ✅ 运行 `./run_full_test_suite.sh` 看到所有测试通过
2. ✅ 每个单独的测试都能成功运行
3. ✅ 没有内存泄漏或崩溃
4. ✅ 性能指标在合理范围内
5. ✅ 文档齐全且准确

**恭喜！ModernFS 的测试套件已经完全就绪！** 🎉

---

## 📝 报告问题

如果发现任何问题，请记录：

1. **测试名称**: 哪个测试失败了？
2. **错误信息**: 完整的错误输出
3. **环境信息**: 
   - 操作系统版本
   - Rust 版本 (`rustc --version`)
   - GCC 版本 (`gcc --version`)
4. **重现步骤**: 如何触发该问题？

---

## 🚀 下一步

测试全部通过后，你可以：

1. 运行 FUSE 集成测试
   ```bash
   ./tests/scripts/test_fuse_simple.sh
   ```

2. 运行性能基准测试
   ```bash
   ./tools/benchmark-rs/target/release/benchmark-modernfs
   ```

3. 测试崩溃恢复
   ```bash
   ./tests/crash/run_all.sh
   ```

---

**保持这个清单并在每次修改代码后重新验证！** ✨

# ModernFS 待解决问题文档

本文档记录了 ModernFS 项目当前存在的所有问题和需要解决的挑战。

## 🚨 严重问题 (Critical Issues)

### 1. 内存管理问题
**状态**: 🔴 CRITICAL
**影响**: 多个测试崩溃
**错误信息**:
- `Fatal glibc error: malloc.c:2599 (sysmalloc): assertion failed`
- `double free or corruption (!prev)`
**影响的测试**:
- Block Layer 测试 (`test_block_layer`)
- Week 7 集成测试 (`test_week7_integration`)
**可能原因**:
- Buffer Cache 中的内存分配/释放不匹配
- 多线程环境下的竞态条件
- Rust FFI 内存生命周期管理问题
**优先级**: 高 - 影响核心功能

## ⚠️ 构建问题 (Build Issues)

### 2. Rust/C 构建警告
**状态**: 🟡 LOW
**影响**: 产生编译噪音，但不影响功能
**警告内容**:
- `field 'superblock_block' is never read` (Rust)
- `constant 'JOURNAL_VERSION' is never used` (Rust)
- `struct 'Transaction' is never constructed` (Rust)
- `evict_lru_buffer' defined but not used` (C)
**解决方案**: 清理未使用的代码和导入
**优先级**: 低 - 代码质量问题

## 🧪 测试问题 (Test Issues)

### 3. Block Layer 测试崩溃
**状态**: 🔴 HIGH
**影响**: 无法验证基础存储层功能
**症状**: 测试启动时立即出现内存分配错误
**可能原因**:
- Buffer Cache 初始化问题
- 内存池配置错误
**调试建议**: 使用 AddressSanitizer 或 Valgrind (详见 [MEMORY_DEBUG.md](MEMORY_DEBUG.md))
**优先级**: 高 - 基础功能验证

### 4. Week 7 集成测试内存问题
**状态**: 🔴 HIGH
**影响**: 无法验证 Journal + Extent 集成功能
**症状**: 在 checkpoint 阶段出现 double free 错误
**可能原因**:
- Journal Manager 和 Extent Allocator 之间的内存管理冲突
- FFI 接口传递的所有权问题
- 文件描述符共享导致的问题
**建议修复**: 使用 `dup(fd)` 为每个组件创建独立的文件描述符
**优先级**: 高 - 核心集成功能

### 5. 崩溃测试脚本依赖问题
**状态**: 🟡 MEDIUM
**影响**: 无法运行崩溃恢复测试
**依赖问题**:
- 需要 `mkfs.modernfs` 在当前目录
- 需要编译特定的测试程序
- 测试程序构建失败
**优先级**: 中等 - 可靠性验证

## 🔧 功能完善 (Feature Completion)

### 6. FUSE 功能在 Windows/WSL 不可用
**状态**: 🟡 MEDIUM
**影响**: 无法在 Windows 环境下测试完整文件系统功能
**限制**:
- demo.sh 跳过 FUSE 相关演示
- 无法进行端到端文件操作测试
**解决方案**: 需要在纯 Linux 环境中测试，或使用 WSL2 + FUSE 支持
**优先级**: 中等 - 影响功能演示

### 7. Transaction 结构体未使用
**状态**: 🟡 LOW
**影响**: 代码不完整，可能有设计意图未实现
**描述**: `transaction/mod.rs` 中定义的 `Transaction` 结构体从未被实例化
**可能原因**: 实现采用了不同的设计模式
**优先级**: 低 - 代码清理和文档完善

## 📊 性能问题 (Performance Issues)

### 8. Buffer Cache 未使用的函数
**状态**: 🟡 LOW
**影响**: 代码冗余，可能影响维护性
**描述**: `evict_lru_buffer` 函数已定义但从未调用
**可能原因**: LRU 淘汰策略可能采用其他实现
**优先级**: 低 - 代码优化

## 🔍 调试和诊断需求

### 需要添加的调试功能
1. **内存调试**: 启用 AddressSanitizer 或 Valgrind 检测内存错误
2. **日志增强**: 在关键路径添加详细日志
3. **断言检查**: 添加更多运行时断言
4. **内存泄漏检测**: 长时间运行的内存使用监控

### 建议的调试步骤
1. 使用 `RUST_BACKTRACE=1` 获取 Rust 端的详细堆栈
2. 使用 `valgrind --tool=memcheck` 检测 C 端内存问题
3. 添加 `assert()` 验证内存分配前提条件
4. 在 FFI 边界添加参数验证

## 📋 解决优先级排序

### 🔴 立即解决 (阻塞项目)
1. ✅ **~~CMakeLists.txt 恢复~~** - 已完成
2. ✅ **~~并发测试链接~~** - 已修复，添加了缺失的源文件和修正了 API 调用
3. **内存管理问题** - 修复核心崩溃（详见 [MEMORY_DEBUG.md](MEMORY_DEBUG.md)）

### 🟡 短期解决 (1-2周)
4. **Block Layer 测试** - 验证基础功能
5. **Week 7 集成测试** - 核心集成功能
6. **崩溃测试脚本** - 可靠性验证

### 🟢 中期解决 (1个月内)
7. **FUSE 环境配置** - 完整功能测试
8. **代码清理** - 移除未使用代码
9. **性能优化** - Buffer Cache 改进

## ✅ 已解决问题 (Resolved Issues)

### CMakeLists.txt 文件问题 (已解决 - 2025-10-09)
- **问题**: 文件存在但并发测试缺少必要的链接
- **解决**: 在 `test_concurrent_writes` 和 `test_concurrent_alloc` 的编译配置中添加了 `buffer_cache.c` 和 `block_alloc.c`

### 并发测试 API 调用错误 (已解决 - 2025-10-09)
- **问题**: 测试使用了不存在的 `read_superblock(fd, &sb)` 函数
- **解决**: 改用正确的 API：
  - 使用 `blkdev_open()` 打开块设备
  - 使用 `superblock_read(dev, &sb)` 读取超级块
  - 通过 `dev->fd` 访问文件描述符

## 🔧 推荐的工具和方法

### 内存调试工具
- **Valgrind**: 检测 C 端内存错误
- **AddressSanitizer**: 编译时内存错误检测
- **Rust Sanitizer**: Rust 端内存安全检查

### 调试技巧
- 使用 `RUST_LOG=debug` 获取详细 Rust 日志
- 在关键函数入口添加参数验证
- 使用 `assert()` 检查不变量
- 添加内存分配/释放的审计日志

## 📚 相关文档

- [MEMORY_DEBUG.md](MEMORY_DEBUG.md) - 内存调试完整指南
- [IMPLEMENTATION.md](IMPLEMENTATION.md) - 实现细节
- [USER_GUIDE.md](USER_GUIDE.md) - 用户指南

---

**文档维护**: 此文档应在问题解决后及时更新，移除已解决的问题并添加新发现的问题。
**最后更新**: 2025-10-09
**更新者**: Claude Code Assistant

## 📝 更新日志

### 2025-10-09
- ✅ 修复了并发测试的编译链接问题
- ✅ 修复了并发测试的 API 调用错误
- 📄 添加了内存调试指南 (MEMORY_DEBUG.md)
- 📝 重新组织了问题优先级
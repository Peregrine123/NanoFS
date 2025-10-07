# Week 9 实施计划：测试完善与性能优化

**日期**: 2025-10-07
**状态**: 📋 计划中
**目标**: 完善测试覆盖，优化系统性能，准备演示环境

---

## 一、Week 9 总体目标

根据项目当前状态（Week 1-8 已完成），Week 9 的核心任务是：

1. **测试完善**: 补充缺失的测试用例，提升覆盖率
2. **性能优化**: 识别瓶颈并优化关键路径
3. **集成验证**: 端到端集成测试
4. **演示准备**: 准备演示脚本和环境

---

## 二、详细任务列表

### 2.1 测试完善 (40%)

#### 任务1: 崩溃测试套件 ⭐
**目标**: 验证崩溃一致性保证

**子任务**:
1. 创建 `tests/crash/` 目录
2. 编写崩溃场景测试:
   - `crash_during_write.sh` - 写入过程中断
   - `crash_after_commit.sh` - 提交后未checkpoint
   - `crash_during_checkpoint.sh` - checkpoint过程中断
3. 自动化恢复验证
4. 数据完整性检查

**验收标准**:
```bash
$ ./tests/crash/run_all.sh
[TEST 1] Crash during write
  ✓ Recovery successful
  ✓ Data intact

[TEST 2] Crash after commit
  ✓ Recovery successful
  ✓ Transaction replayed

[TEST 3] Crash during checkpoint
  ✓ Recovery successful
  ✓ No data loss

✅ All crash tests passed (3/3)
```

**文件清单**:
```
tests/crash/
├── run_all.sh              # 运行所有崩溃测试
├── crash_during_write.sh
├── crash_after_commit.sh
├── crash_during_checkpoint.sh
└── verify_recovery.sh      # 恢复验证工具
```

---

#### 任务2: 并发测试 ⭐
**目标**: 验证多线程安全性

**子任务**:
1. 创建 `tests/concurrent/` 目录
2. 编写并发场景测试:
   - `concurrent_writes.c` - 多线程写入同一文件
   - `concurrent_alloc.c` - 并发extent分配
   - `stress_test.c` - 高负载压力测试

**验收标准**:
```bash
$ ./build/test_concurrent_writes
[INFO] Starting 10 threads...
[INFO] Each thread writes 1000 blocks...
[INFO] Total writes: 10000

✅ No data races detected
✅ All data verified
✅ Test passed in 2.3s
```

**技术要点**:
- 使用 `pthread` 创建多线程
- 验证锁的正确性（Journal、Extent）
- 检测死锁和竞态条件

---

#### 任务3: Fuzz 测试（可选）
**目标**: 发现边界情况和异常输入

**子任务**:
1. 使用 `cargo-fuzz` 或 `AFL` 对 Rust 模块进行 fuzz
2. 测试输入:
   - 畸形的日志记录
   - 异常的extent请求（len=0, len>total）
   - 非法的块号

**验收标准**:
```bash
$ cargo fuzz run fuzz_journal -- -runs=100000
INFO: Running with 100000 inputs
INFO: 0 crashes, 0 hangs, 0 leaks

✅ Fuzzing completed without issues
```

---

### 2.2 性能优化 (30%)

#### 任务4: 性能基准测试 ⭐
**目标**: 建立性能基线，识别瓶颈

**子任务**:
1. 使用 `benchmark-modernfs` 工具运行完整测试
2. 记录性能数据:
   - 顺序写吞吐量 (MB/s)
   - 随机写IOPS
   - 元数据操作延迟（mkdir, create）
3. 与 tmpfs/ext4 对比
4. 生成性能报告

**验收标准**:
```bash
$ ./target/release/benchmark-modernfs /mnt/modernfs --count 1000

╔════════════════════════════════════════╗
║  ModernFS Performance Benchmark        ║
╚════════════════════════════════════════╝

[1/4] Sequential Write (4KB blocks)
  Total: 1000 writes
  Time: 1.23s
  Throughput: 3.25 MB/s
  Avg Latency: 1.23ms

[2/4] Sequential Read (4KB blocks)
  Total: 1000 reads
  Time: 0.45s
  Throughput: 8.89 MB/s
  Avg Latency: 0.45ms

[3/4] Metadata Operations
  mkdir: 500 ops in 0.23s (2173 ops/s)
  create: 500 ops in 0.31s (1612 ops/s)

[4/4] Extent Allocation
  Total: 500 allocations
  Avg Latency: 0.05ms
  Fragmentation: 2.3%

╔════════════════════════════════════════╗
║  Summary                                ║
╚════════════════════════════════════════╝
✅ Sequential Write: 3.25 MB/s
✅ Sequential Read:  8.89 MB/s
✅ Metadata Ops:     ~2000 ops/s
```

**输出文件**:
- `benchmark_results.txt` - 原始数据
- `benchmark_report.md` - 分析报告

---

#### 任务5: 热点优化
**目标**: 优化关键性能路径

**优化点**:

1. **Buffer Cache 优化**:
   - 调整缓存大小（1024 → 2048 块）
   - 改进 LRU 驱逐策略
   - 添加预取机制

2. **Journal 优化**:
   - 批量提交事务（Group Commit）
   - 减少 fsync 频率
   - 异步 checkpoint

3. **Extent Allocator 优化**:
   - 缓存最近释放的extent（快速分配）
   - 使用 B-tree 加速搜索
   - 预分配策略

**验收标准**:
- 顺序写吞吐量提升 20%
- 元数据操作延迟降低 15%
- Extent分配延迟 < 0.1ms

---

### 2.3 集成验证 (20%)

#### 任务6: FUSE 集成测试 ⭐
**目标**: 验证完整的文件系统功能

**子任务**:
1. 扩展 `tests/scripts/test_fuse_auto.sh`
2. 测试场景:
   - 文件基本操作（create, read, write, delete）
   - 目录操作（mkdir, rmdir, readdir）
   - 大文件操作（>1GB）
   - 权限和所有权
   - 软硬链接（如果支持）

**验收标准**:
```bash
$ ./tests/scripts/test_fuse_comprehensive.sh

╔════════════════════════════════════════╗
║  ModernFS FUSE Integration Test        ║
╚════════════════════════════════════════╝

[1/10] File Creation            ✅ PASS
[2/10] File Read/Write          ✅ PASS
[3/10] File Deletion            ✅ PASS
[4/10] Directory Operations     ✅ PASS
[5/10] Large File (1GB)         ✅ PASS
[6/10] Permissions              ✅ PASS
[7/10] Extended Attributes      ⏭️  SKIP (not implemented)
[8/10] Symbolic Links           ⏭️  SKIP (not implemented)
[9/10] Crash Recovery           ✅ PASS
[10/10] Disk Space Management   ✅ PASS

╔════════════════════════════════════════╗
║  Result: 8/8 tests passed              ║
╚════════════════════════════════════════╝
```

---

#### 任务7: 工具集成测试
**目标**: 验证 Rust 工具的端到端流程

**测试流程**:
```bash
# 1. 格式化
mkfs-modernfs test.img --size 256M

# 2. 挂载
modernfs test.img /mnt/test

# 3. 操作文件
echo "test" > /mnt/test/file.txt

# 4. 卸载
fusermount -u /mnt/test

# 5. 检查
fsck-modernfs test.img

# 6. 重新挂载验证
modernfs test.img /mnt/test
cat /mnt/test/file.txt  # 应输出 "test"
```

**验收标准**:
```bash
$ ./tests/scripts/test_tool_integration.sh
✅ mkfs successful
✅ mount successful
✅ file operations successful
✅ fsck passed with no errors
✅ data persisted after remount
```

---

### 2.4 演示准备 (10%)

#### 任务8: 演示脚本
**目标**: 创建自动化演示

**子任务**:
1. 编写 `demo.sh` 脚本
2. 演示内容:
   - 格式化文件系统
   - 挂载
   - 基本文件操作
   - 崩溃恢复演示
   - 性能测试
   - fsck 检查

**脚本结构**:
```bash
#!/bin/bash
# demo.sh - ModernFS 自动化演示

echo "╔════════════════════════════════════════╗"
echo "║  ModernFS Live Demo                    ║"
echo "║  C + Rust Hybrid Filesystem            ║"
echo "╚════════════════════════════════════════╝"

# 1. 环境准备
echo -e "\n[1/6] 准备环境..."
cleanup_previous_demo

# 2. 格式化
echo -e "\n[2/6] 格式化文件系统 (256MB)..."
./target/release/mkfs-modernfs demo.img --size 256M --force

# 3. 挂载
echo -e "\n[3/6] 挂载文件系统..."
./build/modernfs demo.img /mnt/demo -f &
sleep 2

# 4. 文件操作
echo -e "\n[4/6] 文件操作演示..."
cd /mnt/demo
echo "Hello ModernFS!" > test.txt
cat test.txt
mkdir -p a/b/c
tree -L 3

# 5. 崩溃恢复
echo -e "\n[5/6] 崩溃恢复演示..."
./tests/crash/demo_crash_recovery.sh

# 6. fsck 检查
echo -e "\n[6/6] 文件系统检查..."
./target/release/fsck-modernfs demo.img

echo -e "\n✅ Demo 完成！"
```

---

#### 任务9: 性能对比报告
**目标**: 生成可视化性能对比

**输出内容**:
1. Markdown 表格:
   ```markdown
   | 操作 | ModernFS | tmpfs | ext4 |
   |------|----------|-------|------|
   | 顺序写 | 3.2 MB/s | 120 MB/s | 85 MB/s |
   | 顺序读 | 8.9 MB/s | 150 MB/s | 110 MB/s |
   | mkdir | 2173 ops/s | 12000 ops/s | 8000 ops/s |
   ```

2. 图表（可选，使用 gnuplot）

3. 分析结论:
   - ModernFS 的性能瓶颈在哪里？
   - Journal 带来的开销是多少？
   - 与生产级文件系统的差距

---

## 三、时间安排

### Week 9 日程表

| 日期 | 任务 | 预计时间 |
|------|------|---------|
| Day 1 | 任务1: 崩溃测试套件 | 4小时 |
| Day 2 | 任务2: 并发测试 | 4小时 |
| Day 3 | 任务4: 性能基准测试 | 3小时 |
| Day 4 | 任务5: 热点优化 | 5小时 |
| Day 5 | 任务6: FUSE 集成测试 | 3小时 |
| Day 6 | 任务7: 工具集成测试 | 2小时 |
| Day 7 | 任务8-9: 演示准备 | 3小时 |

**总计**: 24小时（约3-4个工作日）

---

## 四、优先级分级

### P0 - 必须完成 ⭐⭐⭐
- 任务1: 崩溃测试套件
- 任务4: 性能基准测试
- 任务6: FUSE 集成测试
- 任务8: 演示脚本

### P1 - 重要
- 任务2: 并发测试
- 任务7: 工具集成测试
- 任务9: 性能对比报告

### P2 - 可选
- 任务3: Fuzz 测试
- 任务5: 热点优化（如果时间充足）

---

## 五、交付物清单

### 代码
- [ ] `tests/crash/` - 崩溃测试脚本
- [ ] `tests/concurrent/` - 并发测试代码
- [ ] `tests/scripts/test_fuse_comprehensive.sh` - 完整 FUSE 测试
- [ ] `demo.sh` - 演示脚本

### 文档
- [ ] `WEEK9_REPORT.md` - Week 9 实施报告
- [ ] `PERFORMANCE.md` - 性能评估报告
- [ ] `TESTING.md` - 测试策略文档

### 数据
- [ ] `benchmark_results.txt` - 基准测试原始数据
- [ ] `test_coverage.txt` - 测试覆盖率报告

---

## 六、验收标准

### 代码质量
- ✅ 所有新增测试通过
- ✅ 代码覆盖率 > 80%
- ✅ 无内存泄漏（Valgrind 检查）
- ✅ 无数据竞争（ThreadSanitizer）

### 性能指标
- ✅ 顺序写 > 3 MB/s
- ✅ 顺序读 > 8 MB/s
- ✅ 元数据操作 > 2000 ops/s
- ✅ Extent 分配延迟 < 0.1ms

### 测试覆盖
- ✅ 单元测试: 10个测试套件
- ✅ 集成测试: 3个完整场景
- ✅ 崩溃测试: 3个场景
- ✅ 并发测试: 2个场景

---

## 七、风险与应对

### 风险1: 崩溃测试难以实现
**应对**: 使用 `kill -9` 模拟崩溃，简化测试场景

### 风险2: 性能优化效果不明显
**应对**: 重点放在测试完善，性能优化作为可选项

### 风险3: 时间不足
**应对**: 按优先级执行，P0 任务优先，P2 任务可推迟到 Week 10

---

## 八、下一步（Week 10）

Week 9 完成后，Week 10 的重点是：

1. **文档完善**: 编写完整的实现报告
2. **代码清理**: 代码审查、注释补充
3. **答辩准备**: PPT制作、演讲稿
4. **最终打磨**: bug修复、细节优化

---

**创建日期**: 2025-10-07
**状态**: 📋 待执行
**预计完成**: 2025-10-14

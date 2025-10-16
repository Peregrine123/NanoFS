# ModernFS 学习历程记录

**学习者身份**: 初学者
**开始日期**: 2025-10-10
**学习目标**: 深入理解 ModernFS 文件系统的核心技术和架构设计

---

## 学习路线图

### 阶段一: 基础入门 (1-2周)

#### 1. 环境搭建与快速开始 ✅
- [x] 1.1 环境要求
  - [x] 了解 Linux/WSL 环境要求
  - [x] 确认 Rust 和 C 编译工具链
  - [x] 理解 FUSE 依赖
- [x] 1.2 安装步骤
  - [x] 克隆仓库
  - [x] 安装系统依赖
  - [x] 构建项目 (./build.sh)
  - [x] 验证安装成功
- [x] 1.3 第一次使用
  - [x] 创建文件系统镜像
  - [x] 挂载文件系统
  - [x] 执行基本文件操作
  - [x] 正确卸载

**学习成果**: 能够独立搭建环境并执行基本的文件系统操作

---

#### 2. 工具使用掌握 🔄
- [x] 2.1 mkfs-modernfs (格式化工具)
  - [x] 理解超级块、日志区、数据区的布局
  - [x] 掌握基本格式化命令
  - [x] 实验不同大小参数 (256MB测试)
  - [x] 理解日志大小对性能的影响（1/8策略）
  - [x] 手动计算并验证磁盘布局

- [x] 2.2 fsck-modernfs (文件系统检查)
  - [x] 理解 6 个检查阶段的作用
  - [x] 运行基本检查命令
  - [x] 使用 --check-journal 深入检查
  - [x] 实验故意损坏文件系统并检测
  - [x] 理解何时需要运行 fsck
  - [x] 修复发现的 4 个 bug
  - [x] 理解 Rust 代码（通过 C/C++ 对照）

- [ ] 2.3 benchmark-modernfs (性能测试)
  - [ ] 了解各种测试类型 (顺序读写、随机读写、元数据操作)
  - [ ] 运行完整基准测试
  - [ ] 分析性能数据
  - [ ] 对比不同配置的性能差异
  - [ ] 理解 ModernFS 与传统文件系统的性能差异

- [ ] 2.4 modernfs (FUSE驱动)
  - [ ] 理解 FUSE 工作原理
  - [ ] 掌握前台/后台挂载
  - [ ] 使用调试模式 (-d) 观察日志
  - [ ] 理解各种 FUSE 选项

**学习成果**: 熟练使用所有工具，能够诊断基本问题

---

#### 3. 常见操作实践 📋
- [ ] 3.1 创建和使用文件系统
  - [ ] 完整工作流实践
  - [ ] 创建目录树结构
  - [ ] 批量文件操作

- [ ] 3.2 备份和恢复
  - [ ] 备份整个文件系统
  - [ ] 恢复损坏的文件系统
  - [ ] 迁移到另一台机器

- [ ] 3.3 监控文件系统使用
  - [ ] 查看空间使用 (df -h)
  - [ ] 查看 Inode 使用 (df -i)
  - [ ] 理解何时会耗尽空间/Inode

- [ ] 3.4 性能优化
  - [ ] 调整日志大小实验
  - [ ] 使用 RAM disk 提升性能
  - [ ] 理解性能瓶颈

**学习成果**: 能够处理日常文件系统管理任务

---

### 阶段二: 架构理解 (2-3周)

#### 4. 混合架构设计 📋
- [ ] 4.1 C 组件理解
  - [ ] FUSE 接口层 (fuse_ops.c, main_fuse.c)
    - [ ] 理解 POSIX 系统调用映射
    - [ ] 阅读 getattr, read, write 实现
  - [ ] 块设备层 (block_dev.c, buffer_cache.c)
    - [ ] 理解块 I/O 抽象
    - [ ] 学习 LRU 缓存算法
  - [ ] Inode 层 (inode.c)
    - [ ] 理解 Inode 结构 (直接/间接块)
    - [ ] 学习 Inode 缓存机制
  - [ ] 目录层 (directory.c)
    - [ ] 理解变长目录项设计
    - [ ] 学习路径查找算法

- [ ] 4.2 Rust 组件理解
  - [ ] Journal Manager (rust_core/src/journal/)
    - [ ] 理解 WAL (Write-Ahead Logging) 原理
    - [ ] 学习事务状态机 (Active → Committed → Checkpointed)
    - [ ] 理解崩溃恢复机制
  - [ ] Extent Allocator (rust_core/src/extent/)
    - [ ] 理解 Extent 分配算法 (First-Fit)
    - [ ] 学习碎片跟踪机制
    - [ ] 理解双重释放检测

- [ ] 4.3 FFI 接口
  - [ ] 理解 Rust 与 C 的互操作
  - [ ] 学习 #[no_mangle] 和 extern "C"
  - [ ] 理解内存管理 (Box::into_raw)
  - [ ] 学习错误处理 (catch_panic)

**学习成果**: 理解混合架构的设计思想和各层职责

---

#### 5. 核心数据结构 📋
- [ ] 5.1 磁盘布局
  - [ ] 绘制磁盘布局图
  - [ ] 计算各区域大小
  - [ ] 理解超级块字段

- [ ] 5.2 Inode 结构
  - [ ] 理解 12 直接块 + 间接块设计
  - [ ] 计算最大文件大小
  - [ ] 理解 Inode 位图

- [ ] 5.3 Journal 结构
  - [ ] 理解循环缓冲区设计
  - [ ] 学习事务记录格式
  - [ ] 理解 Checkpoint 过程

- [ ] 5.4 Buffer Cache
  - [ ] 理解哈希表 + LRU 设计
  - [ ] 学习线程安全机制 (pthread_rwlock_t)

**学习成果**: 掌握核心数据结构的设计原理

---

### 阶段三: 故障处理与优化 (1-2周)

#### 6. 故障排除实践 📋
- [ ] 6.1 挂载失败
  - [ ] 模拟各种挂载失败场景
  - [ ] 掌握调试方法

- [ ] 6.2 性能问题
  - [ ] 诊断写入慢问题
  - [ ] 诊断读取慢问题
  - [ ] 使用 benchmark 定位瓶颈

- [ ] 6.3 数据丢失
  - [ ] 模拟崩溃场景
  - [ ] 实践恢复流程
  - [ ] 理解 Journal 恢复机制

- [ ] 6.4 崩溃和卡死
  - [ ] 使用 gdb 调试 C 代码
  - [ ] 分析崩溃日志

**学习成果**: 能够独立诊断和解决常见问题

---

#### 7. 限制与设计权衡 📋
- [ ] 7.1 已知限制
  - [ ] 理解每个限制背后的设计原因
  - [ ] 思考如何改进

- [ ] 7.2 适用/不适用场景
  - [ ] 理解教学项目的定位
  - [ ] 对比生产级文件系统

- [ ] 7.3 安全建议
  - [ ] 实践备份策略
  - [ ] 实践监控策略

**学习成果**: 理解设计权衡，能够评估技术选型

---

### 阶段四: 深入源码 (3-4周)

#### 8. 开发流程实践 📋
- [ ] 8.1 添加新 C 模块
  - [ ] 创建简单的 C 模块
  - [ ] 编写单元测试
  - [ ] 更新 CMakeLists.txt

- [ ] 8.2 添加 Rust 功能
  - [ ] 实现简单的 Rust 功能
  - [ ] 导出 FFI 接口
  - [ ] 在 C 代码中调用

- [ ] 8.3 常见陷阱
  - [ ] 理解 Packed Structs 问题
  - [ ] 理解 WSL 路径转换
  - [ ] 理解 FFI 生命周期管理

**学习成果**: 能够修改和扩展代码库

---

#### 9. 测试策略 📋
- [ ] 9.1 单元测试
  - [ ] 运行所有单元测试
  - [ ] 理解测试设计
  - [ ] 编写新的测试用例

- [ ] 9.2 集成测试
  - [ ] 运行 Rust/C 集成测试
  - [ ] 理解测试边界

- [ ] 9.3 FUSE 测试
  - [ ] 运行脚本测试
  - [ ] 编写新的测试脚本

**学习成果**: 掌握测试驱动开发流程

---

#### 10. 项目阶段回顾 📋
- [ ] 10.1 Week 1-3: 基础层
  - [ ] 回顾 FFI, Block Layer, Inode Layer 实现
  - [ ] 阅读周报理解设计决策

- [ ] 10.2 Week 4-5: FUSE & Journal
  - [ ] 理解 FUSE 集成
  - [ ] 深入学习 WAL 实现

- [ ] 10.3 Week 6: Extent Allocator
  - [ ] 理解 Extent 分配策略
  - [ ] 学习碎片管理

- [ ] 10.4 Week 7+: 未来计划
  - [ ] 了解 Journal + Extent 集成计划
  - [ ] 了解 Rust 工具链计划

**学习成果**: 全面理解项目演进过程

---

## 学习资源

### 必读文档
- [x] docs/USER_GUIDE.md - 用户手册 (第1章已完成)
- [ ] docs/IMPLEMENTATION.md - 实现细节
- [ ] docs/DEVELOPER.md - 开发指南
- [ ] docs/WEEK*_REPORT.md - 周报 (了解设计演进)
- [ ] CLAUDE.md - 项目说明

### 推荐资源
- [ ] [FUSE Documentation](https://libfuse.github.io/)
- [ ] [Rust FFI Guide](https://doc.rust-lang.org/nomicon/ffi.html)
- [ ] [xv6 Filesystem](https://pdos.csail.mit.edu/6.828/2020/xv6/book-riscv-rev1.pdf)

---

## 学习笔记

### 第1章: 快速开始 (已完成 ✅)

**关键收获**:
- 理解了 ModernFS 的混合架构 (C + Rust)
- 掌握了基本的构建流程 (./build.sh)
- 学会了创建、挂载、使用文件系统的完整流程
- 理解了 WSL 在 Windows 环境下的必要性

**重要概念**:
- **FUSE**: 用户态文件系统，不需要内核模块
- **镜像文件**: disk.img 是文件系统的容器
- **挂载点**: /mnt/test 是访问文件系统的入口
- **正确卸载**: fusermount -u 避免数据丢失

**实践经验**:
```bash
# 成功执行的第一个完整流程
./target/release/mkfs-modernfs mydisk.img --size 256M
sudo mkdir -p /mnt/modernfs
sudo ./build/modernfs mydisk.img /mnt/modernfs -f &
echo "Hello, ModernFS!" > /mnt/modernfs/test.txt
cat /mnt/modernfs/test.txt
sudo fusermount -u /mnt/modernfs
```

**疑问与待探索**:
- [ ] Journal 日志系统的具体工作原理？
- [ ] Extent Allocator 如何优化碎片？
- [ ] 为什么性能比 ext4 低这么多？
- [ ] 如何实现崩溃一致性？

---

### 第2章: 工具使用掌握（进行中 🔄）

#### 2.1 mkfs-modernfs - 格式化工具 (已完成 ✅)

**关键收获**:
- 深入理解了 ModernFS 的磁盘布局结构
- 掌握了各区域大小的计算方法（Inode数、位图大小、Journal大小）
- 理解了迭代计算的必要性（位图大小依赖数据块数，反之亦然）
- 学会了手动计算和验证磁盘布局

**核心概念**:
- **磁盘布局**：`[SuperBlock | Journal | Inode Bitmap | Data Bitmap | Inode Table | Data Blocks]`
- **SuperBlock（超级块）**：文件系统的"身份证"，记录魔数、版本、各区域位置等元数据
  - 位置：块 0
  - 魔数：`0x4D46534D` ("MFSM")
- **Journal（日志区）**：用于崩溃恢复的预写日志
  - 大小策略：文件系统的 1/8，最小 1MB（256块），最大 8MB（2048块）
  - 答案了第1章疑问：为什么需要 Journal？→ 保证崩溃一致性
- **Inode Bitmap**：标记哪些 Inode 已被使用（每位代表1个Inode）
- **Data Bitmap**：标记哪些数据块已被使用（每位代表1个块）
- **Inode Table**：存储所有 Inode 结构（每个 Inode 128字节，每块存32个）
- **Data Blocks**：实际文件数据存储区

**计算公式**（以 256MB 为例）:
```
1. 总块数 = 256MB / 4KB = 65536 块
2. Inode数量 = (总块数 - 100) / 1024，最少64个
   → (65536 - 100) / 1024 = 63，取最小值 = 64个
3. Journal块数 = min(max(总块数/8, 256), 2048)
   → 65536/8 = 8192 > 2048，取最大值 = 2048块
4. Inode位图 = (Inode数 + 32767) / 32768
   → (64 + 32767) / 32768 = 1块
5. Inode表 = (Inode数 + 31) / 32
   → (64 + 31) / 32 = 2块
6. 数据位图（需迭代计算）:
   第一轮: data_blocks = 65536 - 1 - 1 = 65534
          data_bitmap = (65534 + 32767) / 32768 = 3块
   第二轮: metadata = 1 + 2048 + 1 + 3 + 2 = 2055
          data_blocks = 65536 - 2055 = 63481
          data_bitmap = (63481 + 32767) / 32768 = 2块 ✅
   第三轮: metadata = 1 + 2048 + 1 + 2 + 2 = 2054
          data_blocks = 65536 - 2054 = 63482 ✅（收敛）
7. 最终布局:
   块0:         SuperBlock (1块)
   块1-2048:    Journal (2048块)
   块2049:      Inode Bitmap (1块)
   块2050-2051: Data Bitmap (2块)
   块2052-2053: Inode Table (2块)
   块2054-65535: Data Blocks (63482块)
```

**代码位置**:
- `src/mkfs.c:165-299` - 主流程（7个步骤）
- `src/superblock.c:74-171` - 磁盘布局计算核心逻辑
- `include/modernfs/types.h:28-48` - superblock_t 结构定义

**实践经验**:
```bash
# 格式化 256MB 文件系统并查看布局
./build/mkfs.modernfs test256.img 256

# 输出会显示详细的区域分配信息
```

**易错点**:
- ❌ 忘记减去 Journal 和 Inode 表：初学者容易只减超级块和位图
- ❌ 没有迭代计算：数据位图大小依赖数据块数，需要多轮调整
- ❌ 向上取整公式：`(n + divisor - 1) / divisor` 与 `(n + 32767) / 32768` 的区别

**回答的第1章疑问**:
- ✅ **Journal 日志系统大小如何确定？** → 文件系统的1/8，最小1MB，最大8MB

**新产生的疑问**:
- [x] Journal 的内部结构是什么？（journal superblock, head, tail） → 已在2.2节解答
- [ ] 根目录是如何创建的？（Inode #1 的特殊性）
- [ ] mkfs 的 Journal 初始化做了什么？（第4步，216-255行）

---

#### 2.2 fsck-modernfs - 文件系统检查 (已完成 ✅)

**关键收获**:
- 理解了文件系统一致性的核心概念（"索引"与"实际数据"必须对得上）
- 掌握了 fsck 的 6 个检查阶段及其依赖关系
- 学会了读懂 Rust 代码（通过 C/C++ 对照）
- 实践了手动破坏文件系统并用 fsck 检测
- 修复了 4 个实际 bug（Inode 位图不一致、Journal 结构不一致等）

**核心概念**:
- **文件系统一致性**：元数据（位图、超级块）与实际数据必须一致
  - 类比：图书馆的"索引卡片"和"书架上的书"必须对应
  - 不一致原因：系统崩溃、软件 bug、硬件故障
- **fsck 的三大任务**：
  1. 检测（Detect）：发现不一致
  2. 修复（Repair）：自动修复（--auto-fix）
  3. 报告（Report）：告知用户状态

**6 个检查阶段**（按依赖顺序）:
```
Phase 1: Superblock（地基）
  ├─ 检查魔数、版本、布局合理性
  └─ 如果地基坏了，后续检查都不可信

Phase 2: Journal（支柱）
  ├─ 检查 Journal Superblock (magic, version, head, tail)
  ├─ head == tail → Journal 为空 ✅
  └─ head != tail → 有未完成的事务 ⚠️

Phase 3: Inode Bitmap（索引系统）
  ├─ 统计位图中的 1（用 count_ones()）
  ├─ 计算期望值：total_inodes - free_inodes
  └─ 对比：位图统计值 vs 期望值

Phase 4: Data Bitmap（索引系统）
  └─ 同 Phase 3，检查数据块位图

Phase 5: Inodes（实际数据）
  ├─ 扫描前 100 个 Inode（性能考虑）
  ├─ 检查类型合法性（FILE/DIR/SYMLINK）
  ├─ 检查链接数合理性（nlink）
  └─ 与 Phase 3 对比，验证一致性

Phase 6: Directories（数据关系）
  ├─ 检查根目录（Inode #1）
  ├─ 验证 "." 指向自己
  └─ 递归检查子目录
```

**为什么这个顺序不能变？**
- Phase 1 提供"地图"（各区域在哪里）→ 必须先验证
- Phase 3/4 建立"期望值" → 必须在 Phase 5 之前
- Phase 5 检查"实际值" → 依赖 Phase 1 和 Phase 3
- Phase 6 检查"关系" → 依赖 Phase 5 的 Inode 数据

**Journal Superblock 结构**（回答了2.1节的疑问）:
```rust
struct JournalSuperblock {
    magic: u32,           // 魔数 "JRNL" (0x4A524E4C)
    version: u32,         // 版本号
    block_size: u32,      // 块大小
    total_blocks: u32,    // Journal 区总块数
    sequence: u64,        // 事务序列号（递增）
    head: u32,            // 循环缓冲区头指针
    tail: u32,            // 循环缓冲区尾指针
}
```

**head 和 tail 的含义**:
```
Journal 是循环缓冲区：
[块0: JSB] [块1: 数据] ... [块N: 数据]
            ↑                ↑
          head             tail

- head == tail: 空
- head < tail: 有 (tail - head) 个待处理块
- head > tail: 循环绕回
```

**Rust vs C/C++ 代码对照**:
| Rust | C/C++ | 说明 |
|------|-------|------|
| `let mut bitmap = vec![0u8; size]` | `uint8_t* bitmap = calloc(size, 1)` | 动态数组 |
| `for byte in &bitmap` | `for (int i=0; i<size; i++)` | 遍历 |
| `byte.count_ones()` | `__builtin_popcount(*byte)` | 统计1的个数 |
| `&ctx` | `const Context*` | 不可变引用 |
| `&mut ctx` | `Context*` | 可变引用 |
| `Result<()>` | `int` (错误码) | 错误处理 |
| `?` | `if (ret<0) return ret` | 错误传播 |

**代码位置**:
- `tools/fsck-rs/src/main.rs:293-318` - check_inode_bitmap 核心逻辑
- `tools/fsck-rs/src/main.rs:250-291` - check_journal
- `tools/fsck-rs/src/main.rs:80-96` - JournalSuperblock 结构定义

**实践经验**:
```bash
# 基本检查
./target/release/fsck-modernfs test256.img

# 检查 Journal
./target/release/fsck-modernfs test256.img --check-journal

# 详细输出
./target/release/fsck-modernfs test256.img --verbose

# 模拟损坏：修改超级块统计数据
python3 -c "
import struct
with open('test.img', 'r+b') as f:
    f.seek(64)  # free_inodes 偏移
    f.write(struct.pack('<I', 60))  # 改成错误值
"
# 运行 fsck 会检测到 "Inode bitmap count mismatch"
```

**修复的 4 个 Bug**:
| Bug | 位置 | 问题 | 修复 |
|-----|------|------|------|
| 1 | `superblock.c:137` | `free_inodes` 应减 2 而非 1 | ✅ 已修复 |
| 2 | `superblock.c:144` | `state` 已正确初始化为 CLEAN | ✅ 已存在 |
| 3 | `fsck-rs/main.rs:87-90` | Journal 结构缺 `version` 字段 | ✅ 已修复 |
| 4 | `fsck-rs/main.rs:242` | 状态检查用错常量 | ✅ 已修复 |

**易错点**:
- ❌ 忘记更新顺序的重要性：崩溃时先更新的数据更可靠
- ❌ C 和 Rust 结构体定义不一致：FFI 陷阱
- ❌ 只检查 100 个 Inode 的局限性：可能漏检后面的错误

**回答的疑问**:
- ✅ **Journal 的内部结构？** → JSB 包含 head/tail 指针，循环缓冲区设计
- ✅ **为什么需要 fsck？** → 检测崩溃、bug、硬件故障导致的不一致
- ✅ **检查顺序的重要性？** → 依赖链：Superblock → Bitmaps → Inodes → Directories

**新产生的疑问**:
- [ ] 如何实现 `--auto-fix` 自动修复？（信任位图还是超级块？）
- [ ] Phase 5 只检查 100 个 Inode 的风险有多大？
- [ ] Journal 的 Replay 机制如何实现？（崩溃恢复的核心）

---

### 待更新...

*(在学习每个新阶段后，在此处记录笔记)*

---

## 进度追踪

| 阶段 | 进度 | 预计完成日期 | 实际完成日期 |
|------|------|-------------|-------------|
| 阶段一: 基础入门 | 35% | 2025-10-24 | - |
| 阶段二: 架构理解 | 0% | 2025-11-14 | - |
| 阶段三: 故障处理 | 0% | 2025-11-28 | - |
| 阶段四: 深入源码 | 0% | 2025-12-26 | - |

**当前焦点**: 第2.2节 fsck-modernfs 已完成 ✅，下一步：第2.3节 benchmark-modernfs

---

## 学习目标

### 短期目标 (1个月)
- [ ] 完成阶段一所有内容
- [ ] 能够独立操作所有 CLI 工具
- [ ] 理解文件系统的基本概念

### 中期目标 (2个月)
- [ ] 完成阶段二，理解混合架构
- [ ] 能够阅读和理解核心代码
- [ ] 掌握 C/Rust FFI 机制

### 长期目标 (3个月)
- [ ] 完成所有阶段
- [ ] 能够修改和扩展功能
- [ ] 贡献代码或撰写技术文章

---

**最后更新**: 2025-10-15
**下次更新计划**: 完成第2.3节 benchmark-modernfs 后

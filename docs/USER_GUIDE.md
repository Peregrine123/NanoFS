# ModernFS 用户手册

**版本**: 1.0.0
**更新日期**: 2025-10-07
**适用对象**: 最终用户、系统管理员

---

## 目录

1. [快速开始](#1-快速开始)
2. [工具使用](#2-工具使用)
3. [常见操作](#3-常见操作)
4. [故障排除](#4-故障排除)
5. [限制与注意事项](#5-限制与注意事项)
6. [FAQ](#6-faq)

---

## 1. 快速开始

### 1.1 环境要求

#### Linux (推荐)
- **操作系统**: Ubuntu 20.04+ / Debian 11+ / Fedora 35+
- **Rust**: 1.70 或更高版本
- **C编译器**: GCC 9+ 或 Clang 10+
- **CMake**: 3.20+
- **FUSE**: libfuse3-dev
- **内存**: 至少512MB可用内存
- **磁盘**: 50MB用于编译，任意大小用于文件系统镜像

#### Windows (部分功能)
- **Rust**: 1.70+
- **编译器**: MinGW-w64 或 Visual Studio 2019+
- **CMake**: 3.20+
- **注意**: FUSE挂载功能不可用，可使用WSL

### 1.2 安装步骤

#### 方法1: 从源码构建

```bash
# 1. 克隆仓库
git clone https://github.com/yourname/modernfs.git
cd modernfs

# 2. 安装依赖 (Ubuntu/Debian)
sudo apt update
sudo apt install -y build-essential cmake libfuse3-dev curl

# 3. 安装Rust (如未安装)
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
source $HOME/.cargo/env

# 4. 构建项目
./build.sh

# 5. 验证安装
./target/release/mkfs-modernfs --version
```

**预期输出**:
```
mkfs-modernfs 1.0.0
```

#### 方法2: 使用WSL (Windows用户)

```powershell
# 1. 启用WSL
wsl --install

# 2. 进入WSL
wsl

# 3. 按照上述Linux步骤操作
```

### 1.3 第一次使用

```bash
# 步骤1: 创建256MB文件系统
./target/release/mkfs-modernfs mydisk.img --size 256M

# 步骤2: 创建挂载点
sudo mkdir -p /mnt/modernfs

# 步骤3: 挂载文件系统
sudo ./build/modernfs mydisk.img /mnt/modernfs -f &

# 步骤4: 使用文件系统
cd /mnt/modernfs
echo "Hello, ModernFS!" > test.txt
cat test.txt

# 步骤5: 卸载
sudo fusermount -u /mnt/modernfs
```

**输出示例**:
```bash
$ cat test.txt
Hello, ModernFS!
```

---

## 2. 工具使用

ModernFS提供3个CLI工具，全部使用Rust编写，具有彩色输出和友好的错误提示。

### 2.1 mkfs-modernfs (格式化工具)

#### 基本用法

```bash
mkfs-modernfs [OPTIONS] <IMAGE>
```

#### 选项说明

| 选项 | 说明 | 默认值 |
|------|------|--------|
| `-s, --size <SIZE>` | 文件系统总大小 | 必需 |
| `-j, --journal-size <SIZE>` | 日志区大小 | 32M |
| `-b, --block-size <SIZE>` | 块大小 | 4096 |
| `-f, --force` | 强制覆盖已存在的文件 | false |
| `-v, --verbose` | 显示详细信息 | false |
| `-h, --help` | 显示帮助信息 | - |

#### 大小格式

支持以下单位：
- `K` / `KB` - 千字节 (1024字节)
- `M` / `MB` - 兆字节 (1024KB)
- `G` / `GB` - 吉字节 (1024MB)

**示例**:
- `128M` = 128 × 1024 × 1024 = 134,217,728 字节
- `1G` = 1024 × 1024 × 1024 = 1,073,741,824 字节

#### 使用示例

##### 示例1: 创建基本文件系统

```bash
./target/release/mkfs-modernfs disk.img --size 256M
```

**输出**:
```
    ╔═══════════════════════════════════════╗
    ║   ModernFS Filesystem Formatter      ║
    ║   C + Rust Hybrid Architecture       ║
    ╚═══════════════════════════════════════╝

📁 Target: disk.img
💾 Total Size: 256 MB
📝 Journal Size: 32 MB
🔢 Block Size: 4096 bytes

Continue? [y/N] y
[00:00:02] ████████████████████████████████████████ 6/6 ✅ Done!

✅ Filesystem created successfully!

Mount with:
  modernfs disk.img /mnt/point
```

##### 示例2: 自定义日志大小

```bash
./target/release/mkfs-modernfs bigdisk.img \
    --size 1G \
    --journal-size 64M \
    --force
```

**说明**:
- 创建1GB文件系统
- 日志区占64MB (约6.25%)
- `--force`跳过确认提示

##### 示例3: 详细输出模式

```bash
./target/release/mkfs-modernfs disk.img \
    --size 512M \
    --verbose
```

**详细输出包含**:
- 磁盘布局详情
- 各区域起始块号
- Inode数量
- 可用数据块数

#### 日志大小建议

| 文件系统大小 | 推荐日志大小 | 说明 |
|-------------|-------------|------|
| < 256MB | 16-32MB | 默认值适用 |
| 256MB-1GB | 32-64MB | 适中 |
| 1GB-4GB | 64-128MB | 大量小文件 |
| > 4GB | 128-256MB | 频繁写入 |

**注意**: 日志越大，可缓存的未提交事务越多，但浪费的空间也越多。

---

### 2.2 fsck-modernfs (文件系统检查)

#### 基本用法

```bash
fsck-modernfs [OPTIONS] <IMAGE>
```

#### 选项说明

| 选项 | 说明 | 默认值 |
|------|------|--------|
| `-v, --verbose` | 显示详细检查信息 | false |
| `-j, --check-journal` | 检查日志一致性 | false |
| `-r, --repair` | 自动修复错误(未实现) | false |
| `-h, --help` | 显示帮助信息 | - |

#### 检查阶段

fsck-modernfs执行6个检查阶段：

1. **阶段1**: 读取并验证超级块
2. **阶段2**: 检查日志超级块
3. **阶段3**: 验证位图一致性
4. **阶段4**: 检查Inode有效性
5. **阶段5**: 检查目录结构
6. **阶段6**: 统计信息汇总

#### 使用示例

##### 示例1: 基本检查

```bash
./target/release/fsck-modernfs disk.img
```

**输出示例**:
```
╔════════════════════════════════════════╗
║  ModernFS Filesystem Check             ║
╚════════════════════════════════════════╝

📁 Checking: disk.img

[Phase 1/6] Checking superblock...
  ✓ Magic: 0x4D4F4446 (MODF)
  ✓ Version: 1
  ✓ Block size: 4096
  ✓ Total blocks: 65536

[Phase 2/6] Checking journal...
  ✓ Journal magic: 0x4A524E4C
  ✓ Sequence: 42
  ⏭️  Skipped (use --check-journal for detailed check)

[Phase 3/6] Checking bitmaps...
  ✓ Inode bitmap OK
  ✓ Data bitmap OK

[Phase 4/6] Checking inodes...
  ✓ Checked 87 inodes
  ✓ No errors

[Phase 5/6] Checking directories...
  ✓ Root directory OK
  ✓ All directories consistent

[Phase 6/6] Summary...
  Total blocks: 65536
  Used blocks: 1243
  Free blocks: 64293
  Total inodes: 1024
  Used inodes: 87
  Free inodes: 937

╔════════════════════════════════════════╗
║  Result: ✅ CLEAN                      ║
║  No errors found                       ║
╚════════════════════════════════════════╝
```

##### 示例2: 详细检查（包含日志）

```bash
./target/release/fsck-modernfs disk.img --verbose --check-journal
```

**额外输出**:
```
[Phase 2/6] Checking journal... (detailed)
  ✓ Journal head: 0
  ✓ Journal tail: 0
  ✓ Active transactions: 0
  ✓ Journal is empty
```

##### 示例3: 检查损坏的文件系统

```bash
./target/release/fsck-modernfs corrupted.img
```

**可能输出**:
```
[Phase 1/6] Checking superblock...
  ❌ Magic mismatch: expected 0x4D4F4446, got 0x00000000

╔════════════════════════════════════════╗
║  Result: ❌ ERRORS FOUND               ║
║  Superblock is corrupted               ║
╚════════════════════════════════════════╝

Suggested actions:
  1. Restore from backup
  2. Try --repair (not implemented yet)
```

#### 何时使用fsck

推荐在以下情况运行fsck：

- ✅ **定期检查**: 每周或每月
- ✅ **异常卸载后**: 进程被kill或系统崩溃
- ✅ **挂载失败时**: 文件系统无法挂载
- ✅ **数据丢失后**: 发现文件丢失或损坏
- ✅ **迁移前**: 迁移到其他系统之前

---

### 2.3 benchmark-modernfs (性能测试)

#### 基本用法

```bash
benchmark-modernfs [OPTIONS] <MOUNT_POINT>
```

**注意**: 文件系统必须已挂载。

#### 选项说明

| 选项 | 说明 | 默认值 |
|------|------|--------|
| `-c, --count <N>` | 操作次数 | 100 |
| `-t, --test <TEST>` | 指定测试类型 | all |
| `-o, --output <FILE>` | 输出结果到文件 | stdout |
| `-h, --help` | 显示帮助信息 | - |

#### 测试类型

| 测试名称 | 说明 | 测试内容 |
|---------|------|---------|
| `seq-write` | 顺序写 | 写入4KB块 |
| `seq-read` | 顺序读 | 读取4KB块 |
| `rand-write` | 随机写 | 随机位置写入 |
| `rand-read` | 随机读 | 随机位置读取 |
| `mkdir` | 创建目录 | 测试元数据性能 |
| `create` | 创建文件 | 测试元数据性能 |
| `all` | 全部测试 | 运行所有测试 |

#### 使用示例

##### 示例1: 完整基准测试

```bash
# 1. 挂载文件系统
sudo ./build/modernfs disk.img /mnt/test -f &

# 2. 运行测试
./target/release/benchmark-modernfs /mnt/test --count 1000
```

**输出示例**:
```
╔════════════════════════════════════════╗
║  ModernFS Performance Benchmark        ║
╚════════════════════════════════════════╝

Mount point: /mnt/test
Operations: 1000 per test

[1/6] Sequential Write (4KB blocks)
  █████████████████████░░░░░░░░░░░░ 60% (600/1000)
  Total: 1000 writes
  Time: 3.12s
  Throughput: 3.21 MB/s
  Avg Latency: 3.12ms
  ✅ PASS

[2/6] Sequential Read (4KB blocks)
  ████████████████████████████████ 100% (1000/1000)
  Total: 1000 reads
  Time: 1.13s
  Throughput: 8.85 MB/s
  Avg Latency: 1.13ms
  ✅ PASS

[3/6] Random Write (4KB blocks)
  ████████████████████████████████ 100% (1000/1000)
  Total: 1000 writes
  Time: 4.21s
  Throughput: 2.38 MB/s
  Avg Latency: 4.21ms
  ✅ PASS

[4/6] Random Read (4KB blocks)
  ████████████████████████████████ 100% (1000/1000)
  Total: 1000 reads
  Time: 1.85s
  Throughput: 5.41 MB/s
  Avg Latency: 1.85ms
  ✅ PASS

[5/6] Mkdir Operations
  ████████████████████████████████ 100% (1000/1000)
  Total: 1000 ops
  Time: 0.46s
  Throughput: 2173 ops/s
  Avg Latency: 0.46ms
  ✅ PASS

[6/6] Create Operations
  ████████████████████████████████ 100% (1000/1000)
  Total: 1000 ops
  Time: 0.62s
  Throughput: 1612 ops/s
  Avg Latency: 0.62ms
  ✅ PASS

╔════════════════════════════════════════╗
║  Summary                                ║
╚════════════════════════════════════════╝
✅ Sequential Write: 3.21 MB/s
✅ Sequential Read:  8.85 MB/s
✅ Random Write:     2.38 MB/s
✅ Random Read:      5.41 MB/s
✅ Metadata Ops:     ~1900 ops/s
```

##### 示例2: 单独测试

```bash
# 只测试顺序写性能
./target/release/benchmark-modernfs /mnt/test \
    --test seq-write \
    --count 5000
```

##### 示例3: 保存结果

```bash
./target/release/benchmark-modernfs /mnt/test \
    --count 1000 \
    --output results.txt
```

#### 性能基准参考

以下是在典型环境下的预期性能（供参考）：

| 操作类型 | ModernFS | tmpfs | ext4 |
|---------|----------|-------|------|
| 顺序写 | 3-5 MB/s | 120 MB/s | 85 MB/s |
| 顺序读 | 8-10 MB/s | 150 MB/s | 110 MB/s |
| 随机写 | 2-3 MB/s | 100 MB/s | 45 MB/s |
| 随机读 | 5-7 MB/s | 120 MB/s | 65 MB/s |
| mkdir | 2000 ops/s | 12000 ops/s | 8000 ops/s |
| create | 1500 ops/s | 10000 ops/s | 6000 ops/s |

**说明**:
- ModernFS是教学项目，性能低于生产级文件系统是正常的
- 性能受WSL虚拟化影响较大
- 主要优势在于崩溃一致性和内存安全

---

### 2.4 modernfs (FUSE驱动)

#### 基本用法

```bash
modernfs <IMAGE> <MOUNT_POINT> [OPTIONS]
```

#### FUSE选项

| 选项 | 说明 |
|------|------|
| `-f` | 前台运行（便于调试） |
| `-d` | 调试模式（显示详细日志） |
| `-s` | 单线程模式 |
| `-o <options>` | FUSE选项 |

#### 常用FUSE选项

```bash
-o allow_other      # 允许其他用户访问
-o default_permissions  # 使用默认权限检查
-o ro               # 只读挂载
-o uid=1000         # 设置文件所有者UID
-o gid=1000         # 设置文件所有者GID
```

#### 使用示例

##### 示例1: 前台挂载（推荐用于测试）

```bash
sudo ./build/modernfs disk.img /mnt/test -f
```

**优点**:
- 可以直接看到日志输出
- Ctrl+C即可卸载
- 便于调试

**输出示例**:
```
[INFO] ModernFS starting...
[INFO] Opening disk image: disk.img
[INFO] Loading superblock...
[INFO] Initializing Journal Manager...
[INFO] Filesystem mounted at /mnt/test
[INFO] Press Ctrl+C to unmount
```

##### 示例2: 后台挂载

```bash
sudo ./build/modernfs disk.img /mnt/test
```

**卸载**:
```bash
sudo fusermount -u /mnt/test
# 或
sudo umount /mnt/test
```

##### 示例3: 允许其他用户访问

```bash
sudo ./build/modernfs disk.img /mnt/test \
    -o allow_other,default_permissions
```

**验证**:
```bash
# 切换到普通用户
su - user1
cd /mnt/test
ls  # 应该可以正常访问
```

##### 示例4: 调试模式

```bash
sudo ./build/modernfs disk.img /mnt/test -f -d
```

**调试输出示例**:
```
FUSE: getattr: /
FUSE: getattr: /test.txt
FUSE: open: /test.txt
FUSE: read: /test.txt offset=0 size=4096
FUSE: release: /test.txt
```

---

## 3. 常见操作

### 3.1 创建和使用文件系统

#### 完整工作流

```bash
# 步骤1: 格式化
./target/release/mkfs-modernfs my_disk.img --size 512M

# 步骤2: 创建挂载点
sudo mkdir -p /mnt/myfs

# 步骤3: 挂载
sudo ./build/modernfs my_disk.img /mnt/myfs -f &

# 等待挂载完成
sleep 2

# 步骤4: 使用文件系统
cd /mnt/myfs

# 创建目录
mkdir projects documents

# 创建文件
echo "My first file" > projects/readme.txt

# 查看内容
cat projects/readme.txt

# 列出目录
ls -lR

# 步骤5: 卸载
cd ~
sudo fusermount -u /mnt/myfs
```

### 3.2 备份和恢复

#### 备份整个文件系统

```bash
# 方法1: 直接复制镜像文件
cp disk.img disk.img.backup

# 方法2: 使用tar压缩
tar czf disk.img.tar.gz disk.img

# 方法3: 增量备份（使用rsync）
rsync -av disk.img /backup/location/
```

#### 恢复

```bash
# 从备份恢复
cp disk.img.backup disk.img

# 或解压
tar xzf disk.img.tar.gz

# 验证完整性
./target/release/fsck-modernfs disk.img
```

#### 迁移到另一台机器

```bash
# 机器A: 打包
tar czf modernfs_backup.tar.gz disk.img

# 传输
scp modernfs_backup.tar.gz user@machine-b:/tmp/

# 机器B: 解包
cd /tmp
tar xzf modernfs_backup.tar.gz

# 检查
./target/release/fsck-modernfs disk.img
```

### 3.3 监控文件系统使用

#### 查看空间使用

```bash
# 挂载后查看
df -h /mnt/myfs
```

**输出示例**:
```
Filesystem      Size  Used Avail Use% Mounted on
modernfs        512M   24M  488M   5% /mnt/myfs
```

#### 查看Inode使用

```bash
df -i /mnt/myfs
```

**输出示例**:
```
Filesystem      Inodes IUsed IFree IUse% Mounted on
modernfs          1024    87   937    9% /mnt/myfs
```

### 3.4 性能优化

#### 调整日志大小

```bash
# 创建时指定较大的日志（适合频繁写入）
./target/release/mkfs-modernfs disk.img \
    --size 1G \
    --journal-size 128M
```

#### 使用RAM disk提升性能

```bash
# 创建RAM disk
sudo mkdir /mnt/ramdisk
sudo mount -t tmpfs -o size=512M tmpfs /mnt/ramdisk

# 在RAM disk上创建文件系统镜像
./target/release/mkfs-modernfs /mnt/ramdisk/disk.img --size 256M

# 挂载
sudo ./build/modernfs /mnt/ramdisk/disk.img /mnt/myfs
```

**性能提升**: 约3-5倍

---

## 4. 故障排除

### 4.1 挂载失败

#### 问题: `fuse: failed to mount`

**可能原因**:
1. 挂载点不存在
2. 挂载点已被占用
3. 权限不足
4. FUSE模块未加载

**解决方案**:

```bash
# 1. 检查挂载点
ls -ld /mnt/test
# 如不存在
sudo mkdir -p /mnt/test

# 2. 检查是否已挂载
mount | grep /mnt/test
# 如已挂载，先卸载
sudo fusermount -u /mnt/test

# 3. 检查权限
sudo chmod 755 /mnt/test

# 4. 加载FUSE模块 (Linux)
sudo modprobe fuse

# 5. 检查用户权限
groups  # 确认在fuse组中
# 如没有，添加
sudo usermod -a -G fuse $USER
# 重新登录生效
```

#### 问题: `Permission denied`

**解决**:
```bash
# 使用sudo
sudo ./build/modernfs disk.img /mnt/test

# 或添加allow_other选项
sudo ./build/modernfs disk.img /mnt/test -o allow_other
```

### 4.2 性能问题

#### 问题: 写入很慢

**可能原因**:
1. WSL虚拟化开销
2. Journal过大
3. 磁盘I/O瓶颈

**优化措施**:

```bash
# 1. 使用原生Linux（不使用WSL）

# 2. 重新格式化，减小日志
./target/release/mkfs-modernfs disk.img \
    --size 256M \
    --journal-size 16M

# 3. 使用SSD存储镜像文件

# 4. 禁用不必要的日志
# (需要修改代码，减少eprintln!输出)
```

#### 问题: 读取很慢

**检查缓存命中率**:
```bash
# 运行benchmark查看性能
./target/release/benchmark-modernfs /mnt/test
```

**如果性能异常低**:
- 检查磁盘健康状态
- 确认没有其他进程占用I/O
- 考虑增大Buffer Cache (需修改CMakeLists.txt)

### 4.3 数据丢失

#### 问题: 文件突然消失

**可能原因**:
1. 进程被强制终止（kill -9）
2. 未正确卸载
3. 系统崩溃

**恢复步骤**:

```bash
# 1. 卸载文件系统（如果还挂载着）
sudo fusermount -u /mnt/test

# 2. 运行fsck检查
./target/release/fsck-modernfs disk.img --verbose

# 3. 重新挂载，触发自动恢复
sudo ./build/modernfs disk.img /mnt/test -f
# 观察恢复信息:
# [RECOVERY] Starting journal recovery...
# [RECOVERY] Recovered X transactions
```

**预防措施**:
- ✅ 使用正确的卸载命令（fusermount -u）
- ✅ 不要使用kill -9终止进程
- ✅ 定期备份重要数据
- ✅ 定期运行fsck检查

#### 问题: 超级块损坏

**症状**:
```
fsck-modernfs: Magic mismatch
```

**恢复**:
```bash
# 如有备份
cp disk.img.backup disk.img

# 如无备份，数据可能无法恢复
echo "Lesson learned: Always backup!"
```

### 4.4 崩溃和卡死

#### 问题: FUSE进程崩溃

**查看崩溃信息**:
```bash
dmesg | tail -20
```

**调试步骤**:
```bash
# 1. 以调试模式重新运行
sudo ./build/modernfs disk.img /mnt/test -f -d

# 2. 观察崩溃前的日志

# 3. 如果是C代码bug，使用gdb
sudo gdb --args ./build/modernfs disk.img /mnt/test -f
(gdb) run
# 崩溃后
(gdb) backtrace
```

#### 问题: 挂载后无法访问

**症状**:
```bash
$ ls /mnt/test
ls: cannot access '/mnt/test': Transport endpoint is not connected
```

**解决**:
```bash
# FUSE进程可能已崩溃，强制卸载
sudo fusermount -u /mnt/test

# 或使用umount -l (lazy unmount)
sudo umount -l /mnt/test

# 重新挂载
sudo ./build/modernfs disk.img /mnt/test -f
```

---

## 5. 限制与注意事项

### 5.1 已知限制

| 限制项 | 说明 | 影响 |
|-------|------|------|
| **平台** | FUSE功能仅Linux可用 | Windows用户需使用WSL |
| **最大文件大小** | 约4GB | 超过需多个文件 |
| **最大文件系统** | 理论无限，推荐<4GB | 性能考虑 |
| **并发挂载** | 不支持同一镜像多次挂载 | 数据损坏风险 |
| **软链接** | 不支持 | 使用目录代替 |
| **硬链接** | 不支持 | 拷贝文件 |
| **扩展属性** | 不支持 | 无ACL等高级特性 |
| **配额** | 不支持 | 无法限制用户使用 |
| **加密** | 不支持 | 使用外部工具(dm-crypt) |

### 5.2 使用建议

#### 适用场景
- ✅ **学习和研究**: 了解文件系统原理
- ✅ **原型开发**: 快速测试想法
- ✅ **小规模数据**: < 1GB的临时存储
- ✅ **测试环境**: 非生产环境

#### 不适用场景
- ❌ **生产环境**: 使用ext4/xfs/btrfs
- ❌ **大文件**: 使用专业文件系统
- ❌ **关键数据**: 始终需要备份
- ❌ **高性能**: 使用内核文件系统

### 5.3 安全建议

1. **备份**:
   ```bash
   # 每天备份
   cp disk.img disk.img.$(date +%Y%m%d)
   ```

2. **定期检查**:
   ```bash
   # 每周运行fsck
   ./target/release/fsck-modernfs disk.img
   ```

3. **正确卸载**:
   ```bash
   # 好的做法
   cd ~
   sudo fusermount -u /mnt/test

   # 不好的做法
   sudo kill -9 <pid>  # ❌ 可能丢数据
   ```

4. **监控空间**:
   ```bash
   # 避免写满
   df -h /mnt/test
   ```

---

## 6. FAQ

### Q1: ModernFS可以在Windows上使用吗？

**A**:
- ✅ 可以编译Rust工具(mkfs, fsck, benchmark)
- ❌ 不能挂载文件系统（FUSE不支持Windows）
- ✅ 可以使用WSL获得完整功能

```bash
# Windows PowerShell
wsl ./target/release/mkfs-modernfs disk.img --size 256M
wsl sudo ./build/modernfs disk.img /mnt/test
```

### Q2: 性能如何？

**A**: ModernFS是教学项目，性能低于生产级文件系统是正常的。

**典型性能**:
- 顺序写: 3-5 MB/s
- 顺序读: 8-10 MB/s
- 元数据: 2000 ops/s

**对比**:
- ext4顺序写: 80-100 MB/s
- tmpfs顺序写: 120+ MB/s

**性能优势**: 不在速度，而在于崩溃一致性和内存安全。

### Q3: 支持加密吗？

**A**: 当前版本不支持。

**替代方案**:
```bash
# 使用dm-crypt加密底层设备
sudo cryptsetup luksFormat disk.img
sudo cryptsetup open disk.img mydisk
sudo ./build/modernfs /dev/mapper/mydisk /mnt/test
```

### Q4: 可以调整已创建文件系统的大小吗？

**A**:
- ❌ 不支持在线调整
- ❌ 不支持离线调整
- ✅ 可以重新格式化（会丢失数据）

**迁移方案**:
```bash
# 1. 备份数据
tar czf backup.tar.gz -C /mnt/old /

# 2. 创建新文件系统
./target/release/mkfs-modernfs new_disk.img --size 1G

# 3. 挂载并恢复
sudo ./build/modernfs new_disk.img /mnt/new
tar xzf backup.tar.gz -C /mnt/new
```

### Q5: 如何卸载？

**A**: 三种方法：

```bash
# 方法1: fusermount (推荐)
sudo fusermount -u /mnt/test

# 方法2: umount
sudo umount /mnt/test

# 方法3: Ctrl+C (前台模式)
# 在运行modernfs的终端按Ctrl+C
```

### Q6: 支持权限管理吗？

**A**:
- ✅ 支持基本权限 (chmod)
- ✅ 支持所有者 (chown)
- ❌ 不支持ACL
- ❌ 不支持扩展属性

```bash
chmod 644 file.txt    # ✅ 可以
chown user:group file # ✅ 可以
setfacl ...           # ❌ 不支持
```

### Q7: 可以从损坏的镜像恢复数据吗？

**A**:
- ✅ 如果超级块完好，运行fsck
- ✅ 如果有Journal，尝试恢复
- ❌ 如果超级块损坏，需要备份

```bash
# 尝试恢复
./target/release/fsck-modernfs disk.img --verbose

# 如果恢复失败，从备份还原
cp disk.img.backup disk.img
```

### Q8: 日志多大合适？

**A**: 根据使用模式：

- **频繁小文件写入**: 大日志 (64-128MB)
- **偶尔大文件写入**: 小日志 (16-32MB)
- **只读使用**: 最小日志 (16MB)

**计算公式**:
```
journal_size = 峰值并发写入量 × 平均块数 × 4KB × 2
```

### Q9: 支持软链接吗？

**A**:
- ❌ 当前版本不支持
- ✅ 可以使用目录代替

```bash
# 不支持
ln -s target link  # ❌

# 使用目录
mkdir target
mkdir link
mount --bind target link  # ✅ (但不是真正的软链接)
```

### Q10: 如何获取帮助？

**A**:
1. **查看文档**: `docs/`目录
2. **运行help**: `mkfs-modernfs --help`
3. **GitHub Issues**: 报告bug
4. **源码**: 查看实现细节

---

## 附录

### A. 磁盘布局示意图

```
┌─────────────────────────────────────┐
│ Block 0: Superblock                 │
├─────────────────────────────────────┤
│ Blocks 1-8192: Journal (32MB)       │
├─────────────────────────────────────┤
│ Block 8193: Inode Bitmap            │
├─────────────────────────────────────┤
│ Blocks 8194-8225: Data Bitmap       │
├─────────────────────────────────────┤
│ Blocks 8226-8257: Inode Table       │
├─────────────────────────────────────┤
│ Blocks 8258+: Data Blocks           │
└─────────────────────────────────────┘
```

### B. 错误码对照表

| 错误码 | 说明 |
|-------|------|
| `ENOENT` | 文件或目录不存在 |
| `EEXIST` | 文件已存在 |
| `ENOSPC` | 磁盘空间不足 |
| `EIO` | I/O错误 |
| `EINVAL` | 参数无效 |
| `ENOTDIR` | 不是目录 |
| `EISDIR` | 是目录 |
| `ENOTEMPTY` | 目录非空 |

### C. 推荐资源

- **源码**: https://github.com/yourname/modernfs
- **文档**: `docs/IMPLEMENTATION.md`
- **教程**: `docs/DEVELOPER.md`

---

**文档版本**: 1.0.0
**最后更新**: 2025-10-07
**维护者**: ModernFS Team

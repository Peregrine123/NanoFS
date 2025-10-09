# ModernFS 内存调试指南

本文档提供了 ModernFS 项目中内存问题调试的工具和方法。

## 🔍 问题症状

当前项目中发现的内存相关错误：
- `Fatal glibc error: malloc.c:2599 (sysmalloc): assertion failed`
- `double free or corruption (!prev)`
- 在 Buffer Cache 和 Journal 操作中出现

## 🛠️ 调试工具

### 1. AddressSanitizer (ASan)

**推荐用于 C 代码内存错误检测**

#### 配置方法

在 `CMakeLists.txt` 中添加编译选项：

```cmake
# 添加到文件开头，在 set(CMAKE_C_STANDARD 11) 之后
option(ENABLE_ASAN "Enable AddressSanitizer" OFF)

if(ENABLE_ASAN)
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address)
    message(STATUS "AddressSanitizer enabled")
endif()
```

#### 使用方法

```bash
# 重新配置 CMake 并启用 ASan
cd build
cmake -DENABLE_ASAN=ON ..
make

# 运行测试
./test_block_layer

# ASan 会自动报告内存错误，输出详细堆栈
```

#### 环境变量

```bash
# 在崩溃时生成 core dump
export ASAN_OPTIONS=abort_on_error=1

# 检测更多类型的错误
export ASAN_OPTIONS=detect_leaks=1:check_initialization_order=1
```

### 2. Valgrind

**适用于复杂内存泄漏检测**

#### 安装

```bash
# Ubuntu/Debian
sudo apt install valgrind

# WSL
wsl sudo apt install valgrind
```

#### 使用方法

```bash
# 基本内存检查
valgrind --leak-check=full --show-leak-kinds=all ./build/test_block_layer

# 详细输出
valgrind --leak-check=full \
         --show-leak-kinds=all \
         --track-origins=yes \
         --verbose \
         --log-file=valgrind-out.txt \
         ./build/test_block_layer
```

#### Valgrind 输出解读

- **Definitely lost**: 明确的内存泄漏，需要修复
- **Indirectly lost**: 由于父结构泄漏导致的泄漏
- **Possibly lost**: 可能的泄漏，需要检查
- **Still reachable**: 程序结束时仍可达的内存（通常可以忽略）

### 3. Rust Sanitizer

**用于检测 Rust 端的内存安全问题**

#### 配置方法

编辑 `rust_core/Cargo.toml`：

```toml
[profile.dev]
opt-level = 0

[profile.release]
opt-level = 3
lto = true

# 添加调试配置
[profile.debug-asan]
inherits = "dev"
```

#### 使用方法

```bash
# 构建 Rust 代码时启用 ASan
cd rust_core
RUSTFLAGS="-Z sanitizer=address" cargo +nightly build --target x86_64-unknown-linux-gnu

# 或使用脚本
cd ..
./build_with_asan.sh
```

创建 `build_with_asan.sh`：

```bash
#!/bin/bash
set -e

echo "Building Rust with AddressSanitizer..."
cd rust_core
RUSTFLAGS="-Z sanitizer=address" cargo +nightly build --target x86_64-unknown-linux-gnu
cd ..

echo "Building C with AddressSanitizer..."
mkdir -p build
cd build
cmake -DENABLE_ASAN=ON ..
make

echo "Done! Run tests with ASan enabled."
```

### 4. GDB 调试

**用于定位崩溃位置**

#### 使用方法

```bash
# 编译时带调试符号（默认已启用 -g）
gdb ./build/test_block_layer

# GDB 命令
(gdb) run                    # 运行程序
(gdb) bt                     # 崩溃后查看堆栈
(gdb) frame 3                # 跳转到堆栈帧 3
(gdb) print variable_name    # 查看变量值
(gdb) info locals            # 查看所有本地变量
```

#### 调试 Rust FFI 边界

```bash
# 设置断点
(gdb) break rust_journal_init
(gdb) break rust_journal_begin
(gdb) break rust_journal_commit

# 运行并检查参数
(gdb) run
(gdb) info args
(gdb) print *jm
```

## 🔬 常见内存问题排查

### 问题 1: Buffer Cache 内存错误

**症状**: `malloc assertion failed` 或 `double free`

**排查步骤**:

1. **检查 LRU 链表操作**
   ```bash
   # 在 buffer_cache.c 中添加断言
   assert(buf->next != buf);  // 防止自环
   assert(buf->prev != buf);
   ```

2. **验证引用计数**
   ```c
   // 在 get_buffer() 中
   assert(buf->refcount >= 0);

   // 在 release_buffer() 中
   assert(buf->refcount > 0);
   if (buf->refcount == 0) {
       fprintf(stderr, "ERROR: Double release of buffer %u\n", buf->block_num);
       abort();
   }
   ```

3. **使用 Valgrind 检测**
   ```bash
   valgrind --leak-check=full ./build/test_block_layer
   ```

### 问题 2: Rust FFI 内存生命周期

**症状**: `double free` 或访问已释放内存

**排查步骤**:

1. **检查 Box::from_raw() 调用**
   ```rust
   // 每个 Box::into_raw() 应该有对应的 Box::from_raw()
   // 搜索所有 FFI 函数中的 from_raw
   ```

2. **验证所有权传递**
   ```rust
   // 确保 C 端不会多次调用 destroy
   #[no_mangle]
   pub extern "C" fn rust_journal_destroy(jm: *mut JournalManager) {
       if jm.is_null() {
           eprintln!("WARN: Attempting to destroy null JournalManager");
           return;
       }
       unsafe {
           let _ = Box::from_raw(jm);
           eprintln!("DEBUG: JournalManager destroyed at {:p}", jm);
       }
   }
   ```

3. **添加 Rust 端日志**
   ```bash
   RUST_LOG=debug RUST_BACKTRACE=1 ./build/test_journal
   ```

### 问题 3: Week 7 集成测试崩溃

**症状**: 在 checkpoint 阶段出现 `double free`

**可能原因**:
- Journal Manager 和 Extent Allocator 共享 fd，但内部都可能关闭它
- Transaction 被提交后又被访问

**修复建议**:

1. **分离 fd 所有权**
   ```c
   // 每个组件使用 dup(fd) 获取独立的文件描述符
   int journal_fd = dup(fd);
   int extent_fd = dup(fd);
   ```

2. **添加事务状态检查**
   ```rust
   impl Transaction {
       pub fn is_committed(&self) -> bool {
           self.state == TxnState::Committed
       }
   }

   // 在写入前检查
   if txn.is_committed() {
       return Err(anyhow!("Cannot write to committed transaction"));
   }
   ```

## 📋 调试检查清单

在提交代码前，运行以下检查：

- [ ] ASan 构建通过所有测试
  ```bash
  cd build && cmake -DENABLE_ASAN=ON .. && make && ./test_block_layer
  ```

- [ ] Valgrind 无明确内存泄漏
  ```bash
  valgrind --leak-check=full --error-exitcode=1 ./test_block_layer
  ```

- [ ] Rust 测试通过
  ```bash
  cd rust_core && cargo test
  ```

- [ ] 所有 FFI 函数都有 null 检查
  ```rust
  if ptr.is_null() { return -1; }
  ```

- [ ] 所有 malloc/free 配对
  ```c
  // 搜索 malloc 和对应的 free
  grep -n "malloc" src/*.c
  ```

## 🚀 快速修复建议

### 立即行动项（阻塞问题）

1. **修复 Week 7 集成测试**
   - 使用 `dup(fd)` 分离文件描述符
   - 在 `fs_context.c` 中管理 fd 生命周期

2. **添加 ASan 支持到构建系统**
   - 修改 `CMakeLists.txt` 添加 `ENABLE_ASAN` 选项
   - 创建 `build_debug.sh` 脚本

3. **增强错误检查**
   - 在所有 FFI 边界添加参数验证
   - 在关键路径添加 `assert()`

### 中期优化（1-2周）

1. **添加内存审计日志**
   ```c
   #define DEBUG_ALLOC
   #ifdef DEBUG_ALLOC
   #define MALLOC(size) ({ \
       void* p = malloc(size); \
       fprintf(stderr, "ALLOC %p size=%zu at %s:%d\n", p, size, __FILE__, __LINE__); \
       p; \
   })
   #else
   #define MALLOC(size) malloc(size)
   #endif
   ```

2. **实现自动化测试**
   - CI 中运行 ASan 和 Valgrind
   - 每次提交自动检测内存问题

## 📚 参考资料

- [AddressSanitizer Documentation](https://github.com/google/sanitizers/wiki/AddressSanitizer)
- [Valgrind User Manual](https://valgrind.org/docs/manual/manual.html)
- [Rust FFI Memory Safety](https://doc.rust-lang.org/nomicon/ffi.html)
- [GDB Cheat Sheet](https://darkdust.net/files/GDB%20Cheat%20Sheet.pdf)

---

**最后更新**: 2025-10-09
**维护者**: ModernFS Team

# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**ModernFS** is a hybrid C + Rust filesystem implementation with FUSE interface, designed for educational purposes. The project demonstrates crash-consistent filesystem design with Write-Ahead Logging (WAL).

**Architecture**: C handles FUSE interface and basic I/O, Rust implements safety-critical components (Journal Manager, Extent Allocator).

**Current Status**: Week 5 completed - Journal Manager (WAL) implemented and tested.

## Build & Test Commands

### Environment Setup

**Important**: This project requires both Rust and C toolchains. On Windows, always use WSL for Rust-related builds and tests.

```bash
# Install Rust (if not present)
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
source $HOME/.cargo/env

# Install system dependencies (Linux/WSL)
sudo apt install build-essential cmake libfuse3-dev
```

### Building

**On Linux/WSL** (recommended):
```bash
# Full build (Rust + C)
./build.sh

# Or manually:
cargo build --release           # Build Rust core
mkdir -p build && cd build
cmake .. && make                # Build C components
```

**On Windows** (C-only components):
```cmd
build.bat
```

### Running Tests

**Always use WSL for tests that involve Rust components:**

```bash
# FFI tests (basic Rust/C integration)
wsl bash -c "cd /mnt/e/ampa_migra/D/校务/大三上/OS/NanoFS && ./build/test_ffi"

# Block layer tests (C only, Week 2)
./build/test_block_layer

# Inode layer tests (C only, Week 3)
./build/test_inode_layer

# Journal Manager tests (Rust + C, Week 5)
wsl bash -c "cd /mnt/e/ampa_migra/D/校务/大三上/OS/NanoFS && ./build/test_journal"
```

**Single test compilation:**
```bash
cd build
make test_journal              # Build specific test
./test_journal                 # Run it
```

### FUSE Operations (Linux only)

```bash
# Format a filesystem image
./build/mkfs.modernfs disk.img 128M

# Mount filesystem
./build/modernfs disk.img /mnt/test -f

# In another terminal, test operations
cd /mnt/test
echo "test" > file.txt
cat file.txt

# Unmount
fusermount -u /mnt/test
```

## Architecture Details

### Hybrid C/Rust Design

**C Components** (`src/*.c`):
- **FUSE Interface**: `fuse_ops.c`, `main_fuse.c` - POSIX syscall handlers
- **Block Layer**: `block_dev.c`, `buffer_cache.c` - I/O and caching (LRU)
- **Allocation**: `block_alloc.c` - Simple bitmap allocator
- **Inode Layer**: `inode.c` - Manages inodes with LRU cache, supports up to ~4GB files
- **Directory**: `directory.c` - Variable-length directory entries
- **Path**: `path.c` - Path normalization and lookup

**Rust Components** (`rust_core/src/`):
- **Journal Manager** (`journal/`): WAL logging with ACID transactions
  - State machine: Active → Committed → Checkpointed
  - Crash recovery on mount
  - Uses `Arc<Mutex<Transaction>>` for thread-safety
- **Extent Allocator** (`extent/`): Planned, not yet implemented
- **FFI Layer** (`lib.rs`): C-compatible exports using `#[no_mangle]`

### FFI Interface

All Rust ↔ C communication goes through `include/modernfs/rust_ffi.h`:

```c
// C side: Opaque pointers
typedef struct RustJournalManager RustJournalManager;
typedef struct RustTransaction RustTransaction;

RustJournalManager* rust_journal_init(int fd, uint32_t start, uint32_t blocks);
RustTransaction* rust_journal_begin(RustJournalManager* jm);
int rust_journal_write(RustTransaction* txn, uint32_t block, const uint8_t* data);
int rust_journal_commit(RustJournalManager* jm, RustTransaction* txn);
```

**Memory Management**:
- Rust owns all allocations (via `Box::into_raw`)
- C receives opaque pointers
- Must call `rust_*_destroy()` to free

### Key Data Structures

**Disk Layout**:
```
[SuperBlock | Journal | Inode Bitmap | Data Bitmap | Inode Table | Data Blocks]
```

**Inode** (C):
- 12 direct blocks + 1 indirect + 1 double-indirect
- Max file size: ~4GB (1,049,600 blocks × 4KB)
- LRU cache: 64 inodes

**Journal** (Rust):
- Circular buffer in dedicated disk region
- Each transaction: [Data Blocks...] + [Commit Record]
- Checkpoint moves data from journal → final location

**Buffer Cache** (C):
- Hash table (2048 buckets) + LRU list
- 1024 blocks (4MB default)
- Thread-safe with `pthread_rwlock_t`

## Development Workflow

### Adding a New C Module

1. Create source in `src/` and header in `include/modernfs/`
2. Update `CMakeLists.txt`:
   ```cmake
   add_executable(my_test src/my_test.c src/my_module.c ...)
   target_link_libraries(my_test pthread m)
   ```
3. Write tests following existing patterns (see `test_inode_layer.c`)

### Adding Rust Functionality

1. Implement in `rust_core/src/`
2. Export FFI in `lib.rs`:
   ```rust
   #[no_mangle]
   pub extern "C" fn rust_my_function(...) -> i32 {
       catch_panic(|| {
           // Implementation
       })
   }
   ```
3. Declare in `include/modernfs/rust_ffi.h`
4. Update `CMakeLists.txt` to link `librust_core.a`

### Common Pitfalls

**Packed Structs in Rust**:
- Don't reference fields directly: `eprintln!("{}", sb.head)` ❌
- Copy first: `let head = sb.head; eprintln!("{}", head)` ✅

**WSL Paths**:
- Windows: `E:\path\to\NanoFS`
- WSL: `/mnt/e/path/to/NanoFS`

**FFI Lifetime Management**:
- `rust_journal_begin()` returns `Box<Arc<Mutex<Transaction>>>`
- `rust_journal_write()` borrows it mutably
- `rust_journal_commit()` consumes it with `Box::from_raw()`

## Testing Strategy

- **Unit tests**: Individual C modules (e.g., `test_block_layer.c`)
- **Integration tests**: Rust/C interaction (e.g., `test_journal.c`)
- **FUSE tests**: Shell scripts in `tests/scripts/`

All tests should be self-contained and clean up after themselves (delete `.img` files).

## Project Phases

- ✅ **Week 1**: Environment setup, basic FFI
- ✅ **Week 2**: Block device, buffer cache, bitmap allocator
- ✅ **Week 3**: Inode management, directory operations, path resolution
- ✅ **Week 4**: FUSE integration (basic file ops)
- ✅ **Week 5**: Journal Manager (WAL) in Rust
- 🔄 **Week 6**: Extent Allocator in Rust
- 📋 **Week 7**: Integrate Journal into FUSE write path
- 📋 **Week 8**: Rust tools (mkfs, fsck, benchmarks)

## Code Style

**C**:
- Follow xv6 style where applicable
- Use `snake_case` for functions
- Prefix types with module name: `block_device_t`, `inode_t`

**Rust**:
- Follow standard Rust conventions
- Use `Result<T>` for all fallible operations
- Prefer `eprintln!` for logging (goes to stderr)

## References

- [FUSE Documentation](https://libfuse.github.io/)
- [Rust FFI Guide](https://doc.rust-lang.org/nomicon/ffi.html)
- [xv6 Filesystem](https://pdos.csail.mit.edu/6.828/2020/xv6/book-riscv-rev1.pdf) - Original inspiration
- Weekly reports in `docs/WEEK*_REPORT.md` - Detailed implementation notes

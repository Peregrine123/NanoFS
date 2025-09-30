// rust_core/src/lib.rs

mod journal;
mod extent;
mod transaction;

pub use journal::JournalManager;
pub use extent::ExtentAllocator;

use std::os::raw::c_void;
use std::ptr;

// ============ 错误处理辅助 ============

fn catch_panic<F, R>(f: F) -> R
where
    F: FnOnce() -> R + std::panic::UnwindSafe,
    R: Default,
{
    std::panic::catch_unwind(f).unwrap_or_else(|e| {
        eprintln!("Rust panic: {:?}", e);
        R::default()
    })
}

// ============ Hello World FFI测试 ============

#[no_mangle]
pub extern "C" fn rust_hello_world() -> *const u8 {
    b"Hello from Rust!\0".as_ptr()
}

#[no_mangle]
pub extern "C" fn rust_add(a: i32, b: i32) -> i32 {
    a + b
}

// Journal FFI接口的占位符
#[no_mangle]
pub extern "C" fn rust_journal_init(
    _device_fd: i32,
    _journal_start: u32,
    _journal_blocks: u32,
) -> *mut c_void {
    eprintln!("[Rust] Journal Manager initialization - placeholder");
    ptr::null_mut()
}

#[no_mangle]
pub extern "C" fn rust_journal_destroy(_jm_ptr: *mut c_void) {
    eprintln!("[Rust] Journal Manager destroy - placeholder");
}

// Extent Allocator FFI接口的占位符
#[no_mangle]
pub extern "C" fn rust_extent_alloc_init(
    _device_fd: i32,
    _bitmap_start: u32,
    _total_blocks: u32,
) -> *mut c_void {
    eprintln!("[Rust] Extent Allocator initialization - placeholder");
    ptr::null_mut()
}

#[no_mangle]
pub extern "C" fn rust_extent_alloc_destroy(_alloc_ptr: *mut c_void) {
    eprintln!("[Rust] Extent Allocator destroy - placeholder");
}
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

// ============ Journal FFI接口 ============

#[no_mangle]
pub extern "C" fn rust_journal_init(
    device_fd: i32,
    journal_start: u32,
    journal_blocks: u32,
) -> *mut c_void {
    catch_panic(|| {
        match JournalManager::new(device_fd, journal_start, journal_blocks) {
            Ok(jm) => Box::into_raw(Box::new(jm)) as *mut c_void,
            Err(e) => {
                eprintln!("[FFI] rust_journal_init failed: {:?}", e);
                ptr::null_mut()
            }
        }
    })
}

#[no_mangle]
pub extern "C" fn rust_journal_begin(jm_ptr: *mut c_void) -> *mut c_void {
    if jm_ptr.is_null() {
        return ptr::null_mut();
    }

    catch_panic(|| {
        let jm = unsafe { &*(jm_ptr as *const JournalManager) };
        match jm.begin_transaction() {
            Ok(txn) => Box::into_raw(Box::new(txn)) as *mut c_void,
            Err(e) => {
                eprintln!("[FFI] rust_journal_begin failed: {:?}", e);
                ptr::null_mut()
            }
        }
    })
}

#[no_mangle]
pub extern "C" fn rust_journal_write(
    txn_ptr: *mut c_void,
    block_num: u32,
    data: *const u8,
) -> i32 {
    if txn_ptr.is_null() || data.is_null() {
        return -libc::EINVAL;
    }

    catch_panic(|| {
        use std::sync::{Arc, Mutex};
        use journal::types::Transaction;

        let txn = unsafe { &mut *(txn_ptr as *mut Arc<Mutex<Transaction>>) };
        let data_slice = unsafe { std::slice::from_raw_parts(data, 4096) };

        match txn.lock().unwrap().write_block(block_num, data_slice.to_vec()) {
            Ok(()) => 0,
            Err(e) => {
                eprintln!("[FFI] rust_journal_write failed: {:?}", e);
                -libc::EIO
            }
        }
    })
}

#[no_mangle]
pub extern "C" fn rust_journal_commit(
    jm_ptr: *mut c_void,
    txn_ptr: *mut c_void,
) -> i32 {
    if jm_ptr.is_null() || txn_ptr.is_null() {
        return -libc::EINVAL;
    }

    catch_panic(|| {
        use std::sync::{Arc, Mutex};
        use journal::types::Transaction;

        let jm = unsafe { &*(jm_ptr as *const JournalManager) };
        let txn = unsafe { Box::from_raw(txn_ptr as *mut Arc<Mutex<Transaction>>) };

        match jm.commit(*txn) {
            Ok(()) => 0,
            Err(e) => {
                eprintln!("[FFI] rust_journal_commit failed: {:?}", e);
                -libc::EIO
            }
        }
    })
}

#[no_mangle]
pub extern "C" fn rust_journal_abort(txn_ptr: *mut c_void) {
    if txn_ptr.is_null() {
        return;
    }

    catch_panic(|| {
        use std::sync::{Arc, Mutex};
        use journal::types::Transaction;

        let txn = unsafe { Box::from_raw(txn_ptr as *mut Arc<Mutex<Transaction>>) };
        let mut txn_inner = txn.lock().unwrap();
        txn_inner.state = journal::types::TxnState::Aborted;
        eprintln!("[FFI] Transaction {} aborted", txn_inner.id);
    })
}

#[no_mangle]
pub extern "C" fn rust_journal_checkpoint(jm_ptr: *mut c_void) -> i32 {
    if jm_ptr.is_null() {
        return -libc::EINVAL;
    }

    catch_panic(|| {
        let jm = unsafe { &*(jm_ptr as *const JournalManager) };
        match jm.checkpoint() {
            Ok(()) => 0,
            Err(e) => {
                eprintln!("[FFI] rust_journal_checkpoint failed: {:?}", e);
                -libc::EIO
            }
        }
    })
}

#[no_mangle]
pub extern "C" fn rust_journal_recover(jm_ptr: *mut c_void) -> i32 {
    if jm_ptr.is_null() {
        return -libc::EINVAL;
    }

    catch_panic(|| {
        let jm = unsafe { &*(jm_ptr as *const JournalManager) };
        match jm.recover() {
            Ok(count) => count as i32,
            Err(e) => {
                eprintln!("[FFI] rust_journal_recover failed: {:?}", e);
                -libc::EIO
            }
        }
    })
}

#[no_mangle]
pub extern "C" fn rust_journal_destroy(jm_ptr: *mut c_void) {
    if !jm_ptr.is_null() {
        catch_panic(|| unsafe {
            let _ = Box::from_raw(jm_ptr as *mut JournalManager);
        });
    }
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
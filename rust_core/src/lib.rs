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
            Ok(()) => {
                eprintln!("[FFI] rust_journal_write: wrote block {} to transaction", block_num);
                0
            },
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
    eprintln!("[FFI] rust_journal_destroy called with ptr={:p}", jm_ptr);
    if !jm_ptr.is_null() {
        catch_panic(|| unsafe {
            eprintln!("[FFI] About to drop JournalManager at {:p}", jm_ptr);
            let jm = Box::from_raw(jm_ptr as *mut JournalManager);
            drop(jm);
            eprintln!("[FFI] JournalManager dropped successfully at {:p}", jm_ptr);
        });
    } else {
        eprintln!("[FFI] rust_journal_destroy called with null pointer");
    }
}

// ============ Extent Allocator FFI接口 ============

#[no_mangle]
pub extern "C" fn rust_extent_alloc_init(
    device_fd: i32,
    bitmap_start: u32,
    total_blocks: u32,
) -> *mut c_void {
    catch_panic(|| {
        match ExtentAllocator::new(device_fd, bitmap_start, total_blocks) {
            Ok(allocator) => Box::into_raw(Box::new(allocator)) as *mut c_void,
            Err(e) => {
                eprintln!("[FFI] rust_extent_alloc_init failed: {:?}", e);
                ptr::null_mut()
            }
        }
    })
}

#[no_mangle]
pub extern "C" fn rust_extent_alloc(
    alloc_ptr: *mut c_void,
    hint: u32,
    min_len: u32,
    max_len: u32,
    out_start: *mut u32,
    out_len: *mut u32,
) -> i32 {
    if alloc_ptr.is_null() || out_start.is_null() || out_len.is_null() {
        return -libc::EINVAL;
    }

    catch_panic(|| {
        let allocator = unsafe { &*(alloc_ptr as *const ExtentAllocator) };

        match allocator.allocate_extent(hint, min_len, max_len) {
            Ok(extent) => {
                unsafe {
                    *out_start = extent.start;
                    *out_len = extent.length;
                }
                0
            }
            Err(e) => {
                eprintln!("[FFI] rust_extent_alloc failed: {:?}", e);
                -libc::ENOSPC
            }
        }
    })
}

#[no_mangle]
pub extern "C" fn rust_extent_free(
    alloc_ptr: *mut c_void,
    start: u32,
    length: u32,
) -> i32 {
    if alloc_ptr.is_null() {
        return -libc::EINVAL;
    }

    catch_panic(|| {
        use extent::Extent;

        let allocator = unsafe { &*(alloc_ptr as *const ExtentAllocator) };
        let extent = Extent::new(start, length);

        match allocator.free_extent(&extent) {
            Ok(()) => 0,
            Err(e) => {
                eprintln!("[FFI] rust_extent_free failed: {:?}", e);
                -libc::EINVAL
            }
        }
    })
}

#[no_mangle]
pub extern "C" fn rust_extent_fragmentation(alloc_ptr: *mut c_void) -> f32 {
    if alloc_ptr.is_null() {
        return -1.0;
    }

    catch_panic(|| {
        let allocator = unsafe { &*(alloc_ptr as *const ExtentAllocator) };
        allocator.fragmentation_ratio()
    })
}

#[no_mangle]
pub extern "C" fn rust_extent_get_stats(
    alloc_ptr: *mut c_void,
    out_total: *mut u32,
    out_free: *mut u32,
    out_allocated: *mut u32,
) -> i32 {
    if alloc_ptr.is_null() || out_total.is_null() || out_free.is_null() || out_allocated.is_null() {
        return -libc::EINVAL;
    }

    catch_panic(|| {
        let allocator = unsafe { &*(alloc_ptr as *const ExtentAllocator) };
        let stats = allocator.get_stats();

        unsafe {
            *out_total = stats.total_blocks;
            *out_free = stats.free_blocks;
            *out_allocated = stats.allocated_blocks;
        }

        0
    })
}

#[no_mangle]
pub extern "C" fn rust_extent_sync(alloc_ptr: *mut c_void) -> i32 {
    if alloc_ptr.is_null() {
        return -libc::EINVAL;
    }

    catch_panic(|| {
        let allocator = unsafe { &*(alloc_ptr as *const ExtentAllocator) };

        match allocator.sync_bitmap_to_disk() {
            Ok(()) => 0,
            Err(e) => {
                eprintln!("[FFI] rust_extent_sync failed: {:?}", e);
                -libc::EIO
            }
        }
    })
}

#[no_mangle]
pub extern "C" fn rust_extent_alloc_destroy(alloc_ptr: *mut c_void) {
    eprintln!("[FFI] rust_extent_alloc_destroy called with ptr={:p}", alloc_ptr);
    if !alloc_ptr.is_null() {
        catch_panic(|| unsafe {
            eprintln!("[FFI] About to drop ExtentAllocator at {:p}", alloc_ptr);
            let alloc = Box::from_raw(alloc_ptr as *mut ExtentAllocator);
            drop(alloc);
            eprintln!("[FFI] ExtentAllocator dropped successfully at {:p}", alloc_ptr);
        });
    } else {
        eprintln!("[FFI] rust_extent_alloc_destroy called with null pointer");
    }
}
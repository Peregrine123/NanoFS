// rust_core/src/extent/mod.rs
// Extent Allocator 实现

mod types;

pub use types::{AllocStats, Extent};

use anyhow::{bail, Result};
use bitvec::prelude::*;
use std::fs::File;
use std::io::{Read, Seek, SeekFrom, Write};
use std::mem::ManuallyDrop;
use std::os::unix::io::{FromRawFd, RawFd};
use std::sync::{
    atomic::{AtomicU32, AtomicU64, Ordering},
    Arc, Mutex, RwLock,
};

const BLOCK_SIZE: usize = 4096;

/// Extent Allocator: 管理连续块区域的分配器
pub struct ExtentAllocator {
    /// 设备文件句柄
    /// 使用 ManuallyDrop 防止 File 在 drop 时自动关闭 fd
    /// (fd 的生命周期由 C 侧管理)
    device: Arc<Mutex<ManuallyDrop<File>>>,

    /// 位图起始块号
    bitmap_start: u32,

    /// 总块数
    total_blocks: u32,

    /// 位图 (使用 bitvec，true=已分配, false=空闲)
    bitmap: RwLock<BitVec>,

    /// 统计信息
    stats: Arc<Mutex<AllocStats>>,

    /// 原子计数器：空闲块数 (用于快速查询)
    free_blocks: AtomicU32,

    /// 分配计数器
    alloc_count: AtomicU64,
}

impl ExtentAllocator {
    /// 创建新的 ExtentAllocator
    ///
    /// # 参数
    /// - `device_fd`: 设备文件描述符
    /// - `bitmap_start`: 位图在磁盘上的起始块号
    /// - `total_blocks`: 管理的总块数
    pub fn new(device_fd: RawFd, bitmap_start: u32, total_blocks: u32) -> Result<Self> {
        eprintln!(
            "[ExtentAllocator] Initializing: bitmap_start={}, total_blocks={}",
            bitmap_start, total_blocks
        );

        let device = unsafe { File::from_raw_fd(device_fd) };
        // 使用 ManuallyDrop 包装,防止 drop 时关闭 fd
        let device = ManuallyDrop::new(device);

        // 创建初始位图（全部空闲）
        let bitmap = bitvec![0; total_blocks as usize];

        let stats = AllocStats::new(total_blocks);

        let allocator = Self {
            device: Arc::new(Mutex::new(device)),
            bitmap_start,
            total_blocks,
            bitmap: RwLock::new(bitmap),
            stats: Arc::new(Mutex::new(stats)),
            free_blocks: AtomicU32::new(total_blocks),
            alloc_count: AtomicU64::new(0),
        };

        // 尝试从磁盘加载位图
        if let Err(e) = allocator.load_bitmap_from_disk() {
            eprintln!("[ExtentAllocator] Failed to load bitmap from disk: {}", e);
            eprintln!("[ExtentAllocator] Starting with empty bitmap");
        }

        eprintln!("[ExtentAllocator] Initialized successfully");
        Ok(allocator)
    }

    /// 分配 Extent
    ///
    /// # 参数
    /// - `hint`: 分配提示位置（从这里开始搜索）
    /// - `min_len`: 最小长度
    /// - `max_len`: 最大长度
    ///
    /// # 返回
    /// 成功返回 Extent，失败返回错误
    pub fn allocate_extent(&self, hint: u32, min_len: u32, max_len: u32) -> Result<Extent> {
        if min_len == 0 || max_len == 0 || min_len > max_len {
            bail!("Invalid extent length: min={}, max={}", min_len, max_len);
        }

        let mut bitmap = self.bitmap.write().unwrap();

        // First-Fit 搜索
        let (start, length) = self.find_consecutive_free(&bitmap, hint, min_len, max_len)?;

        // 标记为已分配
        for i in start..(start + length) {
            bitmap.set(i as usize, true);
        }

        // 更新统计
        self.free_blocks.fetch_sub(length, Ordering::Relaxed);
        self.alloc_count.fetch_add(1, Ordering::Relaxed);

        let mut stats = self.stats.lock().unwrap();
        stats.free_blocks -= length;
        stats.allocated_blocks += length;
        stats.alloc_count += 1;

        let extent = Extent::new(start, length);
        eprintln!("[ExtentAllocator] Allocated: {}", extent);

        Ok(extent)
    }

    /// 释放 Extent
    pub fn free_extent(&self, extent: &Extent) -> Result<()> {
        if extent.start + extent.length > self.total_blocks {
            bail!("Extent out of range: {}", extent);
        }

        let mut bitmap = self.bitmap.write().unwrap();

        // Double-free 检测
        for i in extent.start..(extent.start + extent.length) {
            if !bitmap[i as usize] {
                bail!("Double free detected at block {}", i);
            }
            bitmap.set(i as usize, false);
        }

        // 更新统计
        self.free_blocks.fetch_add(extent.length, Ordering::Relaxed);

        let mut stats = self.stats.lock().unwrap();
        stats.free_blocks += extent.length;
        stats.allocated_blocks -= extent.length;
        stats.free_count += 1;

        eprintln!("[ExtentAllocator] Freed: {}", extent);

        Ok(())
    }

    /// 计算碎片率
    ///
    /// # 算法
    /// 碎片率 = (实际碎片数 - 理想碎片数) / 总块数
    /// - 实际碎片数 = 连续空闲区域的数量
    /// - 理想碎片数 = 如果所有空闲块连续则为1，否则为0
    pub fn fragmentation_ratio(&self) -> f32 {
        let bitmap = self.bitmap.read().unwrap();
        let free_blocks = self.free_blocks.load(Ordering::Relaxed);

        if free_blocks == 0 {
            return 0.0; // 没有空闲块，无碎片
        }

        // 统计连续空闲区域数量
        let mut fragments = 0;
        let mut in_free_region = false;

        for bit in bitmap.iter() {
            if !*bit {
                // 空闲块
                if !in_free_region {
                    fragments += 1;
                    in_free_region = true;
                }
            } else {
                // 已分配块
                in_free_region = false;
            }
        }

        // 理想情况：所有空闲块连续，只有1个碎片
        let ideal_fragments = 1.0;

        // 碎片率：额外的碎片占总块数的比例
        if fragments <= 1 {
            return 0.0;
        }

        ((fragments as f32 - ideal_fragments) / self.total_blocks as f32).min(1.0)
    }

    /// 获取统计信息
    pub fn get_stats(&self) -> AllocStats {
        *self.stats.lock().unwrap()
    }

    /// 从磁盘加载位图
    fn load_bitmap_from_disk(&self) -> Result<()> {
        let bitmap_bytes = (self.total_blocks as usize + 7) / 8; // 向上取整
        let bitmap_blocks = (bitmap_bytes + BLOCK_SIZE - 1) / BLOCK_SIZE;

        eprintln!(
            "[ExtentAllocator] Loading bitmap from disk: {} bytes, {} blocks",
            bitmap_bytes, bitmap_blocks
        );

        let mut buffer = vec![0u8; bitmap_blocks * BLOCK_SIZE];
        let offset = (self.bitmap_start as u64) * BLOCK_SIZE as u64;

        let mut device = self.device.lock().unwrap();
        device.seek(SeekFrom::Start(offset))?;
        device.read_exact(&mut buffer)?;

        // 转换为 BitVec (显式指定类型)
        let loaded_bitmap: BitVec<u8, Lsb0> = BitVec::from_vec(buffer);
        let mut bitmap = self.bitmap.write().unwrap();

        // 只拷贝有效的位
        for i in 0..(self.total_blocks as usize) {
            bitmap.set(i, loaded_bitmap[i]);
        }

        // 重新计算空闲块数
        let free_count = bitmap.iter().filter(|b| !**b).count() as u32;
        self.free_blocks.store(free_count, Ordering::Relaxed);

        let mut stats = self.stats.lock().unwrap();
        stats.free_blocks = free_count;
        stats.allocated_blocks = self.total_blocks - free_count;

        eprintln!(
            "[ExtentAllocator] Bitmap loaded: free={}, allocated={}",
            free_count,
            self.total_blocks - free_count
        );

        Ok(())
    }

    /// 同步位图到磁盘
    pub fn sync_bitmap_to_disk(&self) -> Result<()> {
        let bitmap = self.bitmap.read().unwrap();

        let bitmap_bytes = (self.total_blocks as usize + 7) / 8;
        let bitmap_blocks = (bitmap_bytes + BLOCK_SIZE - 1) / BLOCK_SIZE;

        let mut buffer = vec![0u8; bitmap_blocks * BLOCK_SIZE];

        // 手动转换 BitVec 到字节数组
        for (i, bit) in bitmap.iter().enumerate() {
            if *bit {
                let byte_idx = i / 8;
                let bit_idx = i % 8;
                buffer[byte_idx] |= 1 << bit_idx;
            }
        }

        let offset = (self.bitmap_start as u64) * BLOCK_SIZE as u64;

        let mut device = self.device.lock().unwrap();
        device.seek(SeekFrom::Start(offset))?;
        device.write_all(&buffer)?;
        device.sync_all()?;

        eprintln!("[ExtentAllocator] Bitmap synced to disk");

        Ok(())
    }

    /// First-Fit 搜索算法
    ///
    /// 从 hint 位置开始循环搜索，找到第一个满足 [min_len, max_len] 的连续空闲区域
    fn find_consecutive_free(
        &self,
        bitmap: &BitVec,
        hint: u32,
        min_len: u32,
        max_len: u32,
    ) -> Result<(u32, u32)> {
        let total = bitmap.len() as u32;
        let start_pos = hint % total;

        let mut consecutive = 0;
        let mut region_start = 0;

        // 从 hint 开始循环搜索
        for offset in 0..total {
            let pos = (start_pos + offset) % total;

            if !bitmap[pos as usize] {
                // 空闲块
                if consecutive == 0 {
                    region_start = pos;
                }
                consecutive += 1;

                // 找到足够大的区域
                if consecutive >= min_len {
                    let allocated_len = consecutive.min(max_len);
                    return Ok((region_start, allocated_len));
                }
            } else {
                // 已分配块，重置计数
                consecutive = 0;
            }
        }

        bail!(
            "No free extent found: requested {} blocks, free_blocks={}",
            min_len,
            self.free_blocks.load(Ordering::Relaxed)
        )
    }
}

impl Drop for ExtentAllocator {
    fn drop(&mut self) {
        eprintln!("[ExtentAllocator] Dropping...");
        // ManuallyDrop 会防止 File 自动 drop,所以 fd 不会被关闭
        // fd 的生命周期由 C 侧管理
        eprintln!("[ExtentAllocator] Dropped (fd not closed)");
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::os::unix::io::AsRawFd;
    use tempfile::tempfile;

    #[test]
    fn test_extent_allocator_basic() {
        let file = tempfile().unwrap();
        let fd = file.as_raw_fd();

        let allocator = ExtentAllocator::new(fd, 0, 1000).unwrap();

        // 分配
        let extent = allocator.allocate_extent(0, 10, 20).unwrap();
        assert!(extent.length >= 10 && extent.length <= 20);

        // 释放
        allocator.free_extent(&extent).unwrap();

        // 统计
        let stats = allocator.get_stats();
        assert_eq!(stats.free_blocks, 1000);
    }

    #[test]
    fn test_double_free_detection() {
        let file = tempfile().unwrap();
        let fd = file.as_raw_fd();

        let allocator = ExtentAllocator::new(fd, 0, 1000).unwrap();

        let extent = allocator.allocate_extent(0, 10, 10).unwrap();
        allocator.free_extent(&extent).unwrap();

        // Double free 应该失败
        let result = allocator.free_extent(&extent);
        assert!(result.is_err());
    }

    #[test]
    fn test_fragmentation() {
        let file = tempfile().unwrap();
        let fd = file.as_raw_fd();

        let allocator = ExtentAllocator::new(fd, 0, 100).unwrap();

        // 初始无碎片
        assert_eq!(allocator.fragmentation_ratio(), 0.0);

        // 分配多个小块造成碎片
        let e1 = allocator.allocate_extent(0, 5, 5).unwrap();
        let _e2 = allocator.allocate_extent(10, 5, 5).unwrap();
        let _e3 = allocator.allocate_extent(20, 5, 5).unwrap();

        // 释放第一个，造成碎片
        allocator.free_extent(&e1).unwrap();

        let frag = allocator.fragmentation_ratio();
        assert!(frag > 0.0);
        println!("Fragmentation: {:.2}%", frag * 100.0);
    }
}

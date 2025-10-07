// rust_core/src/extent/types.rs
// Extent Allocator 数据结构定义

use std::fmt;

/// Extent: 连续块区域的描述符
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Extent {
    /// 起始块号
    pub start: u32,
    /// 块数量
    pub length: u32,
}

impl Extent {
    /// 创建新的 Extent
    pub fn new(start: u32, length: u32) -> Self {
        Self { start, length }
    }

    /// 获取结束块号（不包含）
    pub fn end(&self) -> u32 {
        self.start + self.length
    }

    /// 检查是否包含指定块号
    pub fn contains(&self, block_num: u32) -> bool {
        block_num >= self.start && block_num < self.end()
    }

    /// 检查是否与另一个 Extent 重叠
    pub fn overlaps(&self, other: &Extent) -> bool {
        self.start < other.end() && other.start < self.end()
    }
}

impl fmt::Display for Extent {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "Extent[{}, +{}]", self.start, self.length)
    }
}

/// 分配统计信息
#[derive(Debug, Clone, Copy, Default)]
pub struct AllocStats {
    /// 总块数
    pub total_blocks: u32,
    /// 空闲块数
    pub free_blocks: u32,
    /// 已分配块数
    pub allocated_blocks: u32,
    /// 分配次数
    pub alloc_count: u64,
    /// 释放次数
    pub free_count: u64,
}

impl AllocStats {
    pub fn new(total_blocks: u32) -> Self {
        Self {
            total_blocks,
            free_blocks: total_blocks,
            allocated_blocks: 0,
            alloc_count: 0,
            free_count: 0,
        }
    }

    /// 使用率 (0.0 - 1.0)
    pub fn utilization(&self) -> f32 {
        if self.total_blocks == 0 {
            return 0.0;
        }
        self.allocated_blocks as f32 / self.total_blocks as f32
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_extent_basic() {
        let e = Extent::new(100, 50);
        assert_eq!(e.start, 100);
        assert_eq!(e.length, 50);
        assert_eq!(e.end(), 150);
    }

    #[test]
    fn test_extent_contains() {
        let e = Extent::new(100, 50);
        assert!(e.contains(100));
        assert!(e.contains(149));
        assert!(!e.contains(150));
        assert!(!e.contains(99));
    }

    #[test]
    fn test_extent_overlaps() {
        let e1 = Extent::new(100, 50);
        let e2 = Extent::new(120, 30);
        let e3 = Extent::new(200, 50);

        assert!(e1.overlaps(&e2));
        assert!(e2.overlaps(&e1));
        assert!(!e1.overlaps(&e3));
    }

    #[test]
    fn test_alloc_stats() {
        let mut stats = AllocStats::new(1000);
        assert_eq!(stats.free_blocks, 1000);
        assert_eq!(stats.utilization(), 0.0);

        stats.allocated_blocks = 300;
        stats.free_blocks = 700;
        assert_eq!(stats.utilization(), 0.3);
    }
}

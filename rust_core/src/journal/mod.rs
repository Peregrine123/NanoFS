// Journal Manager - WAL (Write-Ahead Log) Implementation
//
// 设计原理:
// 1. 所有数据修改前先写入Journal (Write-Ahead)
// 2. Transaction提交时写入commit记录并fsync
// 3. Checkpoint时将Journal数据写入最终位置
// 4. 崩溃恢复时重放已提交的事务

use anyhow::{bail, Context as AnyhowContext, Result};
use std::collections::HashMap;
use std::fs::File;
use std::io::{Read, Seek, SeekFrom, Write};
use std::mem::ManuallyDrop;
use std::os::unix::io::{FromRawFd, RawFd};
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{Arc, Mutex, RwLock};

pub mod types;
use types::*;

const BLOCK_SIZE: usize = 4096;
const JOURNAL_MAGIC: u32 = 0x4A524E4C; // "JRNL"
#[allow(dead_code)]
const JOURNAL_VERSION: u32 = 1;

// ============ Journal Manager主结构 ============

pub struct JournalManager {
    /// 设备文件（共享引用，支持并发）
    /// 使用 ManuallyDrop 防止 File 在 drop 时自动关闭 fd
    /// (fd 的生命周期由 C 侧管理)
    device: Arc<Mutex<ManuallyDrop<File>>>,
    /// Journal在磁盘上的起始块号
    journal_start: u32,
    /// Journal总块数
    journal_blocks: u32,
    /// Journal超级块（包含head/tail指针）
    superblock: Mutex<JournalSuperblock>,
    /// 活跃事务表
    active_txns: RwLock<HashMap<u64, Arc<Mutex<Transaction>>>>,
    /// 下一个事务ID
    next_tid: AtomicU64,
}

impl JournalManager {
    pub fn new(device_fd: RawFd, start: u32, blocks: u32) -> Result<Self> {
        if blocks < 2 {
            bail!("Journal too small (need at least 2 blocks)");
        }

        let device = unsafe { File::from_raw_fd(device_fd) };
        // 使用 ManuallyDrop 包装,防止 drop 时关闭 fd
        let device = ManuallyDrop::new(device);
        let mut sb_buf = vec![0u8; BLOCK_SIZE];
        let offset = (start as u64) * (BLOCK_SIZE as u64);

        let mut device_clone = device.try_clone().context("Failed to clone device file")?;

        device_clone.seek(SeekFrom::Start(offset))?;
        device_clone.read_exact(&mut sb_buf)?;

        let superblock: JournalSuperblock = unsafe { std::ptr::read(sb_buf.as_ptr() as *const _) };

        let magic = superblock.magic;
        let sequence = superblock.sequence;
        let head = superblock.head;
        let tail = superblock.tail;

        if magic != JOURNAL_MAGIC {
            bail!(
                "Invalid journal magic: expected 0x{:X}, got 0x{:X}",
                JOURNAL_MAGIC,
                magic
            );
        }

        // 兼容性检查: 如果head=0且tail=0,自动升级为head=1, tail=1
        // (旧版本从0开始分配,但块0应该是superblock)
        let (head, tail) = if head == 0 && tail == 0 {
            eprintln!(
                "[Journal] Detected old format (head=0, tail=0), upgrading to (head=1, tail=1)"
            );
            (1, 1)
        } else {
            (head, tail)
        };

        eprintln!(
            "[Journal] Loaded: magic=0x{:X}, seq={}, head={}, tail={}",
            magic, sequence, head, tail
        );

        // 更新superblock中的head和tail
        let mut superblock = superblock;
        superblock.head = head;
        superblock.tail = tail;

        Ok(Self {
            device: Arc::new(Mutex::new(device)),
            journal_start: start,
            journal_blocks: blocks,
            superblock: Mutex::new(superblock),
            active_txns: RwLock::new(HashMap::new()),
            next_tid: AtomicU64::new(1),
        })
    }

    pub fn begin_transaction(&self) -> Result<Arc<Mutex<Transaction>>> {
        let tid = self.next_tid.fetch_add(1, Ordering::SeqCst);
        let txn = Arc::new(Mutex::new(Transaction {
            id: tid,
            writes: Vec::new(),
            state: TxnState::Active,
        }));
        self.active_txns.write().unwrap().insert(tid, txn.clone());
        eprintln!("[Journal] Begin transaction {}", tid);
        Ok(txn)
    }

    pub fn commit(&self, txn: Arc<Mutex<Transaction>>) -> Result<()> {
        let mut txn_inner = txn.lock().unwrap();
        if txn_inner.state != TxnState::Active {
            bail!("Cannot commit transaction in state {:?}", txn_inner.state);
        }

        eprintln!(
            "[Journal] Committing transaction {} ({} writes)",
            txn_inner.id,
            txn_inner.writes.len()
        );

        // 如果没有任何写入,直接标记为已提交并返回
        if txn_inner.writes.is_empty() {
            eprintln!(
                "[Journal] Transaction {} has no writes, skipping commit",
                txn_inner.id
            );
            txn_inner.state = TxnState::Committed;
            self.active_txns.write().unwrap().remove(&txn_inner.id);
            return Ok(());
        }

        let mut journal_blocks_used = Vec::new();
        for (block_num, data) in &txn_inner.writes {
            let journal_block = self.allocate_journal_block()?;
            self.write_journal_data(journal_block, *block_num, data)?;
            journal_blocks_used.push(journal_block);
        }

        let commit_block = self.allocate_journal_block()?;
        self.write_commit_record(commit_block, txn_inner.id, &journal_blocks_used)?;

        // 同步superblock到磁盘，确保tail指针被持久化
        self.sync_superblock_to_disk()?;

        self.device
            .lock()
            .unwrap()
            .sync_all()
            .context("Failed to fsync journal")?;

        txn_inner.state = TxnState::Committed;
        self.active_txns.write().unwrap().remove(&txn_inner.id);

        eprintln!("[Journal] Transaction {} committed", txn_inner.id);
        Ok(())
    }

    pub fn abort(&self, txn: Arc<Mutex<Transaction>>) -> Result<()> {
        let mut txn_inner = txn.lock().unwrap();
        if txn_inner.state != TxnState::Active {
            bail!("Cannot abort transaction in state {:?}", txn_inner.state);
        }
        eprintln!("[Journal] Aborting transaction {}", txn_inner.id);
        txn_inner.state = TxnState::Aborted;
        self.active_txns.write().unwrap().remove(&txn_inner.id);
        Ok(())
    }

    fn allocate_journal_block(&self) -> Result<u32> {
        let mut sb = self.superblock.lock().unwrap();
        let next_tail = (sb.tail + 1) % self.journal_blocks;
        if next_tail == sb.head {
            bail!("Journal is full");
        }
        let allocated = sb.tail;
        sb.tail = next_tail;
        Ok(allocated)
    }

    fn write_journal_data(&self, journal_block: u32, target_block: u32, data: &[u8]) -> Result<()> {
        if data.len() != BLOCK_SIZE {
            bail!("Invalid data size");
        }

        // 分配2个Journal块: 一个存header, 一个存完整数据
        let data_journal_block = self.allocate_journal_block()?;

        let header = JournalDataHeader {
            magic: JOURNAL_DATA_MAGIC,
            target_block,
            checksum: Self::calculate_checksum(data),
        };

        // 先写入header块
        let mut header_block = vec![0u8; BLOCK_SIZE];
        unsafe {
            std::ptr::copy_nonoverlapping(
                &header as *const _ as *const u8,
                header_block.as_mut_ptr(),
                std::mem::size_of::<JournalDataHeader>(),
            );
        }
        let offset = ((self.journal_start + journal_block) as u64) * (BLOCK_SIZE as u64);
        let mut device = self.device.lock().unwrap();
        device.seek(SeekFrom::Start(offset))?;
        device.write_all(&header_block)?;

        // 再写入完整数据块
        let data_offset = ((self.journal_start + data_journal_block) as u64) * (BLOCK_SIZE as u64);
        device.seek(SeekFrom::Start(data_offset))?;
        device.write_all(data)?;

        Ok(())
    }

    fn write_commit_record(&self, journal_block: u32, txn_id: u64, blocks: &[u32]) -> Result<()> {
        let header = JournalCommitRecord {
            magic: JOURNAL_COMMIT_MAGIC,
            txn_id,
            num_blocks: blocks.len() as u32,
            checksum: Self::calculate_commit_checksum(txn_id, blocks),
        };
        let mut block = vec![0u8; BLOCK_SIZE];
        unsafe {
            std::ptr::copy_nonoverlapping(
                &header as *const _ as *const u8,
                block.as_mut_ptr(),
                std::mem::size_of::<JournalCommitRecord>(),
            );
        }
        let offset = ((self.journal_start + journal_block) as u64) * (BLOCK_SIZE as u64);
        let mut device = self.device.lock().unwrap();
        device.seek(SeekFrom::Start(offset))?;
        device.write_all(&block)?;
        Ok(())
    }

    fn calculate_checksum(data: &[u8]) -> u32 {
        data.chunks(4)
            .map(|chunk| {
                let mut val = 0u32;
                for (i, &byte) in chunk.iter().enumerate() {
                    val |= (byte as u32) << (i * 8);
                }
                val
            })
            .fold(0u32, |acc, val| acc ^ val)
    }

    fn calculate_commit_checksum(txn_id: u64, _blocks: &[u32]) -> u32 {
        (txn_id & 0xFFFFFFFF) as u32
    }

    fn sync_superblock_to_disk(&self) -> Result<()> {
        let sb = self.superblock.lock().unwrap();
        let mut sb_buf = vec![0u8; BLOCK_SIZE];
        unsafe {
            std::ptr::copy_nonoverlapping(
                &*sb as *const _ as *const u8,
                sb_buf.as_mut_ptr(),
                std::mem::size_of::<JournalSuperblock>(),
            );
        }
        drop(sb); // 释放锁以便进行I/O

        let offset = (self.journal_start as u64) * (BLOCK_SIZE as u64);
        let mut device = self.device.lock().unwrap();
        device.seek(SeekFrom::Start(offset))?;
        device.write_all(&sb_buf)?;
        device.sync_all()?;
        eprintln!("[Journal] Superblock synced to disk");
        Ok(())
    }

    pub fn checkpoint(&self) -> Result<()> {
        let sb = self.superblock.lock().unwrap();
        let head = sb.head;
        let tail = sb.tail;
        eprintln!(
            "[Journal] Starting checkpoint (head={}, tail={})",
            head, tail
        );

        if head == tail {
            eprintln!("[Journal] Checkpoint complete: 0 blocks applied");
            return Ok(());
        }

        let mut current = head;
        let mut blocks_applied = 0;

        // 先释放锁,避免在I/O操作时持有锁
        drop(sb);

        while current != tail {
            let (magic, target_block, data) = self.read_journal_block(current)?;

            if magic == JOURNAL_DATA_MAGIC {
                // 写入数据到最终位置
                let offset = (target_block as u64) * (BLOCK_SIZE as u64);
                eprintln!(
                    "[Journal] Applying data block: journal_block={}, target_block={}, offset={}",
                    current, target_block, offset
                );
                let mut device = self.device.lock().unwrap();
                device.seek(SeekFrom::Start(offset))?;
                device.write_all(&data)?;
                eprintln!(
                    "[Journal] Successfully wrote {} bytes to target block {}",
                    data.len(),
                    target_block
                );
                drop(device); // 释放锁
                blocks_applied += 1;

                // 使C侧的缓存失效
                extern "C" {
                    fn c_buffer_cache_invalidate_by_fd(fd: i32, block: u32);
                }
                unsafe {
                    c_buffer_cache_invalidate_by_fd(0, target_block); // fd暂不使用
                }

                // 数据块占用2个Journal块 (header + data), 跳过数据块
                current = (current + 2) % self.journal_blocks;
            } else if magic == JOURNAL_COMMIT_MAGIC {
                eprintln!("[Journal] Found commit record at block {}", current);
                current = (current + 1) % self.journal_blocks;
            } else {
                // 未知块，跳过
                current = (current + 1) % self.journal_blocks;
            }
        }

        // 同步到磁盘
        self.device.lock().unwrap().sync_all()?;

        // 更新 head 指针到 tail,释放 Journal 空间
        let mut sb = self.superblock.lock().unwrap();
        sb.head = tail;
        sb.sequence += 1;

        // 将更新后的 superblock 写回磁盘
        let mut sb_buf = vec![0u8; BLOCK_SIZE];
        unsafe {
            std::ptr::copy_nonoverlapping(
                &*sb as *const _ as *const u8,
                sb_buf.as_mut_ptr(),
                std::mem::size_of::<JournalSuperblock>(),
            );
        }

        let offset = (self.journal_start as u64) * (BLOCK_SIZE as u64);
        let mut device = self.device.lock().unwrap();
        device.seek(SeekFrom::Start(offset))?;
        device.write_all(&sb_buf)?;
        device.sync_all()?;

        eprintln!(
            "[Journal] Checkpoint complete: {} blocks applied, head updated to {}",
            blocks_applied, tail
        );
        Ok(())
    }

    pub fn recover(&self) -> Result<usize> {
        eprintln!("[Journal] ========== Starting crash recovery ==========");

        let sb = self.superblock.lock().unwrap();
        let head = sb.head;
        let tail = sb.tail;
        eprintln!("[Journal] Journal state: head={}, tail={}", head, tail);

        if head == tail {
            eprintln!("[Journal] Journal is empty, no recovery needed");
            return Ok(0);
        }

        let mut recovered_txns = 0;
        let mut current = head;
        let mut in_transaction = false;
        let mut current_txn_blocks = Vec::new();

        while current != tail {
            let (magic, target_block, data) = self.read_journal_block(current)?;

            match magic {
                JOURNAL_DATA_MAGIC => {
                    if !in_transaction {
                        eprintln!(
                            "[Journal] Found data block at {}, starting new transaction",
                            current
                        );
                        in_transaction = true;
                    }
                    current_txn_blocks.push((target_block, data));
                    // 数据块占用2个Journal块 (header + data)
                    current = (current + 2) % self.journal_blocks;
                }
                JOURNAL_COMMIT_MAGIC => {
                    if in_transaction && !current_txn_blocks.is_empty() {
                        eprintln!(
                            "[Journal] Found commit record, replaying {} blocks",
                            current_txn_blocks.len()
                        );

                        // 重放所有块
                        for (block_num, block_data) in &current_txn_blocks {
                            let offset = (*block_num as u64) * (BLOCK_SIZE as u64);
                            let mut device = self.device.lock().unwrap();
                            device.seek(SeekFrom::Start(offset))?;
                            device.write_all(block_data)?;
                        }

                        recovered_txns += 1;
                        current_txn_blocks.clear();
                        in_transaction = false;
                    }
                    current = (current + 1) % self.journal_blocks;
                }
                _ => {
                    eprintln!(
                        "[Journal] Unknown magic 0x{:X} at block {}, stopping recovery",
                        magic, current
                    );
                    break;
                }
            }
        }

        // 如果有未提交的块，丢弃它们
        if in_transaction && !current_txn_blocks.is_empty() {
            eprintln!(
                "[Journal] Discarding {} uncommitted blocks",
                current_txn_blocks.len()
            );
        }

        // 同步到磁盘
        self.device.lock().unwrap().sync_all()?;

        eprintln!(
            "[Journal] ========== Recovery complete: {} transactions recovered ==========",
            recovered_txns
        );
        Ok(recovered_txns)
    }

    fn read_journal_block(&self, journal_block: u32) -> Result<(u32, u32, Vec<u8>)> {
        let mut block = vec![0u8; BLOCK_SIZE];
        let offset = ((self.journal_start + journal_block) as u64) * (BLOCK_SIZE as u64);

        let mut device = self.device.lock().unwrap();
        device.seek(SeekFrom::Start(offset))?;
        device.read_exact(&mut block)?;

        let magic = u32::from_le_bytes([block[0], block[1], block[2], block[3]]);

        if magic == JOURNAL_DATA_MAGIC {
            // 读取header
            let header: JournalDataHeader = unsafe { std::ptr::read(block.as_ptr() as *const _) };

            // 读取下一个块的完整数据
            let mut data = vec![0u8; BLOCK_SIZE];
            let data_offset = offset + (BLOCK_SIZE as u64);
            device.seek(SeekFrom::Start(data_offset))?;
            device.read_exact(&mut data)?;

            drop(device);
            Ok((magic, header.target_block, data))
        } else if magic == JOURNAL_COMMIT_MAGIC {
            drop(device);
            Ok((magic, 0, vec![]))
        } else {
            drop(device);
            Ok((magic, 0, vec![]))
        }
    }
}

impl Drop for JournalManager {
    fn drop(&mut self) {
        eprintln!("[Journal] JournalManager dropping...");
        // ManuallyDrop 会防止 File 自动 drop,所以 fd 不会被关闭
        // fd 的生命周期由 C 侧管理
        eprintln!("[Journal] JournalManager dropped (fd not closed)");
    }
}

// Journal数据结构定义

// ============ 常量 ============

pub const JOURNAL_DATA_MAGIC: u32 = 0x4A444154; // "JDAT"
pub const JOURNAL_COMMIT_MAGIC: u32 = 0x4A434D54; // "JCMT"

// ============ Journal超级块 ============

#[repr(C, packed)]
#[derive(Debug, Clone, Copy)]
pub struct JournalSuperblock {
    /// Magic number (0x4A524E4C = "JRNL")
    pub magic: u32,
    /// Journal版本
    pub version: u32,
    /// 块大小
    pub block_size: u32,
    /// Journal总块数
    pub total_blocks: u32,
    /// 序列号（每次挂载递增）
    pub sequence: u64,
    /// Head指针（最老的未应用事务）
    pub head: u32,
    /// Tail指针（下一个可分配位置）
    pub tail: u32,
    /// 保留字段
    pub reserved: [u8; 4040],
}

// ============ Journal数据块头部 ============

#[repr(C, packed)]
#[derive(Debug, Clone, Copy)]
pub struct JournalDataHeader {
    /// Magic number (0x4A444154 = "JDAT")
    pub magic: u32,
    /// 目标块号（最终写入位置）
    pub target_block: u32,
    /// 数据校验和
    pub checksum: u32,
}

// ============ Journal提交记录 ============

#[repr(C, packed)]
#[derive(Debug, Clone, Copy)]
pub struct JournalCommitRecord {
    /// Magic number (0x4A434D54 = "JCMT")
    pub magic: u32,
    /// 事务ID
    pub txn_id: u64,
    /// 数据块数量
    pub num_blocks: u32,
    /// 提交记录校验和
    pub checksum: u32,
}

// ============ 事务状态 ============

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum TxnState {
    /// 活跃（正在写入）
    Active,
    /// 已提交
    Committed,
    /// 已中止
    Aborted,
}

// ============ 事务结构 ============

pub struct Transaction {
    /// 事务ID
    pub id: u64,
    /// 写入列表 (block_num, data)
    pub writes: Vec<(u32, Vec<u8>)>,
    /// 事务状态
    pub state: TxnState,
}

impl Transaction {
    /// 添加一个块写入到事务
    pub fn write_block(&mut self, block_num: u32, data: Vec<u8>) -> anyhow::Result<()> {
        if self.state != TxnState::Active {
            anyhow::bail!("Cannot write to transaction in state {:?}", self.state);
        }

        if data.len() != 4096 {
            anyhow::bail!("Invalid block size: expected 4096, got {}", data.len());
        }

        self.writes.push((block_num, data));
        Ok(())
    }
}

// RAII: 事务未提交时警告
impl Drop for Transaction {
    fn drop(&mut self) {
        if self.state == TxnState::Active {
            eprintln!("⚠️  Transaction {} dropped without commit or abort!", self.id);
        }
    }
}

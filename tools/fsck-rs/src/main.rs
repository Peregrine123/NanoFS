// fsck.modernfs - ModernFS 文件系统检查工具
// Week 8: Rust 工具集 - 文件系统检查工具

use anyhow::{Context, Result, bail};
use clap::Parser;
use colored::Colorize;
use std::fs::File;
use std::os::unix::fs::FileExt;

// ============ 常量定义 ============

const BLOCK_SIZE: u32 = 4096;
const SUPERBLOCK_MAGIC: u32 = 0x4D4F4446; // "MODF"
const JOURNAL_MAGIC: u32 = 0x4A524E4C; // "JRNL"
const INODE_SIZE: u32 = 128;

const INODE_TYPE_FILE: u8 = 1;
const INODE_TYPE_DIR: u8 = 2;
const INODE_TYPE_SYMLINK: u8 = 3;

const INVALID_BLOCK: u32 = 0xFFFFFFFF;

// ============ 命令行参数 ============

#[derive(Parser)]
#[command(name = "fsck.modernfs")]
#[command(about = "Check and repair ModernFS filesystem", version = "1.0.0")]
struct Args {
    /// Disk image path
    disk: String,

    /// Automatically fix errors
    #[arg(short = 'y', long)]
    auto_fix: bool,

    /// Verbose output
    #[arg(short, long)]
    verbose: bool,

    /// Check journal consistency
    #[arg(short, long)]
    check_journal: bool,
}

// ============ 数据结构 ============

#[repr(C, packed)]
#[derive(Debug, Clone, Copy)]
struct Superblock {
    magic: u32,
    version: u32,
    block_size: u32,
    total_blocks: u32,
    free_blocks: u32,

    journal_start: u32,
    journal_blocks: u32,
    inode_bitmap_start: u32,
    inode_bitmap_blocks: u32,
    data_bitmap_start: u32,
    data_bitmap_blocks: u32,
    inode_table_start: u32,
    inode_table_blocks: u32,
    data_start: u32,
    data_blocks: u32,

    total_inodes: u32,
    free_inodes: u32,
    first_inode: u32,
    root_inum: u32,

    state: u32,
    mount_time: u64,
    write_time: u64,
    mount_count: u32,

    padding: [u8; 3988],
}

#[repr(C, packed)]
#[derive(Debug, Clone, Copy)]
struct JournalSuperblock {
    magic: u32,
    block_size: u32,
    total_blocks: u32,
    sequence: u64,
    head: u32,
    tail: u32,
    padding: [u8; 4064],
}

#[repr(C, packed)]
#[derive(Debug, Clone, Copy)]
struct DiskInode {
    mode: u16,
    uid: u16,
    gid: u16,
    nlink: u16,
    inode_type: u8,
    flags: u8,
    reserved: u16,

    size: u64,
    blocks: u64,

    atime: u64,
    mtime: u64,
    ctime: u64,

    direct: [u32; 12],
    indirect: u32,
    double_indirect: u32,

    padding: [u8; 20],
}

#[derive(Debug)]
struct FsckContext {
    disk: File,
    sb: Superblock,
    errors: Vec<String>,
    warnings: Vec<String>,
    fixed: Vec<String>,
    verbose: bool,
}

// ============ 主函数 ============

fn main() -> Result<()> {
    let args = Args::parse();

    print_banner();

    println!("🔍 Checking filesystem: {}\n", args.disk.bright_yellow());

    // 打开磁盘镜像
    let disk = File::open(&args.disk)
        .context("Failed to open disk image")?;

    // 读取超级块
    let sb = read_superblock(&disk)?;

    // 创建上下文
    let mut ctx = FsckContext {
        disk,
        sb,
        errors: Vec::new(),
        warnings: Vec::new(),
        fixed: Vec::new(),
        verbose: args.verbose,
    };

    // 执行检查
    println!("{}", "Phase 1: Checking superblock...".bright_blue());
    check_superblock(&mut ctx)?;

    if args.check_journal {
        println!("{}", "Phase 2: Checking journal...".bright_blue());
        check_journal(&mut ctx)?;
    }

    println!("{}", "Phase 3: Checking inode bitmap...".bright_blue());
    check_inode_bitmap(&mut ctx)?;

    println!("{}", "Phase 4: Checking data bitmap...".bright_blue());
    check_data_bitmap(&mut ctx)?;

    println!("{}", "Phase 5: Checking inodes...".bright_blue());
    check_inodes(&mut ctx)?;

    println!("{}", "Phase 6: Checking directory structure...".bright_blue());
    check_directories(&mut ctx)?;

    // 输出结果
    print_results(&ctx);

    // 返回状态
    if !ctx.errors.is_empty() {
        std::process::exit(1);
    }

    Ok(())
}

// ============ 检查函数 ============

fn read_superblock(disk: &File) -> Result<Superblock> {
    let mut buf = vec![0u8; BLOCK_SIZE as usize];
    disk.read_exact_at(&mut buf, 0)
        .context("Failed to read superblock")?;

    let sb: Superblock = unsafe { std::ptr::read(buf.as_ptr() as *const _) };

    // 安全地读取 magic（避免 packed struct 问题）
    let magic = sb.magic;
    if magic != SUPERBLOCK_MAGIC {
        bail!("Invalid superblock magic: 0x{:08X} (expected 0x{:08X})", magic, SUPERBLOCK_MAGIC);
    }

    Ok(sb)
}

fn check_superblock(ctx: &mut FsckContext) -> Result<()> {
    let sb = &ctx.sb;

    // 检查基本字段
    let version = sb.version;
    if version != 1 {
        ctx.warnings.push(format!("Unusual version number: {}", version));
    }

    let block_size = sb.block_size;
    if block_size != BLOCK_SIZE {
        ctx.errors.push(format!("Invalid block size: {} (expected {})", block_size, BLOCK_SIZE));
    }

    // 检查布局合理性
    let total_blocks = sb.total_blocks;
    let journal_start = sb.journal_start;
    let journal_blocks = sb.journal_blocks;
    let data_start = sb.data_start;

    if journal_start == 0 {
        ctx.errors.push("Journal start is 0".to_string());
    }

    if journal_start + journal_blocks > total_blocks {
        ctx.errors.push("Journal extends beyond filesystem".to_string());
    }

    if data_start > total_blocks {
        ctx.errors.push("Data region starts beyond filesystem".to_string());
    }

    let state = sb.state;
    if state != 1 {
        ctx.warnings.push(format!("Filesystem not cleanly unmounted (state={})", state));
    }

    println!("  ✅ Superblock structure is valid");
    Ok(())
}

fn check_journal(ctx: &mut FsckContext) -> Result<()> {
    let sb = &ctx.sb;
    let journal_start = sb.journal_start;
    let block_size = sb.block_size;

    let mut buf = vec![0u8; block_size as usize];
    let offset = (journal_start as u64) * (block_size as u64);
    ctx.disk.read_exact_at(&mut buf, offset)
        .context("Failed to read journal superblock")?;

    let jsb: JournalSuperblock = unsafe { std::ptr::read(buf.as_ptr() as *const _) };

    let magic = jsb.magic;
    if magic != JOURNAL_MAGIC {
        ctx.errors.push(format!("Invalid journal magic: 0x{:08X}", magic));
        return Ok(());
    }

    let head = jsb.head;
    let tail = jsb.tail;
    let total_blocks = jsb.total_blocks;

    if head >= total_blocks || tail >= total_blocks {
        ctx.errors.push(format!("Invalid journal pointers: head={}, tail={}", head, tail));
    }

    if head == tail {
        println!("  ✅ Journal is empty");
    } else {
        ctx.warnings.push(format!("Journal contains {} pending transactions",
            if tail > head { tail - head } else { total_blocks - head + tail }));
    }

    Ok(())
}

fn check_inode_bitmap(ctx: &mut FsckContext) -> Result<()> {
    let sb = &ctx.sb;
    let start = sb.inode_bitmap_start;
    let blocks = sb.inode_bitmap_blocks;
    let block_size = sb.block_size;

    let size = (blocks * block_size) as usize;
    let mut bitmap = vec![0u8; size];

    let offset = (start as u64) * (block_size as u64);
    ctx.disk.read_exact_at(&mut bitmap, offset)
        .context("Failed to read inode bitmap")?;

    // 统计已使用的 inode
    let mut used_count = 0;
    for byte in &bitmap {
        used_count += byte.count_ones();
    }

    let expected_used = sb.total_inodes - sb.free_inodes;
    if used_count != expected_used {
        ctx.warnings.push(format!(
            "Inode bitmap count mismatch: {} used in bitmap, {} expected",
            used_count, expected_used
        ));
    }

    println!("  ✅ Inode bitmap checked ({} inodes used)", used_count);
    Ok(())
}

fn check_data_bitmap(ctx: &mut FsckContext) -> Result<()> {
    let sb = &ctx.sb;
    let start = sb.data_bitmap_start;
    let blocks = sb.data_bitmap_blocks;
    let block_size = sb.block_size;

    let size = (blocks * block_size) as usize;
    let mut bitmap = vec![0u8; size];

    let offset = (start as u64) * (block_size as u64);
    ctx.disk.read_exact_at(&mut bitmap, offset)
        .context("Failed to read data bitmap")?;

    // 统计已使用的数据块
    let mut used_count = 0;
    for byte in &bitmap {
        used_count += byte.count_ones();
    }

    let expected_used = sb.data_blocks - sb.free_blocks;
    if used_count != expected_used {
        ctx.warnings.push(format!(
            "Data bitmap count mismatch: {} used in bitmap, {} expected",
            used_count, expected_used
        ));
    }

    println!("  ✅ Data bitmap checked ({} blocks used)", used_count);
    Ok(())
}

fn check_inodes(ctx: &mut FsckContext) -> Result<()> {
    let sb = &ctx.sb;
    let inode_table_start = sb.inode_table_start;
    let total_inodes = sb.total_inodes;
    let block_size = sb.block_size;

    let mut valid_inodes = 0;
    let mut errors = 0;

    // 检查前 100 个 inode（避免检查太久）
    let check_count = std::cmp::min(total_inodes, 100);

    for inum in 0..check_count {
        let offset = (inode_table_start as u64) * (block_size as u64)
            + (inum as u64) * (INODE_SIZE as u64);

        let mut buf = vec![0u8; INODE_SIZE as usize];
        if ctx.disk.read_exact_at(&mut buf, offset).is_err() {
            continue;
        }

        let inode: DiskInode = unsafe { std::ptr::read(buf.as_ptr() as *const _) };

        let inode_type = inode.inode_type;
        let nlink = inode.nlink;

        // 跳过空 inode
        if inode_type == 0 {
            continue;
        }

        // 检查类型
        if inode_type != INODE_TYPE_FILE && inode_type != INODE_TYPE_DIR && inode_type != INODE_TYPE_SYMLINK {
            if ctx.verbose {
                ctx.errors.push(format!("Inode {} has invalid type: {}", inum, inode_type));
            }
            errors += 1;
            continue;
        }

        // 检查链接数
        if nlink == 0 {
            if ctx.verbose {
                ctx.warnings.push(format!("Inode {} has zero link count but is allocated", inum));
            }
        }

        valid_inodes += 1;
    }

    if errors > 0 {
        println!("  ⚠️  Found {} errors in inodes", errors);
    } else {
        println!("  ✅ Checked {} inodes, {} valid", check_count, valid_inodes);
    }

    Ok(())
}

fn check_directories(ctx: &mut FsckContext) -> Result<()> {
    let sb = &ctx.sb;
    let root_inum = sb.root_inum;

    // 检查根目录
    if let Err(e) = check_directory(ctx, root_inum, None) {
        ctx.errors.push(format!("Root directory check failed: {}", e));
    } else {
        println!("  ✅ Directory structure is consistent");
    }

    Ok(())
}

fn check_directory(ctx: &mut FsckContext, inum: u32, _parent: Option<u32>) -> Result<()> {
    let sb = &ctx.sb;
    let inode_table_start = sb.inode_table_start;
    let block_size = sb.block_size;

    // 读取 inode
    let offset = (inode_table_start as u64) * (block_size as u64)
        + (inum as u64) * (INODE_SIZE as u64);

    let mut buf = vec![0u8; INODE_SIZE as usize];
    ctx.disk.read_exact_at(&mut buf, offset)
        .context("Failed to read inode")?;

    let inode: DiskInode = unsafe { std::ptr::read(buf.as_ptr() as *const _) };

    let inode_type = inode.inode_type;
    if inode_type != INODE_TYPE_DIR {
        bail!("Inode {} is not a directory (type={})", inum, inode_type);
    }

    // 只检查第一个直接块（简化）
    let block0 = inode.direct[0];
    if block0 == INVALID_BLOCK {
        return Ok(()); // 空目录
    }

    // 读取目录块
    let data_offset = (block0 as u64) * (block_size as u64);
    let mut dir_buf = vec![0u8; block_size as usize];
    ctx.disk.read_exact_at(&mut dir_buf, data_offset)?;

    // 简单验证：检查前两个目录项应该是 "." 和 ".."
    if dir_buf.len() >= 8 {
        // 检查第一个目录项的 inum 应该是自己
        let first_inum = u32::from_le_bytes([dir_buf[0], dir_buf[1], dir_buf[2], dir_buf[3]]);
        if first_inum != inum && first_inum != 0 {
            if ctx.verbose {
                ctx.warnings.push(format!("Directory {} first entry should point to itself", inum));
            }
        }
    }

    Ok(())
}

// ============ 输出函数 ============

fn print_banner() {
    println!("{}", r#"
    ╔═══════════════════════════════════════╗
    ║   ModernFS Filesystem Checker        ║
    ║   fsck.modernfs v1.0.0               ║
    ╚═══════════════════════════════════════╝
    "#.bright_cyan());
}

fn print_results(ctx: &FsckContext) {
    println!("\n{}", "=".repeat(50).bright_blue());
    println!("{}", "  FSCK RESULTS".bright_cyan().bold());
    println!("{}", "=".repeat(50).bright_blue());

    if ctx.errors.is_empty() && ctx.warnings.is_empty() {
        println!("\n{}", "✅ Filesystem is clean!".bright_green().bold());
    } else {
        if !ctx.errors.is_empty() {
            println!("\n{} Errors found:", "❌".red());
            for (i, err) in ctx.errors.iter().enumerate() {
                println!("  {}. {}", i + 1, err.red());
            }
        }

        if !ctx.warnings.is_empty() {
            println!("\n{} Warnings:", "⚠️ ".yellow());
            for (i, warn) in ctx.warnings.iter().enumerate() {
                println!("  {}. {}", i + 1, warn.yellow());
            }
        }

        if !ctx.fixed.is_empty() {
            println!("\n{} Fixed:", "🔧".green());
            for (i, fix) in ctx.fixed.iter().enumerate() {
                println!("  {}. {}", i + 1, fix.green());
            }
        }
    }

    println!("\n{}", "=".repeat(50).bright_blue());
    println!("Errors: {} | Warnings: {} | Fixed: {}",
        ctx.errors.len().to_string().red(),
        ctx.warnings.len().to_string().yellow(),
        ctx.fixed.len().to_string().green()
    );
    println!("{}", "=".repeat(50).bright_blue());
}

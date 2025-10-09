// mkfs.modernfs - ModernFS 文件系统格式化工具
// Week 8: Rust 工具集 - 格式化工具

use anyhow::{Context, Result, bail};
use clap::Parser;
use colored::Colorize;
use indicatif::{ProgressBar, ProgressStyle};
use std::fs::{File, OpenOptions};
use std::io::Write;
use std::os::unix::fs::FileExt;

// ============ 常量定义 ============

const BLOCK_SIZE: u32 = 4096;
const SUPERBLOCK_MAGIC: u32 = 0x4D4F4446; // "MODF"
const SUPERBLOCK_VERSION: u32 = 1;
const INODE_SIZE: u32 = 128;
const ROOT_INODE: u32 = 1;

const JOURNAL_MAGIC: u32 = 0x4A524E4C; // "JRNL"

// ============ 命令行参数 ============

#[derive(Parser)]
#[command(name = "mkfs.modernfs")]
#[command(about = "Create a ModernFS filesystem", version = "1.0.0")]
struct Args {
    /// Disk image path
    disk: String,

    /// Size (e.g., 128M, 256M, 1G)
    #[arg(short, long)]
    size: String,

    /// Journal size (default: 32M)
    #[arg(short = 'j', long, default_value = "32M")]
    journal_size: String,

    /// Block size (default: 4096)
    #[arg(short, long, default_value = "4096")]
    block_size: u32,

    /// Force overwrite
    #[arg(short, long)]
    force: bool,

    /// Verbose output
    #[arg(short, long)]
    verbose: bool,
}

// ============ 文件系统布局 ============

#[derive(Debug)]
struct FsLayout {
    total_blocks: u32,
    block_size: u32,

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
}

// ============ 超级块结构 ============

#[repr(C, packed)]
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

// ============ 日志超级块 ============

#[repr(C, packed)]
struct JournalSuperblock {
    magic: u32,
    block_size: u32,
    total_blocks: u32,
    sequence: u64,
    head: u32,
    tail: u32,
    padding: [u8; 4064],
}

// ============ Inode 结构 ============

#[repr(C, packed)]
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

// ============ 目录项结构 ============

#[repr(C, packed)]
struct Dirent {
    inum: u32,
    rec_len: u16,
    name_len: u8,
    file_type: u8,
    name: [u8; 255],
}

// ============ 主函数 ============

fn main() -> Result<()> {
    let args = Args::parse();

    // 打印 Banner
    print_banner();

    // 解析大小
    let total_size = parse_size(&args.size)
        .context("Invalid size format")?;
    let journal_size = parse_size(&args.journal_size)
        .context("Invalid journal size format")?;

    // 验证参数
    if args.block_size != 4096 {
        bail!("Only 4096 byte block size is supported");
    }

    if total_size < 64 * 1024 * 1024 {
        bail!("Filesystem size must be at least 64MB");
    }

    if journal_size > total_size / 4 {
        bail!("Journal size too large (max 25% of total size)");
    }

    // 打印配置
    println!("📁 Target: {}", args.disk.bright_yellow());
    println!("💾 Total Size: {} MB", (total_size / 1024 / 1024).to_string().bright_green());
    println!("📝 Journal Size: {} MB", (journal_size / 1024 / 1024).to_string().bright_green());
    println!("🔢 Block Size: {} bytes\n", args.block_size.to_string().bright_green());

    // 确认
    if !args.force {
        print!("Continue? [y/N] ");
        std::io::stdout().flush()?;
        let mut input = String::new();
        std::io::stdin().read_line(&mut input)?;
        if input.trim().to_lowercase() != "y" {
            println!("{}", "❌ Aborted".red());
            return Ok(());
        }
    }

    // 创建进度条
    let pb = ProgressBar::new(7);
    pb.set_style(
        ProgressStyle::default_bar()
            .template("[{elapsed_precise}] {bar:40.cyan/blue} {pos}/{len} {msg}")
            .unwrap()
            .progress_chars("█▓▒░ ")
    );

    // 1. 创建磁盘镜像
    pb.set_message("Creating disk image...");
    let mut disk = create_disk_image(&args.disk, total_size)?;
    pb.inc(1);

    // 2. 计算布局
    pb.set_message("Calculating layout...");
    let layout = calculate_layout(total_size, journal_size, args.block_size)?;
    if args.verbose {
        println!("\n{:#?}", layout);
    }
    pb.inc(1);

    // 3. 写入超级块
    pb.set_message("Writing superblock...");
    write_superblock(&mut disk, &layout)?;
    pb.inc(1);

    // 4. 初始化日志区
    pb.set_message("Initializing journal...");
    init_journal(&mut disk, &layout)?;
    pb.inc(1);

    // 5. 初始化 Inode 位图
    pb.set_message("Initializing inode bitmap...");
    init_inode_bitmap(&mut disk, &layout)?;
    pb.inc(1);

    // 6. 初始化数据位图
    pb.set_message("Initializing data bitmap...");
    init_data_bitmap(&mut disk, &layout)?;
    pb.inc(1);

    // 7. 创建根目录
    pb.set_message("Creating root directory...");
    create_root_directory(&mut disk, &layout)?;
    pb.inc(1);

    pb.finish_with_message("✅ Done!");

    println!("\n{}", "✅ Filesystem created successfully!".bright_green().bold());
    println!("\nMount with:");
    println!("  {} {} {}",
        "modernfs".bright_cyan(),
        args.disk.bright_yellow(),
        "/mnt/point".bright_yellow()
    );

    Ok(())
}

// ============ 辅助函数 ============

fn print_banner() {
    println!("{}", r#"
    ╔═══════════════════════════════════════╗
    ║   ModernFS Filesystem Formatter      ║
    ║   C + Rust Hybrid Architecture       ║
    ╚═══════════════════════════════════════╝
    "#.bright_cyan());
}

fn parse_size(s: &str) -> Result<u64> {
    let s = s.to_uppercase();
    if let Some(num) = s.strip_suffix("M") {
        Ok(num.parse::<u64>()? * 1024 * 1024)
    } else if let Some(num) = s.strip_suffix("G") {
        Ok(num.parse::<u64>()? * 1024 * 1024 * 1024)
    } else if let Some(num) = s.strip_suffix("K") {
        Ok(num.parse::<u64>()? * 1024)
    } else {
        Ok(s.parse::<u64>()?)
    }
}

fn create_disk_image(path: &str, size: u64) -> Result<File> {
    let file = OpenOptions::new()
        .read(true)
        .write(true)
        .create(true)
        .truncate(true)
        .open(path)
        .context("Failed to create disk image")?;

    file.set_len(size)
        .context("Failed to set file size")?;

    Ok(file)
}

fn calculate_layout(total_size: u64, journal_size: u64, block_size: u32) -> Result<FsLayout> {
    let total_blocks = (total_size / block_size as u64) as u32;
    let journal_blocks = (journal_size / block_size as u64) as u32;

    // 布局：[SB][Journal][InodeBitmap][DataBitmap][InodeTable][Data]
    let journal_start = 1;

    // Inode 数量：每 8KB 一个 inode
    let total_inodes = total_blocks / 2;
    let inode_bitmap_blocks = (total_inodes + BLOCK_SIZE * 8 - 1) / (BLOCK_SIZE * 8);

    let inode_bitmap_start = journal_start + journal_blocks;

    // 数据位图（为剩余空间）
    let remaining = total_blocks - inode_bitmap_start;
    let inodes_per_block = BLOCK_SIZE / INODE_SIZE;
    let inode_table_blocks = (total_inodes + inodes_per_block - 1) / inodes_per_block;

    let data_blocks_estimate = remaining - inode_table_blocks - 10; // 预留 10 块
    let data_bitmap_blocks = (data_blocks_estimate + BLOCK_SIZE * 8 - 1) / (BLOCK_SIZE * 8);

    let data_bitmap_start = inode_bitmap_start + inode_bitmap_blocks;
    let inode_table_start = data_bitmap_start + data_bitmap_blocks;
    let data_start = inode_table_start + inode_table_blocks;
    let data_blocks = total_blocks - data_start;

    Ok(FsLayout {
        total_blocks,
        block_size,
        journal_start,
        journal_blocks,
        inode_bitmap_start,
        inode_bitmap_blocks,
        data_bitmap_start,
        data_bitmap_blocks,
        inode_table_start,
        inode_table_blocks,
        data_start,
        data_blocks,
        total_inodes,
    })
}

fn write_superblock(disk: &mut File, layout: &FsLayout) -> Result<()> {
    let sb = Superblock {
        magic: SUPERBLOCK_MAGIC,
        version: SUPERBLOCK_VERSION,
        block_size: layout.block_size,
        total_blocks: layout.total_blocks,
        free_blocks: layout.data_blocks - 1, // 减去根目录占用的块

        journal_start: layout.journal_start,
        journal_blocks: layout.journal_blocks,
        inode_bitmap_start: layout.inode_bitmap_start,
        inode_bitmap_blocks: layout.inode_bitmap_blocks,
        data_bitmap_start: layout.data_bitmap_start,
        data_bitmap_blocks: layout.data_bitmap_blocks,
        inode_table_start: layout.inode_table_start,
        inode_table_blocks: layout.inode_table_blocks,
        data_start: layout.data_start,
        data_blocks: layout.data_blocks,

        total_inodes: layout.total_inodes,
        free_inodes: layout.total_inodes - 1, // 减去根目录的 inode
        first_inode: 2, // inode 0 保留，1 是根目录
        root_inum: ROOT_INODE,

        state: 1, // Clean state
        mount_time: 0,
        write_time: std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap()
            .as_secs(),
        mount_count: 0,

        padding: [0; 3988],
    };

    let sb_bytes = unsafe {
        std::slice::from_raw_parts(
            &sb as *const _ as *const u8,
            std::mem::size_of::<Superblock>()
        )
    };

    disk.write_all_at(sb_bytes, 0)
        .context("Failed to write superblock")?;
    disk.sync_all()?;

    Ok(())
}

fn init_journal(disk: &mut File, layout: &FsLayout) -> Result<()> {
    let journal_sb = JournalSuperblock {
        magic: JOURNAL_MAGIC,
        block_size: layout.block_size,
        total_blocks: layout.journal_blocks,
        sequence: 0,
        head: 1, // 第一个块是日志超级块
        tail: 1,
        padding: [0; 4064],
    };

    let jsb_bytes = unsafe {
        std::slice::from_raw_parts(
            &journal_sb as *const _ as *const u8,
            std::mem::size_of::<JournalSuperblock>()
        )
    };

    let offset = (layout.journal_start as u64) * (layout.block_size as u64);
    disk.write_all_at(jsb_bytes, offset)
        .context("Failed to write journal superblock")?;

    Ok(())
}

fn init_inode_bitmap(disk: &mut File, layout: &FsLayout) -> Result<()> {
    let mut bitmap = vec![0u8; (layout.inode_bitmap_blocks * layout.block_size) as usize];

    // 标记 inode 0 和 1（根目录）为已使用
    bitmap[0] = 0b00000011; // bit 0 和 bit 1

    let offset = (layout.inode_bitmap_start as u64) * (layout.block_size as u64);
    disk.write_all_at(&bitmap, offset)
        .context("Failed to write inode bitmap")?;

    Ok(())
}

fn init_data_bitmap(disk: &mut File, layout: &FsLayout) -> Result<()> {
    let mut bitmap = vec![0u8; (layout.data_bitmap_blocks * layout.block_size) as usize];

    // 标记第一个数据块（根目录）为已使用
    bitmap[0] = 0b00000001;

    let offset = (layout.data_bitmap_start as u64) * (layout.block_size as u64);
    disk.write_all_at(&bitmap, offset)
        .context("Failed to write data bitmap")?;

    Ok(())
}

fn create_root_directory(disk: &mut File, layout: &FsLayout) -> Result<()> {
    let now = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .unwrap()
        .as_secs();

    // 创建根目录 inode
    let root_inode = DiskInode {
        mode: 0o755 | 0o040000, // S_IFDIR | rwxr-xr-x
        uid: 0,
        gid: 0,
        nlink: 2, // . 和 ..
        inode_type: 2, // DIR
        flags: 0,
        reserved: 0,
        size: BLOCK_SIZE as u64, // 一个块的大小
        blocks: 1,
        atime: now,
        mtime: now,
        ctime: now,
        direct: [0xFFFFFFFF; 12],
        indirect: 0xFFFFFFFF,
        double_indirect: 0xFFFFFFFF,
        padding: [0; 20],
    };

    // 根目录的第一个数据块
    let mut root_inode_modified = root_inode;
    root_inode_modified.direct[0] = layout.data_start;

    // 写入根目录 inode（inode 1）
    let inode_bytes = unsafe {
        std::slice::from_raw_parts(
            &root_inode_modified as *const _ as *const u8,
            std::mem::size_of::<DiskInode>()
        )
    };

    let inode_offset = (layout.inode_table_start as u64) * (layout.block_size as u64)
        + (ROOT_INODE as u64) * (INODE_SIZE as u64);
    disk.write_all_at(inode_bytes, inode_offset)
        .context("Failed to write root inode")?;

    // 创建根目录内容（. 和 ..）
    let mut dir_block = vec![0u8; BLOCK_SIZE as usize];

    // "." 目录项
    let mut dot_entry = Dirent {
        inum: ROOT_INODE,
        rec_len: 264, // 4 + 2 + 1 + 1 + 256
        name_len: 1,
        file_type: 2, // DIR
        name: [0; 255],
    };
    dot_entry.name[0] = b'.';

    // ".." 目录项
    let mut dotdot_entry = Dirent {
        inum: ROOT_INODE,
        rec_len: 264,
        name_len: 2,
        file_type: 2,
        name: [0; 255],
    };
    dotdot_entry.name[0] = b'.';
    dotdot_entry.name[1] = b'.';

    // 写入目录项
    unsafe {
        let dot_ptr = &dot_entry as *const _ as *const u8;
        let dot_slice = std::slice::from_raw_parts(dot_ptr, std::mem::size_of::<Dirent>());
        dir_block[0..std::mem::size_of::<Dirent>()].copy_from_slice(dot_slice);

        let dotdot_ptr = &dotdot_entry as *const _ as *const u8;
        let dotdot_slice = std::slice::from_raw_parts(dotdot_ptr, std::mem::size_of::<Dirent>());
        let offset = std::mem::size_of::<Dirent>();
        dir_block[offset..offset + std::mem::size_of::<Dirent>()].copy_from_slice(dotdot_slice);
    }

    // 写入根目录数据块
    let data_offset = (layout.data_start as u64) * (layout.block_size as u64);
    disk.write_all_at(&dir_block, data_offset)
        .context("Failed to write root directory data")?;

    disk.sync_all()?;

    Ok(())
}

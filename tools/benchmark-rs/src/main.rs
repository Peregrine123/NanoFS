// benchmark.modernfs - ModernFS 性能测试工具
// Week 8: Rust 工具集 - 性能测试工具

use anyhow::{Context, Result};
use clap::Parser;
use colored::Colorize;
use indicatif::{ProgressBar, ProgressStyle};
use std::time::{Duration, Instant};

// ============ 命令行参数 ============

#[derive(Parser)]
#[command(name = "benchmark.modernfs")]
#[command(about = "Benchmark ModernFS filesystem performance", version = "1.0.0")]
struct Args {
    /// Mount point or disk image
    target: String,

    /// Test type: all, seq-write, seq-read, rand-write, rand-read, mkdir
    #[arg(short, long, default_value = "all")]
    test: String,

    /// Number of files/operations
    #[arg(short, long, default_value = "100")]
    count: usize,

    /// File size in KB (for I/O tests)
    #[arg(short = 's', long, default_value = "4")]
    size_kb: usize,

    /// Verbose output
    #[arg(short, long)]
    verbose: bool,
}

// ============ 测试结果 ============

#[derive(Debug)]
struct BenchmarkResult {
    name: String,
    operations: usize,
    duration: Duration,
    throughput: f64, // ops/sec
    bandwidth: Option<f64>, // MB/s
}

impl BenchmarkResult {
    fn new(name: String, operations: usize, duration: Duration, data_mb: Option<f64>) -> Self {
        let secs = duration.as_secs_f64();
        let throughput = operations as f64 / secs;
        let bandwidth = data_mb.map(|mb| mb / secs);

        Self {
            name,
            operations,
            duration,
            throughput,
            bandwidth,
        }
    }

    fn print(&self) {
        println!("\n{} {}", "📊".bright_cyan(), self.name.bright_yellow().bold());
        println!("  Operations: {}", self.operations.to_string().bright_green());
        println!("  Duration: {:.2} s", self.duration.as_secs_f64());
        println!("  Throughput: {:.2} ops/sec", self.throughput);
        if let Some(bw) = self.bandwidth {
            println!("  Bandwidth: {:.2} MB/s", bw);
        }
    }
}

// ============ 主函数 ============

fn main() -> Result<()> {
    let args = Args::parse();

    print_banner();

    println!("🎯 Target: {}\n", args.target.bright_yellow());

    let mut results = Vec::new();

    match args.test.as_str() {
        "all" => {
            results.push(bench_seq_write(&args)?);
            results.push(bench_seq_read(&args)?);
            results.push(bench_mkdir(&args)?);
        }
        "seq-write" => {
            results.push(bench_seq_write(&args)?);
        }
        "seq-read" => {
            results.push(bench_seq_read(&args)?);
        }
        "mkdir" => {
            results.push(bench_mkdir(&args)?);
        }
        _ => {
            eprintln!("{} Unknown test type: {}", "❌".red(), args.test);
            std::process::exit(1);
        }
    }

    // 打印汇总
    print_summary(&results);

    Ok(())
}

// ============ 测试函数 ============

fn bench_seq_write(args: &Args) -> Result<BenchmarkResult> {
    println!("{}", "Test 1: Sequential Write".bright_blue().bold());

    let target = std::path::Path::new(&args.target);
    if !target.exists() {
        eprintln!("{} Target does not exist: {}", "❌".red(), args.target);
        std::process::exit(1);
    }

    let pb = ProgressBar::new(args.count as u64);
    pb.set_style(
        ProgressStyle::default_bar()
            .template("[{elapsed_precise}] {bar:40.cyan/blue} {pos}/{len} {msg}")
            .unwrap()
            .progress_chars("█▓▒░ ")
    );

    let data = vec![0u8; args.size_kb * 1024];
    let total_mb = (args.count * args.size_kb) as f64 / 1024.0;

    let start = Instant::now();

    for i in 0..args.count {
        let file_path = target.join(format!("bench_write_{}.dat", i));
        std::fs::write(&file_path, &data)
            .context(format!("Failed to write file {}", i))?;
        pb.inc(1);
    }

    let duration = start.elapsed();
    pb.finish_with_message("✅ Done");

    // 清理
    for i in 0..args.count {
        let file_path = target.join(format!("bench_write_{}.dat", i));
        let _ = std::fs::remove_file(&file_path);
    }

    Ok(BenchmarkResult::new(
        "Sequential Write".to_string(),
        args.count,
        duration,
        Some(total_mb),
    ))
}

fn bench_seq_read(args: &Args) -> Result<BenchmarkResult> {
    println!("\n{}", "Test 2: Sequential Read".bright_blue().bold());

    let target = std::path::Path::new(&args.target);

    // 先创建测试文件
    let data = vec![0u8; args.size_kb * 1024];
    for i in 0..args.count {
        let file_path = target.join(format!("bench_read_{}.dat", i));
        std::fs::write(&file_path, &data)?;
    }

    let pb = ProgressBar::new(args.count as u64);
    pb.set_style(
        ProgressStyle::default_bar()
            .template("[{elapsed_precise}] {bar:40.cyan/blue} {pos}/{len} {msg}")
            .unwrap()
            .progress_chars("█▓▒░ ")
    );

    let total_mb = (args.count * args.size_kb) as f64 / 1024.0;

    let start = Instant::now();

    for i in 0..args.count {
        let file_path = target.join(format!("bench_read_{}.dat", i));
        let _ = std::fs::read(&file_path)
            .context(format!("Failed to read file {}", i))?;
        pb.inc(1);
    }

    let duration = start.elapsed();
    pb.finish_with_message("✅ Done");

    // 清理
    for i in 0..args.count {
        let file_path = target.join(format!("bench_read_{}.dat", i));
        let _ = std::fs::remove_file(&file_path);
    }

    Ok(BenchmarkResult::new(
        "Sequential Read".to_string(),
        args.count,
        duration,
        Some(total_mb),
    ))
}

fn bench_mkdir(args: &Args) -> Result<BenchmarkResult> {
    println!("\n{}", "Test 3: Directory Creation".bright_blue().bold());

    let target = std::path::Path::new(&args.target);

    let pb = ProgressBar::new(args.count as u64);
    pb.set_style(
        ProgressStyle::default_bar()
            .template("[{elapsed_precise}] {bar:40.cyan/blue} {pos}/{len} {msg}")
            .unwrap()
            .progress_chars("█▓▒░ ")
    );

    let start = Instant::now();

    for i in 0..args.count {
        let dir_path = target.join(format!("bench_dir_{}", i));
        std::fs::create_dir(&dir_path)
            .context(format!("Failed to create directory {}", i))?;
        pb.inc(1);
    }

    let duration = start.elapsed();
    pb.finish_with_message("✅ Done");

    // 清理
    for i in 0..args.count {
        let dir_path = target.join(format!("bench_dir_{}", i));
        let _ = std::fs::remove_dir(&dir_path);
    }

    Ok(BenchmarkResult::new(
        "Directory Creation".to_string(),
        args.count,
        duration,
        None,
    ))
}

// ============ 输出函数 ============

fn print_banner() {
    println!("{}", r#"
    ╔═══════════════════════════════════════╗
    ║   ModernFS Benchmark Tool            ║
    ║   Performance Testing Suite          ║
    ╚═══════════════════════════════════════╝
    "#.bright_cyan());
}

fn print_summary(results: &[BenchmarkResult]) {
    println!("\n{}", "="
.repeat(50).bright_blue());
    println!("{}", "  BENCHMARK SUMMARY".bright_cyan().bold());
    println!("{}", "=".repeat(50).bright_blue());

    for result in results {
        result.print();
    }

    println!("\n{}", "=".repeat(50).bright_blue());
    println!("{}", "✅ All benchmarks completed!".bright_green().bold());
    println!("{}", "=".repeat(50).bright_blue());
}

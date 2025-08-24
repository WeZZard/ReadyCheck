//! Simplified Coverage Helper - Orchestrates existing tools per Option A architecture
//! 
//! This is a Cargo-based orchestrator that calls existing production tools:
//! - cargo-llvm-cov for Rust
//! - llvm-cov/llvm-profdata for C/C++
//! - pytest-cov for Python
//! - lcov for merging
//! - genhtml for HTML reports
//! - diff-cover for changed-line enforcement

use anyhow::{Context, Result};
use clap::{Parser, Subcommand};
use std::env;
use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;

#[derive(Parser)]
#[command(name = "coverage_helper")]
#[command(about = "Coverage orchestrator for ADA project", long_about = None)]
struct Cli {
    #[command(subcommand)]
    command: Commands,
}

#[derive(Subcommand)]
enum Commands {
    /// Run full coverage workflow (clean, test, collect, report)
    Full,
    
    /// Run incremental coverage check on changed files only
    Incremental,
    
    /// Check that all required tools are installed
    CheckEnvironment,
    
    /// Clean coverage directories
    Clean,
}

fn main() -> Result<()> {
    let cli = Cli::parse();
    
    match cli.command {
        Commands::Full => run_full_coverage(),
        Commands::Incremental => run_incremental_coverage(),
        Commands::CheckEnvironment => check_environment(),
        Commands::Clean => clean_coverage_dirs(),
    }
}

fn get_workspace_root() -> Result<PathBuf> {
    let output = Command::new("cargo")
        .args(&["locate-project", "--workspace", "--message-format=plain"])
        .output()
        .context("Failed to locate workspace root")?;
    
    let cargo_toml = PathBuf::from(String::from_utf8_lossy(&output.stdout).trim());
    Ok(cargo_toml.parent().unwrap().to_path_buf())
}

fn clean_coverage_dirs() -> Result<()> {
    println!("Cleaning coverage directories...");
    
    let workspace = get_workspace_root()?;
    let temp_dir = workspace.join("target/coverage");
    let report_dir = workspace.join("target/coverage_report");
    
    // Clean temporary coverage data
    if temp_dir.exists() {
        fs::remove_dir_all(&temp_dir)?;
    }
    fs::create_dir_all(&temp_dir)?;
    
    // Keep report directory but clear contents
    if report_dir.exists() {
        for entry in fs::read_dir(&report_dir)? {
            let entry = entry?;
            let path = entry.path();
            if path.is_file() || path.is_dir() {
                if path.is_dir() {
                    fs::remove_dir_all(path)?;
                } else {
                    fs::remove_file(path)?;
                }
            }
        }
    }
    fs::create_dir_all(&report_dir)?;
    
    println!("✅ Coverage directories cleaned");
    Ok(())
}

fn check_environment() -> Result<()> {
    println!("Checking coverage tool environment...");
    
    let workspace = get_workspace_root()?;
    let setup_script = workspace.join("utils/setup_coverage_tools.sh");
    
    // Run the setup check script
    let status = Command::new("bash")
        .arg(setup_script)
        .status()
        .context("Failed to run setup_coverage_tools.sh")?;
    
    if !status.success() {
        anyhow::bail!("Environment check failed - missing required tools");
    }
    
    Ok(())
}

fn collect_rust_coverage() -> Result<()> {
    println!("📦 Collecting Rust coverage...");
    
    let workspace = get_workspace_root()?;
    let output_path = workspace.join("target/coverage/rust.lcov");
    
    // Use cargo-llvm-cov to generate LCOV for Rust
    let status = Command::new("cargo")
        .args(&[
            "llvm-cov",
            "--workspace",
            "--lcov",
            "--output-path", output_path.to_str().unwrap(),
        ])
        .status()
        .context("Failed to run cargo-llvm-cov")?;
    
    if !status.success() {
        anyhow::bail!("Rust coverage collection failed");
    }
    
    println!("✅ Rust coverage collected");
    Ok(())
}

fn collect_cpp_coverage() -> Result<()> {
    println!("🔧 Collecting C/C++ coverage...");
    
    let workspace = get_workspace_root()?;
    
    // Set environment for C/C++ coverage
    env::set_var("LLVM_PROFILE_FILE", "target/coverage/cpp-%p-%m.profraw");
    env::set_var("CARGO_FEATURE_COVERAGE", "1");
    
    // Build and test C/C++ with coverage
    let status = Command::new("cargo")
        .args(&["test", "--features", "tracer_backend/coverage"])
        .env("CARGO_FEATURE_COVERAGE", "1")
        .env("CC", "clang")
        .env("CXX", "clang++")
        .status()
        .context("Failed to run C/C++ tests with coverage")?;
    
    if !status.success() {
        anyhow::bail!("C/C++ tests failed");
    }
    
    // Merge profraw files
    let profraw_pattern = workspace.join("target/coverage/cpp-*.profraw");
    let profdata_path = workspace.join("target/coverage/cpp.profdata");
    
    let status = Command::new("llvm-profdata")
        .args(&[
            "merge",
            "-sparse",
            profraw_pattern.to_str().unwrap(),
            "-o", profdata_path.to_str().unwrap(),
        ])
        .status()
        .context("Failed to merge C/C++ profile data")?;
    
    if !status.success() {
        eprintln!("⚠️  No C/C++ coverage data found (may be normal if no C/C++ tests)");
        // Create empty LCOV file so merge doesn't fail
        fs::write(workspace.join("target/coverage/cpp.lcov"), "")?;
        return Ok(());
    }
    
    // Export to LCOV format
    // TODO: Need to collect all test binaries dynamically
    let test_binaries = find_test_binaries(&workspace)?;
    
    if test_binaries.is_empty() {
        eprintln!("⚠️  No C/C++ test binaries found");
        fs::write(workspace.join("target/coverage/cpp.lcov"), "")?;
        return Ok(());
    }
    
    let mut cmd = Command::new("llvm-cov");
    cmd.args(&["export", "--format=lcov"]);
    cmd.args(&["--instr-profile", profdata_path.to_str().unwrap()]);
    
    for binary in &test_binaries {
        cmd.args(&["--object", binary.to_str().unwrap()]);
    }
    
    let output = cmd.output()
        .context("Failed to export C/C++ coverage to LCOV")?;
    
    if !output.status.success() {
        eprintln!("⚠️  C/C++ LCOV export failed");
        fs::write(workspace.join("target/coverage/cpp.lcov"), "")?;
    } else {
        fs::write(workspace.join("target/coverage/cpp.lcov"), output.stdout)?;
        println!("✅ C/C++ coverage collected");
    }
    
    Ok(())
}

fn collect_python_coverage() -> Result<()> {
    println!("🐍 Collecting Python coverage...");
    
    let workspace = get_workspace_root()?;
    
    // Check each Python component
    for component in &["query_engine", "mcp_server"] {
        let component_dir = workspace.join(component);
        if !component_dir.exists() {
            continue;
        }
        
        println!("  Testing {}...", component);
        
        // Run pytest with coverage
        let status = Command::new("pytest")
            .args(&[
                "--cov", component,
                "--cov-report", "xml:target/coverage/python_temp.xml",
                "-q",
            ])
            .current_dir(&component_dir)
            .status();
        
        if let Ok(status) = status {
            if status.success() {
                // Convert XML to LCOV
                let lcov_path = workspace.join(format!("target/coverage/{}.lcov", component));
                let xml_path = workspace.join("target/coverage/python_temp.xml");
                
                let status = Command::new("coverage-lcov")
                    .args(&[
                        "-i", xml_path.to_str().unwrap(),
                        "-o", lcov_path.to_str().unwrap(),
                    ])
                    .status();
                
                if status.is_err() {
                    eprintln!("⚠️  coverage-lcov not found, trying alternate method");
                    // Try alternate conversion if coverage-lcov not available
                    // Just create empty file for now
                    fs::write(lcov_path, "")?;
                }
                
                // Clean up temp file
                fs::remove_file(xml_path).ok();
            }
        }
    }
    
    // Merge Python LCOV files
    let mut python_lcovs = vec![];
    for component in &["query_engine", "mcp_server"] {
        let lcov_path = workspace.join(format!("target/coverage/{}.lcov", component));
        if lcov_path.exists() && fs::metadata(&lcov_path)?.len() > 0 {
            python_lcovs.push(lcov_path);
        }
    }
    
    if python_lcovs.is_empty() {
        eprintln!("⚠️  No Python coverage data found");
        fs::write(workspace.join("target/coverage/python.lcov"), "")?;
    } else {
        // Merge Python LCOV files
        let mut cmd = Command::new("lcov");
        cmd.args(&["-o", workspace.join("target/coverage/python.lcov").to_str().unwrap()]);
        
        for lcov in python_lcovs {
            cmd.args(&["-a", lcov.to_str().unwrap()]);
        }
        
        cmd.status().context("Failed to merge Python LCOV files")?;
        println!("✅ Python coverage collected");
    }
    
    Ok(())
}

fn merge_lcov_files() -> Result<()> {
    println!("🔄 Merging LCOV files...");
    
    let workspace = get_workspace_root()?;
    let merged_path = workspace.join("target/coverage_report/merged.lcov");
    
    // Find all LCOV files to merge
    let lcov_files = vec![
        workspace.join("target/coverage/rust.lcov"),
        workspace.join("target/coverage/cpp.lcov"),
        workspace.join("target/coverage/python.lcov"),
    ];
    
    // Filter to only existing non-empty files
    let valid_lcovs: Vec<_> = lcov_files
        .into_iter()
        .filter(|p| p.exists() && fs::metadata(p).map(|m| m.len() > 0).unwrap_or(false))
        .collect();
    
    if valid_lcovs.is_empty() {
        anyhow::bail!("No LCOV files found to merge");
    }
    
    // Merge with lcov tool
    let mut cmd = Command::new("lcov");
    cmd.args(&["-o", merged_path.to_str().unwrap()]);
    
    for lcov in valid_lcovs {
        cmd.args(&["-a", lcov.to_str().unwrap()]);
    }
    
    let status = cmd.status()
        .context("Failed to merge LCOV files")?;
    
    if !status.success() {
        anyhow::bail!("LCOV merge failed");
    }
    
    println!("✅ LCOV files merged");
    Ok(())
}

fn generate_html_report() -> Result<()> {
    println!("📊 Generating HTML report...");
    
    let workspace = get_workspace_root()?;
    let merged_lcov = workspace.join("target/coverage_report/merged.lcov");
    let html_dir = workspace.join("target/coverage_report/html");
    
    if !merged_lcov.exists() {
        anyhow::bail!("No merged LCOV file found");
    }
    
    // Generate HTML with genhtml
    let status = Command::new("genhtml")
        .args(&[
            merged_lcov.to_str().unwrap(),
            "--legend",
            "--branch-coverage",
            "--output-directory", html_dir.to_str().unwrap(),
        ])
        .status()
        .context("Failed to generate HTML report")?;
    
    if !status.success() {
        anyhow::bail!("HTML generation failed");
    }
    
    println!("✅ HTML report generated: {}/index.html", html_dir.display());
    
    // Print summary
    let output = Command::new("lcov")
        .args(&["--summary", merged_lcov.to_str().unwrap()])
        .output()
        .context("Failed to get coverage summary")?;
    
    println!("\nCoverage Summary:");
    println!("{}", String::from_utf8_lossy(&output.stderr));
    
    Ok(())
}

fn check_changed_lines() -> Result<()> {
    println!("🔍 Checking coverage for changed lines...");
    
    let workspace = get_workspace_root()?;
    let merged_lcov = workspace.join("target/coverage_report/merged.lcov");
    
    if !merged_lcov.exists() {
        anyhow::bail!("No merged LCOV file found - run 'full' first");
    }
    
    // Run diff-cover
    let output = Command::new("diff-cover")
        .args(&[
            merged_lcov.to_str().unwrap(),
            "--compare-branch=origin/main",
            "--fail-under=100",
        ])
        .output()
        .context("Failed to run diff-cover")?;
    
    // Print output
    println!("{}", String::from_utf8_lossy(&output.stdout));
    
    if !output.status.success() {
        eprintln!("{}", String::from_utf8_lossy(&output.stderr));
        anyhow::bail!("Changed lines do not have 100% coverage");
    }
    
    println!("✅ All changed lines have 100% coverage");
    Ok(())
}

fn find_test_binaries(workspace: &Path) -> Result<Vec<PathBuf>> {
    let mut binaries = vec![];
    
    // Look for test binaries in typical locations
    let search_dirs = vec![
        workspace.join("target/debug/deps"),
        workspace.join("target/release/deps"),
        workspace.join("target/debug"),
        workspace.join("target/release"),
    ];
    
    for dir in search_dirs {
        if !dir.exists() {
            continue;
        }
        
        for entry in fs::read_dir(dir)? {
            let entry = entry?;
            let path = entry.path();
            
            // Look for test executables (heuristic: contains "test" in name)
            if path.is_file() {
                if let Some(name) = path.file_name() {
                    let name_str = name.to_string_lossy();
                    if name_str.contains("test") && !name_str.ends_with(".d") {
                        binaries.push(path);
                    }
                }
            }
        }
    }
    
    Ok(binaries)
}

fn run_full_coverage() -> Result<()> {
    println!("🚀 Running full coverage workflow...\n");
    
    // Check environment first
    check_environment()?;
    
    // Clean old data
    clean_coverage_dirs()?;
    
    // Collect coverage for each language
    collect_rust_coverage()?;
    collect_cpp_coverage()?;
    collect_python_coverage()?;
    
    // Merge and generate reports
    merge_lcov_files()?;
    generate_html_report()?;
    
    println!("\n✅ Full coverage workflow completed!");
    println!("📊 View report: target/coverage_report/html/index.html");
    
    Ok(())
}

fn run_incremental_coverage() -> Result<()> {
    println!("🔄 Running incremental coverage check...\n");
    
    // Get changed files
    let output = Command::new("git")
        .args(&["diff", "--cached", "--name-only"])
        .output()
        .context("Failed to get staged files")?;
    
    let changed_files = String::from_utf8_lossy(&output.stdout);
    
    if changed_files.trim().is_empty() {
        // Check last commit if no staged files
        let output = Command::new("git")
            .args(&["diff", "HEAD~1", "--name-only"])
            .output()
            .context("Failed to get changed files from last commit")?;
        
        let changed_files = String::from_utf8_lossy(&output.stdout);
        
        if changed_files.trim().is_empty() {
            println!("No changed files detected");
            return Ok(());
        }
    }
    
    println!("Changed files detected:\n{}", changed_files);
    
    // Check if only docs changed
    let only_docs = changed_files
        .lines()
        .all(|f| f.ends_with(".md") || f.ends_with(".txt") || f.starts_with("docs/"));
    
    if only_docs {
        println!("✅ Only documentation files changed - skipping coverage check");
        return Ok(());
    }
    
    // Run coverage check on changed lines
    check_changed_lines()?;
    
    Ok(())
}
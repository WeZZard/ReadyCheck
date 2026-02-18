//! E2E Benchmark: Measures ADA tracing overhead across the full
//! (os, toolchain, lang, effective_config) matrix.
//!
//! This test is `#[ignore]`d by default — run explicitly:
//!   cargo test --release -p tracer_backend --test bench_e2e_overhead -- --ignored --nocapture

use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::env;
use std::fs;
use std::io::Read as _;
#[cfg(unix)]
use std::os::unix::process::CommandExt;
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};
use std::time::{Duration, Instant};

// ========================== Configuration ==========================

#[derive(Debug, Deserialize)]
struct Config {
    workload: WorkloadConfig,
    /// 3-level: platform → lang → config
    thresholds: HashMap<String, HashMap<String, HashMap<String, ThresholdConfig>>>,
    ci_overrides: Option<CiOverrides>,
}

#[derive(Debug, Deserialize)]
struct WorkloadConfig {
    #[allow(dead_code)]
    outer: u32,
    #[allow(dead_code)]
    inner: u32,
    #[allow(dead_code)]
    depth: u32,
    #[allow(dead_code)]
    threads: u32,
    #[allow(dead_code)]
    thread_iterations: u32,
    warmup_runs: u32,
    measure_runs: u32,
}

#[derive(Debug, Deserialize)]
struct ThresholdConfig {
    #[allow(dead_code)]
    max_overhead_ratio: f64,
    max_per_call_ns: f64,
    max_memory_overhead_kb: u64,
    max_trace_bytes_mb: u64,
}

#[derive(Debug, Deserialize)]
struct CiOverrides {
    #[allow(dead_code)]
    overhead_ratio_multiplier: f64,
    per_call_ns_multiplier: f64,
    memory_multiplier: f64,
}

// ========================== Results ==========================

#[derive(Debug, Serialize)]
struct BenchResults {
    timestamp: String,
    git_sha: String,
    platform: String,
    arch: String,
    /// platform → lang_config → result
    results: HashMap<String, HashMap<String, LanguageResult>>,
    verdict: Verdict,
}

#[derive(Debug, Clone, Serialize)]
struct LanguageResult {
    total_calls: u64,
    baseline_median_ms: f64,
    traced_median_ms: f64,
    overhead_ratio: f64,
    frida_startup_ms: f64,
    per_call_ns: f64,
    memory_overhead_kb: i64,
    baseline_rss_kb: u64,
    traced_rss_kb: u64,
    trace_bytes: u64,
    events_captured: u64,
    hooks_installed: u32,
    fallback_events: u64,
    estimated_fn_calls: u64,
    threshold_pass: bool,
    failures: Vec<String>,
}

#[derive(Debug, Serialize)]
struct Verdict {
    pass: bool,
    failures: Vec<String>,
    configs_tested: usize,
}

// ========================== Tracer Stats ==========================

#[derive(Debug, Deserialize, Default)]
struct TracerStatsFile {
    #[serde(default)]
    events_captured: u64,
    #[serde(default)]
    events_dropped: u64,
    #[serde(default)]
    bytes_written: u64,
    #[serde(default)]
    hooks_installed: u32,
    #[serde(default)]
    fallback_events: u64,
}

fn parse_tracer_stats(output_dir: &Path) -> Option<TracerStatsFile> {
    let stats_path = output_dir.join("tracer_stats.json");
    let content = fs::read_to_string(&stats_path).ok()?;
    serde_json::from_str(&content).ok()
}

// ========================== Measurement ==========================

struct RunMeasurement {
    wall_ms: f64,
    rss_kb: u64,
}

/// Per-run timeout: kill the process tree if it takes longer than this.
const RUN_TIMEOUT: Duration = Duration::from_secs(120);

fn run_measured(program: &Path, args: &[&str]) -> Result<(RunMeasurement, String), String> {
    let start = Instant::now();

    let time_flag = if cfg!(target_os = "macos") { "-l" } else { "-v" };

    let mut cmd = Command::new("/usr/bin/time");
    cmd.arg(time_flag)
        .arg(program)
        .args(args)
        .stdout(Stdio::piped())
        .stderr(Stdio::piped());

    // Create a new process group so we can kill the entire tree on timeout.
    #[cfg(unix)]
    unsafe {
        cmd.pre_exec(|| {
            libc::setpgid(0, 0);
            Ok(())
        });
    }

    let mut child = cmd.spawn().map_err(|e| format!("Failed to spawn: {}", e))?;
    let child_pid = child.id();

    // Read stderr in a background thread to avoid pipe buffer deadlock.
    let mut stderr_pipe = child.stderr.take().unwrap();
    let stderr_thread = std::thread::spawn(move || {
        let mut buf = String::new();
        let _ = stderr_pipe.read_to_string(&mut buf);
        buf
    });

    // Poll for completion with a timeout.
    let status = loop {
        match child.try_wait() {
            Ok(Some(s)) => break Ok(s),
            Ok(None) => {
                if start.elapsed() > RUN_TIMEOUT {
                    // Kill the entire process group (time + tracer + workload).
                    #[cfg(unix)]
                    unsafe {
                        libc::kill(-(child_pid as i32), libc::SIGKILL);
                    }
                    #[cfg(not(unix))]
                    let _ = child.kill();

                    let _ = child.wait();
                    break Err(format!(
                        "Process timed out after {}s",
                        RUN_TIMEOUT.as_secs()
                    ));
                }
                std::thread::sleep(Duration::from_millis(200));
            }
            Err(e) => {
                break Err(format!("Wait error: {}", e));
            }
        }
    };

    let wall_ms = start.elapsed().as_secs_f64() * 1000.0;

    match status {
        Ok(s) => {
            // Normal exit — join stderr thread to get output.
            let stderr = stderr_thread.join().unwrap_or_default();
            let rss_kb = parse_rss(&stderr);
            if !s.success() {
                eprintln!(
                    "    Warning: process exited with status {:?}",
                    s.code()
                );
            }
            Ok((RunMeasurement { wall_ms, rss_kb }, stderr))
        }
        Err(e) => {
            // Timeout/error — drop the stderr thread (it will finish
            // once all pipe writers die from the process-group kill).
            Err(e)
        }
    }
}

fn parse_rss(stderr: &str) -> u64 {
    if cfg!(target_os = "macos") {
        for line in stderr.lines() {
            let trimmed = line.trim();
            if trimmed.ends_with("maximum resident set size") {
                if let Some(num_str) = trimmed.split_whitespace().next() {
                    if let Ok(bytes) = num_str.parse::<u64>() {
                        return bytes / 1024;
                    }
                }
            }
        }
    } else {
        for line in stderr.lines() {
            if line.contains("Maximum resident set size") {
                if let Some(num_str) = line.split(':').nth(1) {
                    if let Ok(kb) = num_str.trim().parse::<u64>() {
                        return kb;
                    }
                }
            }
        }
    }
    0
}

fn parse_total_calls(stderr: &str) -> Option<u64> {
    for line in stderr.lines() {
        if line.starts_with("BENCH_RESULT") || line.contains("BENCH_RESULT") {
            for part in line.split_whitespace() {
                if let Some(val) = part.strip_prefix("total_calls=") {
                    return val.parse().ok();
                }
            }
        }
    }
    None
}

fn parse_hotpath_ms(stderr: &str) -> Option<f64> {
    for line in stderr.lines() {
        if line.starts_with("BENCH_RESULT") || line.contains("BENCH_RESULT") {
            for part in line.split_whitespace() {
                if let Some(val) = part.strip_prefix("hotpath_ms=") {
                    return val.parse().ok();
                }
            }
        }
    }
    None
}

fn median_f64(values: &mut [f64]) -> f64 {
    values.sort_by(|a, b| a.partial_cmp(b).unwrap_or(std::cmp::Ordering::Equal));
    let n = values.len();
    if n == 0 {
        return 0.0;
    }
    if n % 2 == 0 {
        (values[n / 2 - 1] + values[n / 2]) / 2.0
    } else {
        values[n / 2]
    }
}

fn median_u64(values: &mut [u64]) -> u64 {
    values.sort();
    let n = values.len();
    if n == 0 {
        return 0;
    }
    if n % 2 == 0 {
        (values[n / 2 - 1] + values[n / 2]) / 2
    } else {
        values[n / 2]
    }
}

fn dir_size(path: &Path) -> u64 {
    let mut total = 0u64;
    if let Ok(entries) = fs::read_dir(path) {
        for entry in entries.flatten() {
            let p = entry.path();
            if p.is_file() {
                total += fs::metadata(&p).map(|m| m.len()).unwrap_or(0);
            } else if p.is_dir() {
                total += dir_size(&p);
            }
        }
    }
    total
}

// ========================== Discovery ==========================

fn detect_platform() -> &'static str {
    if cfg!(target_os = "macos") {
        "macos"
    } else if cfg!(target_os = "linux") {
        "linux"
    } else if cfg!(target_os = "windows") {
        "windows"
    } else {
        "unknown"
    }
}

fn detect_arch() -> String {
    env::consts::ARCH.to_string()
}

fn git_sha() -> String {
    Command::new("git")
        .args(["rev-parse", "--short", "HEAD"])
        .output()
        .ok()
        .and_then(|o| {
            if o.status.success() {
                Some(String::from_utf8_lossy(&o.stdout).trim().to_string())
            } else {
                None
            }
        })
        .unwrap_or_else(|| "unknown".into())
}

fn find_project_root() -> PathBuf {
    let manifest_dir = PathBuf::from(env!("CARGO_MANIFEST_DIR"));
    manifest_dir
        .parent()
        .unwrap_or(&manifest_dir)
        .to_path_buf()
}

fn find_tracer(project_root: &Path) -> Option<PathBuf> {
    // Benchmarks MUST use the release build of the tracer.
    // Check the predictable tracer_backend location first, then fallback.
    let predictable = project_root
        .join("target/release/tracer_backend/bin/tracer");
    if predictable.exists() {
        return Some(predictable);
    }
    let legacy = project_root.join("target/release/tracer");
    if legacy.exists() {
        Some(legacy)
    } else {
        None
    }
}

/// Map lang to the product name inside the .app bundle's Contents/MacOS/.
fn app_product_name(lang: &str) -> &str {
    match lang {
        "c" => "BenchWorkloadCApp",
        "cpp" => "BenchWorkloadCppApp",
        "objc" => "BenchWorkloadObjcApp",
        "swift" => "BenchWorkloadSwiftApp",
        _ => "BenchWorkload",
    }
}

fn find_workload_binary(project_root: &Path, lang: &str, config: &str) -> Option<PathBuf> {
    let name = format!("bench_workload_{}_{}", lang, config);
    for profile in ["release", "debug"] {
        let base = project_root
            .join("target")
            .join(profile)
            .join("tracer_backend/test");

        // CLI executables
        let path = base.join(&name);
        if path.exists() && path.is_file() {
            return Some(path);
        }

        // App bundles — return executable inside .app
        let app_path = base.join(format!("{}.app", name));
        if app_path.exists() && app_path.is_dir() {
            let product = app_product_name(lang);
            let exe = app_path.join(format!("Contents/MacOS/{}", product));
            if exe.exists() {
                return Some(exe);
            }
        }
    }
    None
}

fn ensure_agent_rpath(project_root: &Path) {
    if let Ok(existing) = env::var("ADA_AGENT_RPATH_SEARCH_PATHS") {
        if !existing.trim().is_empty() {
            return;
        }
    }

    #[cfg(target_os = "macos")]
    let lib_name = "libfrida_agent.dylib";
    #[cfg(not(target_os = "macos"))]
    let lib_name = "libfrida_agent.so";

    let mut search_paths = Vec::new();

    for profile in ["release", "debug"] {
        let candidate = project_root
            .join("target")
            .join(profile)
            .join("tracer_backend/lib");
        if candidate.join(lib_name).exists() {
            search_paths.push(candidate);
        }
    }

    if !search_paths.is_empty() {
        let joined = search_paths
            .iter()
            .map(|p| p.to_string_lossy().to_string())
            .collect::<Vec<_>>()
            .join(":");
        eprintln!("  Setting ADA_AGENT_RPATH_SEARCH_PATHS={}", joined);
        unsafe { env::set_var("ADA_AGENT_RPATH_SEARCH_PATHS", &joined) };
    } else {
        eprintln!(
            "  Warning: {} not found under {}/target/*/tracer_backend/lib/",
            lib_name,
            project_root.display()
        );
    }
}

fn lang_display_name(lang: &str) -> &str {
    match lang {
        "c" => "C",
        "cpp" => "C++",
        "objc" => "Objective-C",
        "swift" => "Swift",
        _ => lang,
    }
}

fn config_display_name(config: &str) -> &str {
    match config {
        "cli_export_sym" => "CLI (export_sym)",
        "cli_no_export_sym" => "CLI (no_export_sym)",
        "app_debug" => "App Debug",
        "app_debug_dylib" => "App Debug+Dylib",
        "app_release" => "App Release",
        _ => config,
    }
}

// ========================== Benchmark Core ==========================

fn bench_config(
    lang: &str,
    config: &str,
    workload_bin: &Path,
    tracer_bin: &Path,
    wl_config: &WorkloadConfig,
    threshold: &ThresholdConfig,
    is_ci: bool,
    ci_overrides: Option<&CiOverrides>,
) -> LanguageResult {
    let label = format!(
        "{}/{}",
        lang_display_name(lang),
        config_display_name(config)
    );
    eprintln!(
        "  [{}] Warming up ({} runs)...",
        label, wl_config.warmup_runs
    );

    // Warmup: baseline
    for _ in 0..wl_config.warmup_runs {
        let _ = run_measured(workload_bin, &[]);
    }

    // Warmup: traced
    for _ in 0..wl_config.warmup_runs {
        let tmp = tempfile::tempdir().unwrap();
        let output_dir = tmp.path().to_str().unwrap();
        let wb = workload_bin.to_str().unwrap();
        let _ = run_measured(tracer_bin, &["spawn", wb, "--output", output_dir]);
    }

    eprintln!(
        "  [{}] Measuring baseline ({} runs)...",
        label, wl_config.measure_runs
    );

    let mut baseline_walls = Vec::new();
    let mut baseline_rss = Vec::new();
    let mut baseline_hotpaths = Vec::new();
    let mut total_calls: u64 = 0;

    for i in 0..wl_config.measure_runs {
        match run_measured(workload_bin, &[]) {
            Ok((m, stderr)) => {
                baseline_walls.push(m.wall_ms);
                baseline_rss.push(m.rss_kb);
                if let Some(hp) = parse_hotpath_ms(&stderr) {
                    baseline_hotpaths.push(hp);
                }
                if i == 0 {
                    if let Some(tc) = parse_total_calls(&stderr) {
                        total_calls = tc;
                    }
                }
            }
            Err(e) => {
                eprintln!("    Baseline run {} failed: {}", i, e);
            }
        }
    }

    if total_calls == 0 {
        eprintln!(
            "    Warning: Could not parse total_calls from workload stderr"
        );
    }

    eprintln!(
        "  [{}] Measuring traced ({} runs)...",
        label, wl_config.measure_runs
    );

    let mut traced_walls = Vec::new();
    let mut traced_rss = Vec::new();
    let mut traced_hotpaths = Vec::new();
    let mut trace_sizes = Vec::new();
    let mut last_tracer_stats: Option<TracerStatsFile> = None;

    for i in 0..wl_config.measure_runs {
        let tmp = tempfile::tempdir().unwrap();
        let output_dir = tmp.path().to_str().unwrap();
        let wb = workload_bin.to_str().unwrap();

        match run_measured(tracer_bin, &["spawn", wb, "--output", output_dir]) {
            Ok((m, stderr)) => {
                traced_walls.push(m.wall_ms);
                traced_rss.push(m.rss_kb);
                if let Some(hp) = parse_hotpath_ms(&stderr) {
                    traced_hotpaths.push(hp);
                }
                trace_sizes.push(dir_size(tmp.path()));
                if let Some(ts) = parse_tracer_stats(tmp.path()) {
                    last_tracer_stats = Some(ts);
                }
            }
            Err(e) => {
                eprintln!("    Traced run {} failed: {}", i, e);
            }
        }
    }

    let (events_captured, hooks_installed, fallback_events, estimated_fn_calls) =
        if let Some(ref ts) = last_tracer_stats {
            let est = ts.events_captured / 3;
            (ts.events_captured, ts.hooks_installed, ts.fallback_events, est)
        } else {
            (0, 0, 0, 0)
        };

    if baseline_walls.is_empty() || traced_walls.is_empty() {
        return LanguageResult {
            total_calls,
            baseline_median_ms: 0.0,
            traced_median_ms: 0.0,
            overhead_ratio: 0.0,
            frida_startup_ms: 0.0,
            per_call_ns: 0.0,
            memory_overhead_kb: 0,
            baseline_rss_kb: 0,
            traced_rss_kb: 0,
            trace_bytes: 0,
            events_captured,
            hooks_installed,
            fallback_events,
            estimated_fn_calls,
            threshold_pass: false,
            failures: vec!["Insufficient measurements".to_string()],
        };
    }

    let baseline_median = median_f64(&mut baseline_walls);
    let traced_median = median_f64(&mut traced_walls);
    let baseline_rss_med = median_u64(&mut baseline_rss);
    let traced_rss_med = median_u64(&mut traced_rss);
    let trace_bytes_med = median_u64(&mut trace_sizes);

    let overhead_ratio = if baseline_median > 0.0 {
        traced_median / baseline_median
    } else {
        f64::INFINITY
    };

    let total_overhead_ms = (traced_median - baseline_median).max(0.0);

    let (frida_startup_ms, per_call_ns) = if !baseline_hotpaths.is_empty()
        && !traced_hotpaths.is_empty()
        && total_calls > 0
    {
        let baseline_hp_median = median_f64(&mut baseline_hotpaths);
        let traced_hp_median = median_f64(&mut traced_hotpaths);
        let hotpath_overhead_ms = (traced_hp_median - baseline_hp_median).max(0.0);
        let startup = (total_overhead_ms - hotpath_overhead_ms).max(0.0);
        let ns_per_call = (hotpath_overhead_ms * 1_000_000.0) / total_calls as f64;
        (startup, ns_per_call)
    } else {
        let ns_per_call = if total_calls > 0 {
            (total_overhead_ms * 1_000_000.0) / total_calls as f64
        } else {
            0.0
        };
        (0.0, ns_per_call)
    };

    let memory_overhead_kb = traced_rss_med as i64 - baseline_rss_med as i64;

    let (max_ns, max_mem_kb, max_trace_mb) = if is_ci {
        if let Some(ci) = ci_overrides {
            (
                threshold.max_per_call_ns * ci.per_call_ns_multiplier,
                (threshold.max_memory_overhead_kb as f64 * ci.memory_multiplier) as u64,
                threshold.max_trace_bytes_mb,
            )
        } else {
            (
                threshold.max_per_call_ns,
                threshold.max_memory_overhead_kb,
                threshold.max_trace_bytes_mb,
            )
        }
    } else {
        (
            threshold.max_per_call_ns,
            threshold.max_memory_overhead_kb,
            threshold.max_trace_bytes_mb,
        )
    };

    let mut failures = Vec::new();

    if per_call_ns > max_ns {
        failures.push(format!("per_call_ns {:.0} > {:.0}", per_call_ns, max_ns));
    }
    if memory_overhead_kb > 0 && memory_overhead_kb as u64 > max_mem_kb {
        failures.push(format!(
            "memory_overhead {}KB > {}KB",
            memory_overhead_kb, max_mem_kb
        ));
    }
    let trace_mb = trace_bytes_med / (1024 * 1024);
    if trace_mb > max_trace_mb {
        failures.push(format!(
            "trace_size {}MB > {}MB",
            trace_mb, max_trace_mb
        ));
    }

    LanguageResult {
        total_calls,
        baseline_median_ms: baseline_median,
        traced_median_ms: traced_median,
        overhead_ratio,
        frida_startup_ms,
        per_call_ns,
        memory_overhead_kb,
        baseline_rss_kb: baseline_rss_med,
        traced_rss_kb: traced_rss_med,
        trace_bytes: trace_bytes_med,
        events_captured,
        hooks_installed,
        fallback_events,
        estimated_fn_calls,
        threshold_pass: failures.is_empty(),
        failures,
    }
}

// ========================== Config Loading ==========================

fn load_config() -> Config {
    let config_path = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("bench_thresholds.toml");
    let content = fs::read_to_string(&config_path)
        .unwrap_or_else(|e| panic!("Failed to read {}: {}", config_path.display(), e));
    toml::from_str(&content)
        .unwrap_or_else(|e| panic!("Failed to parse {}: {}", config_path.display(), e))
}

fn run_bench_for_config(lang: &str, config: &str) -> Option<LanguageResult> {
    let project_root = find_project_root();
    let cfg = load_config();
    let platform = detect_platform();
    let is_ci = env::var("ADA_BENCH_CI").is_ok() || env::var("CI").is_ok();

    let workload_bin = match find_workload_binary(&project_root, lang, config) {
        Some(p) => p,
        None => {
            eprintln!(
                "  [{}/{}] Workload binary not found, skipping",
                lang_display_name(lang),
                config_display_name(config)
            );
            return None;
        }
    };

    let tracer_bin = match find_tracer(&project_root) {
        Some(p) => p,
        None => {
            panic!(
                "tracer binary not found. Run `cargo build --release -p tracer_backend` first."
            );
        }
    };

    ensure_agent_rpath(&project_root);

    let threshold = match cfg
        .thresholds
        .get(platform)
        .and_then(|p| p.get(lang))
        .and_then(|l| l.get(config))
    {
        Some(t) => t,
        None => {
            eprintln!(
                "  [{}/{}] No threshold for {}.{}.{}, skipping",
                lang_display_name(lang),
                config_display_name(config),
                platform,
                lang,
                config
            );
            return None;
        }
    };

    let ci_overrides = cfg.ci_overrides.as_ref();

    Some(bench_config(
        lang,
        config,
        &workload_bin,
        &tracer_bin,
        &cfg.workload,
        threshold,
        is_ci,
        ci_overrides,
    ))
}

// ========================== Display ==========================

fn print_config_table(lang: &str, config: &str, result: &LanguageResult, threshold: &ThresholdConfig) {
    let label = format!(
        "{}/{}",
        lang_display_name(lang),
        config_display_name(config)
    );
    let total_calls_k = result.total_calls as f64 / 1000.0;
    println!("  [{}] {:.0}K calls", label, total_calls_k);
    println!(
        "  {:<22}  {:>12}  {:>12}   {:<6}",
        "Metric", "Value", "Threshold", "Status"
    );
    println!(
        "  {:<22}  {:>12}  {:>12}   {:<6}",
        "\u{2500}".repeat(22),
        "\u{2500}".repeat(12),
        "\u{2500}".repeat(12),
        "\u{2500}".repeat(6)
    );

    println!(
        "  {:<22}  {:>11.2}x  {:>12}   {:<6}",
        "Overhead ratio", result.overhead_ratio, "--", "INFO"
    );

    println!(
        "  {:<22}  {:>9.1} ms  {:>12}   {:<6}",
        "Frida startup", result.frida_startup_ms, "--", "INFO"
    );

    let ns_pass = result.per_call_ns <= threshold.max_per_call_ns;
    println!(
        "  {:<22}  {:>9.0} ns  {:>9.0} ns   {:<6}",
        "Per-call overhead",
        result.per_call_ns,
        threshold.max_per_call_ns,
        if ns_pass { "PASS" } else { "FAIL" }
    );

    let mem_mb = result.memory_overhead_kb as f64 / 1024.0;
    let mem_threshold_mb = threshold.max_memory_overhead_kb as f64 / 1024.0;
    let mem_pass = result.memory_overhead_kb <= 0
        || (result.memory_overhead_kb as u64) <= threshold.max_memory_overhead_kb;
    println!(
        "  {:<22}  {:>9.1} MB  {:>9.1} MB   {:<6}",
        "Memory overhead",
        mem_mb,
        mem_threshold_mb,
        if mem_pass { "PASS" } else { "FAIL" }
    );

    let trace_mb = result.trace_bytes as f64 / (1024.0 * 1024.0);
    let trace_pass = (result.trace_bytes / (1024 * 1024)) <= threshold.max_trace_bytes_mb;
    println!(
        "  {:<22}  {:>9.1} MB  {:>8} MB   {:<6}",
        "Trace output",
        trace_mb,
        threshold.max_trace_bytes_mb,
        if trace_pass { "PASS" } else { "FAIL" }
    );

    // Interception stats
    println!(
        "  {:<22}  {:>12}  {:>12}   {:<6}",
        "Events captured",
        result.events_captured,
        "--",
        "INFO"
    );
    println!(
        "  {:<22}  {:>12}  {:>12}   {:<6}",
        "Est. fn calls",
        result.estimated_fn_calls,
        "--",
        "INFO"
    );
    println!(
        "  {:<22}  {:>12}  {:>12}   {:<6}",
        "Hooks installed",
        result.hooks_installed,
        "--",
        "INFO"
    );
    if result.fallback_events > 0 {
        println!(
            "  {:<22}  {:>12}  {:>12}   {:<6}",
            "Fallback events",
            result.fallback_events,
            "--",
            "WARN"
        );
    }

    println!();
}

// ========================== Per-config tests (20 total) ==========================

macro_rules! bench_test {
    ($name:ident, $lang:expr, $config:expr) => {
        #[test]
        #[ignore]
        fn $name() {
            if let Some(result) = run_bench_for_config($lang, $config) {
                let cfg = load_config();
                let platform = detect_platform();
                if let Some(threshold) = cfg
                    .thresholds
                    .get(platform)
                    .and_then(|p| p.get($lang))
                    .and_then(|l| l.get($config))
                {
                    print_config_table($lang, $config, &result, threshold);
                }
                assert!(
                    result.threshold_pass,
                    "{}/{} benchmark failed: {:?}",
                    $lang,
                    $config,
                    result.failures
                );
            }
        }
    };
}

// C configs
bench_test!(bench_c_cli_export_sym, "c", "cli_export_sym");
bench_test!(bench_c_cli_no_export_sym, "c", "cli_no_export_sym");
bench_test!(bench_c_app_debug, "c", "app_debug");
bench_test!(bench_c_app_debug_dylib, "c", "app_debug_dylib");
bench_test!(bench_c_app_release, "c", "app_release");

// C++ configs
bench_test!(bench_cpp_cli_export_sym, "cpp", "cli_export_sym");
bench_test!(bench_cpp_cli_no_export_sym, "cpp", "cli_no_export_sym");
bench_test!(bench_cpp_app_debug, "cpp", "app_debug");
bench_test!(bench_cpp_app_debug_dylib, "cpp", "app_debug_dylib");
bench_test!(bench_cpp_app_release, "cpp", "app_release");

// ObjC configs
bench_test!(bench_objc_cli_export_sym, "objc", "cli_export_sym");
bench_test!(bench_objc_cli_no_export_sym, "objc", "cli_no_export_sym");
bench_test!(bench_objc_app_debug, "objc", "app_debug");
bench_test!(bench_objc_app_debug_dylib, "objc", "app_debug_dylib");
bench_test!(bench_objc_app_release, "objc", "app_release");

// Swift configs
bench_test!(bench_swift_cli_export_sym, "swift", "cli_export_sym");
bench_test!(bench_swift_cli_no_export_sym, "swift", "cli_no_export_sym");
bench_test!(bench_swift_app_debug, "swift", "app_debug");
bench_test!(bench_swift_app_debug_dylib, "swift", "app_debug_dylib");
bench_test!(bench_swift_app_release, "swift", "app_release");

// ========================== Combined verdict ==========================

#[test]
#[ignore]
fn bench_combined_verdict() {
    let project_root = find_project_root();
    let cfg = load_config();
    let platform = detect_platform();
    let arch = detect_arch();
    let is_ci = env::var("ADA_BENCH_CI").is_ok() || env::var("CI").is_ok();

    let tracer_bin = match find_tracer(&project_root) {
        Some(p) => p,
        None => {
            panic!("tracer binary not found. Run `cargo build --release` first.");
        }
    };

    ensure_agent_rpath(&project_root);

    let languages = ["c", "cpp", "objc", "swift"];
    let configs = [
        "cli_export_sym",
        "cli_no_export_sym",
        "app_debug",
        "app_debug_dylib",
        "app_release",
    ];

    let mut all_results: HashMap<String, LanguageResult> = HashMap::new();
    let mut all_failures: Vec<String> = Vec::new();

    println!("\n=== ADA E2E Benchmark Results ===");
    println!("Platform: {}-{}", arch, platform);
    println!("Matrix: {} languages x {} configs = {} benchmarks\n", languages.len(), configs.len(), languages.len() * configs.len());

    for lang in &languages {
        for config in &configs {
            let result_key = format!("{}_{}", lang, config);
            let label = format!(
                "{}/{}",
                lang_display_name(lang),
                config_display_name(config)
            );

            let workload_bin = match find_workload_binary(&project_root, lang, config) {
                Some(p) => p,
                None => {
                    println!("  [{}] Skipped (binary not found)\n", label);
                    continue;
                }
            };

            let threshold = match cfg
                .thresholds
                .get(platform)
                .and_then(|p| p.get(*lang))
                .and_then(|l| l.get(*config))
            {
                Some(t) => t,
                None => {
                    println!(
                        "  [{}] Skipped (no threshold for {}.{}.{})\n",
                        label, platform, lang, config
                    );
                    continue;
                }
            };

            let ci_overrides = cfg.ci_overrides.as_ref();
            let result = bench_config(
                lang,
                config,
                &workload_bin,
                &tracer_bin,
                &cfg.workload,
                threshold,
                is_ci,
                ci_overrides,
            );

            print_config_table(lang, config, &result, threshold);

            if !result.threshold_pass {
                for f in &result.failures {
                    all_failures.push(format!("{}: {}", label, f));
                }
            }

            all_results.insert(result_key, result);
        }
    }

    let configs_tested = all_results.len();
    let configs_passed = all_results.values().filter(|r| r.threshold_pass).count();
    let pass = all_failures.is_empty();

    let verdict = Verdict {
        pass,
        failures: all_failures.clone(),
        configs_tested,
    };

    let mut platform_results = HashMap::new();
    platform_results.insert(platform.to_string(), all_results);

    let bench_results = BenchResults {
        timestamp: chrono::Utc::now().to_rfc3339(),
        git_sha: git_sha(),
        platform: platform.to_string(),
        arch,
        results: platform_results,
        verdict,
    };

    let results_path = project_root.join("target/bench_e2e_results.json");
    let json = serde_json::to_string_pretty(&bench_results).unwrap();
    fs::write(&results_path, &json).unwrap();

    println!(
        "VERDICT: {} ({}/{} configs passed)",
        if pass { "PASS" } else { "FAIL" },
        configs_passed,
        configs_tested
    );
    println!("\nResults written to: {}", results_path.display());

    assert!(pass, "Benchmark failures: {:?}", all_failures);
}

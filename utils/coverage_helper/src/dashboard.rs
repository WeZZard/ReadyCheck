//! HTML Dashboard generator for coverage reports

use anyhow::{Context, Result};
use std::collections::HashMap;
use std::fs;
use std::path::Path;
use std::process::Command;

/// Coverage metrics for a component
#[derive(Debug, Default, Clone)]
pub struct ComponentMetrics {
    pub line_coverage: f64,
    pub function_coverage: f64,
    pub branch_coverage: f64,
    pub lines_covered: usize,
    pub lines_total: usize,
}

/// Generate the HTML dashboard
pub fn generate_dashboard(
    workspace: &Path,
    report_dir: &Path,
    merged_lcov: &Path,
) -> Result<()> {
    println!("\nGenerating HTML dashboard...");
    
    // Ensure report directory exists
    fs::create_dir_all(report_dir)?;
    
    // Parse LCOV file for metrics
    let metrics = parse_lcov_metrics(merged_lcov)?;
    
    // Get git information
    let commit = get_git_commit()?;
    let branch = get_git_branch()?;
    let timestamp = chrono::Local::now().format("%Y-%m-%d %H:%M:%S").to_string();
    
    // Check diff-cover results if available
    let changed_lines_metrics = get_diff_cover_metrics(report_dir)?;
    
    // Read template
    let template_path = workspace.join("utils/coverage_helper/dashboard_template.html");
    let template = if template_path.exists() {
        fs::read_to_string(&template_path)?
    } else {
        // Use embedded template if file doesn't exist
        include_str!("../dashboard_template.html").to_string()
    };
    
    // Replace placeholders
    let html = replace_placeholders(
        template,
        &metrics,
        &changed_lines_metrics,
        &commit,
        &branch,
        &timestamp,
    );
    
    // Write dashboard
    let dashboard_path = report_dir.join("index.html");
    fs::write(&dashboard_path, html)?;
    println!("  Dashboard generated: {}", dashboard_path.display());
    
    // Generate full HTML report using genhtml if available
    if which::which("genhtml").is_ok() {
        generate_full_html_report(merged_lcov, report_dir)?;
    }
    
    Ok(())
}

/// Parse LCOV file for coverage metrics
fn parse_lcov_metrics(lcov_path: &Path) -> Result<HashMap<String, ComponentMetrics>> {
    let mut metrics = HashMap::new();
    
    if !lcov_path.exists() {
        return Ok(metrics);
    }
    
    let content = fs::read_to_string(lcov_path)?;
    let mut current_file = String::new();
    let mut component_data: HashMap<String, ComponentMetrics> = HashMap::new();
    
    // Initialize component metrics
    component_data.insert("tracer".to_string(), ComponentMetrics::default());
    component_data.insert("tracer_backend".to_string(), ComponentMetrics::default());
    component_data.insert("query_engine".to_string(), ComponentMetrics::default());
    component_data.insert("mcp_server".to_string(), ComponentMetrics::default());
    component_data.insert("total".to_string(), ComponentMetrics::default());
    
    for line in content.lines() {
        if line.starts_with("SF:") {
            current_file = line[3..].to_string();
        } else if line.starts_with("DA:") {
            // Line coverage data
            let parts: Vec<&str> = line[3..].split(',').collect();
            if parts.len() == 2 {
                let component = detect_component(&current_file);
                let is_covered = parts[1] != "0";
                
                // Update component metrics
                component_data.entry(component.clone())
                    .and_modify(|m| {
                        m.lines_total += 1;
                        if is_covered {
                            m.lines_covered += 1;
                        }
                    });
                
                // Update total metrics
                component_data.entry("total".to_string())
                    .and_modify(|m| {
                        m.lines_total += 1;
                        if is_covered {
                            m.lines_covered += 1;
                        }
                    });
            }
        }
    }
    
    // Calculate percentages
    for (_, metrics) in component_data.iter_mut() {
        if metrics.lines_total > 0 {
            metrics.line_coverage = 
                (metrics.lines_covered as f64 / metrics.lines_total as f64) * 100.0;
        }
    }
    
    Ok(component_data)
}

/// Detect which component a file belongs to
fn detect_component(file_path: &str) -> String {
    if file_path.contains("tracer_backend") {
        "tracer_backend".to_string()
    } else if file_path.contains("tracer") {
        "tracer".to_string()
    } else if file_path.contains("query_engine") {
        "query_engine".to_string()
    } else if file_path.contains("mcp_server") {
        "mcp_server".to_string()
    } else {
        "other".to_string()
    }
}

/// Get diff-cover metrics if available
fn get_diff_cover_metrics(_report_dir: &Path) -> Result<HashMap<String, String>> {
    let mut metrics = HashMap::new();
    
    // Set defaults
    metrics.insert("CHANGED_LINES_COVERAGE".to_string(), "N/A".to_string());
    metrics.insert("CHANGED_LINES_STATUS".to_string(), "warning".to_string());
    metrics.insert("CHANGED_LINES_COVERED".to_string(), "0".to_string());
    metrics.insert("CHANGED_LINES_TOTAL".to_string(), "0".to_string());
    metrics.insert("CHANGED_LINES_RESULT".to_string(), "No Changes".to_string());
    
    // TODO: Parse diff-cover output if available
    
    Ok(metrics)
}

/// Get current git commit
fn get_git_commit() -> Result<String> {
    let output = Command::new("git")
        .args(&["rev-parse", "--short", "HEAD"])
        .output()
        .context("Failed to get git commit")?;
    
    Ok(String::from_utf8_lossy(&output.stdout).trim().to_string())
}

/// Get current git branch
fn get_git_branch() -> Result<String> {
    let output = Command::new("git")
        .args(&["rev-parse", "--abbrev-ref", "HEAD"])
        .output()
        .context("Failed to get git branch")?;
    
    Ok(String::from_utf8_lossy(&output.stdout).trim().to_string())
}

/// Replace template placeholders with actual values
fn replace_placeholders(
    mut template: String,
    metrics: &HashMap<String, ComponentMetrics>,
    changed_lines: &HashMap<String, String>,
    commit: &str,
    branch: &str,
    timestamp: &str,
) -> String {
    // Git info
    template = template.replace("{{TIMESTAMP}}", timestamp);
    template = template.replace("{{COMMIT}}", commit);
    template = template.replace("{{BRANCH}}", branch);
    
    // Changed lines metrics
    for (key, value) in changed_lines {
        template = template.replace(&format!("{{{{{}}}}}", key), value);
    }
    
    // Total coverage
    if let Some(total) = metrics.get("total") {
        let status = get_status(total.line_coverage);
        template = template.replace("{{TOTAL_COVERAGE}}", &format!("{:.1}", total.line_coverage));
        template = template.replace("{{TOTAL_STATUS}}", &status);
        template = template.replace("{{TOTAL_LINES_COVERED}}", &total.lines_covered.to_string());
        template = template.replace("{{TOTAL_LINES}}", &total.lines_total.to_string());
        template = template.replace("{{FUNC_COVERAGE}}", "N/A");
        template = template.replace("{{FUNC_STATUS}}", "warning");
        template = template.replace("{{BRANCH_COVERAGE}}", "N/A");
        template = template.replace("{{BRANCH_STATUS}}", "warning");
    }
    
    // Component metrics
    for component in &["tracer", "tracer_backend", "query_engine", "mcp_server"] {
        let prefix = component.to_uppercase().replace("_", "_");
        let comp_metrics = metrics.get(*component).cloned().unwrap_or_default();
        let status = get_status(comp_metrics.line_coverage);
        let health = get_health(&comp_metrics);
        
        // Special handling for component prefixes
        let template_prefix = match *component {
            "tracer_backend" => "BACKEND",
            "query_engine" => "QUERY",
            "mcp_server" => "MCP",
            _ => &prefix,
        };
        
        template = template.replace(
            &format!("{{{{{}_LINE_COV}}}}", template_prefix),
            &format!("{:.1}", comp_metrics.line_coverage),
        );
        template = template.replace(
            &format!("{{{{{}_LINE_STATUS}}}}", template_prefix),
            &status,
        );
        template = template.replace(
            &format!("{{{{{}_FUNC_COV}}}}", template_prefix),
            "N/A",
        );
        template = template.replace(
            &format!("{{{{{}_BRANCH_COV}}}}", template_prefix),
            "N/A",
        );
        template = template.replace(
            &format!("{{{{{}_STATUS}}}}", template_prefix),
            &status,
        );
        template = template.replace(
            &format!("{{{{{}_HEALTH}}}}", template_prefix),
            &health,
        );
    }
    
    template
}

/// Get status class based on coverage percentage
fn get_status(coverage: f64) -> String {
    if coverage >= 80.0 {
        "pass".to_string()
    } else if coverage >= 60.0 {
        "warning".to_string()
    } else {
        "fail".to_string()
    }
}

/// Get health status for a component
fn get_health(metrics: &ComponentMetrics) -> String {
    if metrics.line_coverage >= 80.0 {
        "Healthy".to_string()
    } else if metrics.line_coverage >= 60.0 {
        "Needs Work".to_string()
    } else {
        "Critical".to_string()
    }
}

/// Generate full HTML report using genhtml
fn generate_full_html_report(lcov_path: &Path, report_dir: &Path) -> Result<()> {
    println!("  Generating full HTML report with genhtml...");
    
    let full_report_dir = report_dir.join("full");
    fs::create_dir_all(&full_report_dir)?;
    
    let output = Command::new("genhtml")
        .args(&[
            lcov_path.to_str().unwrap(),
            "--output-directory",
            full_report_dir.to_str().unwrap(),
            "--title",
            "ADA Coverage Report",
            "--legend",
            "--show-details",
            "--demangle-cpp",
            "--ignore-errors",
            "deprecated",
        ])
        .output()
        .context("Failed to run genhtml")?;
    
    if output.status.success() {
        println!("  Full report generated: {}/index.html", full_report_dir.display());
    } else {
        let stderr = String::from_utf8_lossy(&output.stderr);
        println!("  Warning: genhtml failed: {}", stderr);
    }
    
    Ok(())
}
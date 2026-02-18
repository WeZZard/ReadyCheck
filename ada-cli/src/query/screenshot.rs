//! Screenshot extraction from screen recordings
//!
//! Wraps FFmpeg to extract frames at specific timestamps.

use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;

use anyhow::{bail, Context, Result};
use serde::Serialize;

use super::bundle::Bundle;
use super::output::OutputFormat;

/// Screenshot extraction result
#[derive(Debug, Clone, Serialize)]
pub struct ScreenshotResult {
    /// Time in seconds where frame was extracted
    pub time_sec: f64,
    /// Path to the extracted screenshot
    pub path: PathBuf,
    /// Image width (if available)
    #[serde(skip_serializing_if = "Option::is_none")]
    pub width: Option<u32>,
    /// Image height (if available)
    #[serde(skip_serializing_if = "Option::is_none")]
    pub height: Option<u32>,
}

/// Extract a screenshot at the specified time
// LCOV_EXCL_START - Requires real bundle with screen recording and ffmpeg
pub fn extract_screenshot(
    bundle: &Bundle,
    time_sec: f64,
    output_path: Option<&Path>,
) -> Result<ScreenshotResult> {
    let screen_path = bundle.screen_path().ok_or_else(|| {
        anyhow::anyhow!("Session has no screen recording. Use --screen flag during capture.")
    })?;

    if !screen_path.exists() {
        bail!(
            "Screen recording not found at {:?}. Use --screen flag during capture.",
            screen_path
        );
    }

    // Resolve ffmpeg and ffprobe via binary_resolver
    let ffmpeg_path = ada_cli::binary_resolver::resolve(ada_cli::binary_resolver::Tool::Ffmpeg)
        .map_err(|_| anyhow::anyhow!("FFmpeg not available. Run: ./utils/init_media_tools.sh"))?;

    // Determine output path
    let output = match output_path {
        Some(p) => p.to_path_buf(),
        None => {
            let filename = format!("screenshot_{:.0}s.png", time_sec);
            bundle.path.join(filename)
        }
    };

    // Ensure output directory exists
    if let Some(parent) = output.parent() {
        fs::create_dir_all(parent)?;
    }

    // Run ffmpeg to extract frame
    // -ss before -i for fast seeking
    let ffmpeg_output = Command::new(&ffmpeg_path)
        .arg("-y") // Overwrite output file
        .arg("-ss")
        .arg(format!("{:.3}", time_sec))
        .arg("-i")
        .arg(&screen_path)
        .arg("-vframes")
        .arg("1")
        .arg("-q:v")
        .arg("2") // High quality
        .arg(&output)
        .output()
        .with_context(|| "Failed to run ffmpeg")?;

    if !ffmpeg_output.status.success() {
        let stderr = String::from_utf8_lossy(&ffmpeg_output.stderr);
        bail!("FFmpeg failed: {}", stderr);
    }

    if !output.exists() {
        bail!("FFmpeg did not produce output file. Time may be out of range.");
    }

    // Try to get image dimensions using ffprobe
    let (width, height) = get_image_dimensions(&output).unwrap_or((None, None));

    Ok(ScreenshotResult {
        time_sec,
        path: output,
        width,
        height,
    })
}

/// Get image dimensions using ffprobe
fn get_image_dimensions(path: &Path) -> Result<(Option<u32>, Option<u32>)> {
    let ffprobe_path = ada_cli::binary_resolver::resolve(ada_cli::binary_resolver::Tool::Ffprobe)
        .unwrap_or_else(|_| std::path::PathBuf::from("ffprobe"));
    let output = Command::new(&ffprobe_path)
        .arg("-v")
        .arg("error")
        .arg("-select_streams")
        .arg("v:0")
        .arg("-show_entries")
        .arg("stream=width,height")
        .arg("-of")
        .arg("csv=s=x:p=0")
        .arg(path)
        .output()?;

    if output.status.success() {
        let stdout = String::from_utf8_lossy(&output.stdout);
        let parts: Vec<&str> = stdout.trim().split('x').collect();
        if parts.len() == 2 {
            let width = parts[0].parse().ok();
            let height = parts[1].parse().ok();
            return Ok((width, height));
        }
    }

    Ok((None, None))
}
// LCOV_EXCL_STOP

/// Format screenshot result
// LCOV_EXCL_START - Integration tested via CLI
pub fn format_screenshot(result: &ScreenshotResult, format: OutputFormat) -> String {
    match format {
        OutputFormat::Text | OutputFormat::Line => format_screenshot_text(result),
        OutputFormat::Json => format_screenshot_json(result),
    }
}

fn format_screenshot_text(result: &ScreenshotResult) -> String {
    let mut output = String::new();
    output.push_str(&format!("Screenshot extracted at {:.1}s\n", result.time_sec));
    output.push_str(&format!("Path: {}\n", result.path.display()));
    if let (Some(w), Some(h)) = (result.width, result.height) {
        output.push_str(&format!("Dimensions: {}x{}\n", w, h));
    }
    output
}

fn format_screenshot_json(result: &ScreenshotResult) -> String {
    serde_json::to_string_pretty(result).unwrap_or_else(|_| "{}".to_string())
}
// LCOV_EXCL_STOP

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_screenshot_result__serialize__then_valid_json() {
        let result = ScreenshotResult {
            time_sec: 30.0,
            path: PathBuf::from("/tmp/screenshot.png"),
            width: Some(1920),
            height: Some(1080),
        };
        let json = serde_json::to_string(&result).unwrap();
        assert!(json.contains("1920"));
        assert!(json.contains("1080"));
    }

    #[test]
    fn test_format_screenshot_text__then_contains_info() {
        let result = ScreenshotResult {
            time_sec: 30.0,
            path: PathBuf::from("/tmp/screenshot.png"),
            width: Some(1920),
            height: Some(1080),
        };
        let output = format_screenshot(&result, OutputFormat::Text);
        assert!(output.contains("30.0s"));
        assert!(output.contains("/tmp/screenshot.png"));
        assert!(output.contains("1920x1080"));
    }

    #[test]
    fn test_format_screenshot_text__no_dimensions__then_omits_line() {
        let result = ScreenshotResult {
            time_sec: 15.0,
            path: PathBuf::from("/tmp/frame.png"),
            width: None,
            height: None,
        };
        let output = format_screenshot(&result, OutputFormat::Text);
        assert!(output.contains("15.0s"));
        assert!(!output.contains("Dimensions"));
    }
}

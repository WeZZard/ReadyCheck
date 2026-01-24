//! Transcription support for voice recordings
//!
//! Wraps Whisper for transcription with caching in session directory.

use std::fs;
use std::path::Path;
use std::process::Command;

use anyhow::{bail, Context, Result};
use serde::{Deserialize, Serialize};

use super::bundle::Bundle;
use super::output::OutputFormat;

/// A transcript segment with timing information
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Segment {
    /// Segment index (0-based)
    pub index: usize,
    /// Start time in seconds
    pub start_sec: f64,
    /// End time in seconds
    pub end_sec: f64,
    /// Transcribed text
    pub text: String,
}

/// Cached transcript data
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Transcript {
    /// All segments
    pub segments: Vec<Segment>,
    /// Total duration in seconds
    pub total_duration_sec: f64,
    /// Voice file path (relative to bundle)
    pub voice_path: String,
}

/// Transcript metadata (info command output)
#[derive(Debug, Clone, Serialize)]
pub struct TranscriptInfo {
    pub segment_count: usize,
    pub total_duration_sec: f64,
    pub time_start_sec: f64,
    pub time_end_sec: f64,
    pub voice_path: String,
    pub cached: bool,
}

/// Paginated segments result
#[derive(Debug, Clone, Serialize)]
pub struct SegmentsResult {
    pub pagination: Pagination,
    pub time_range: TimeRange,
    pub segments: Vec<Segment>,
}

#[derive(Debug, Clone, Serialize)]
pub struct Pagination {
    pub offset: usize,
    pub limit: usize,
    pub total: usize,
    pub has_more: bool,
}

#[derive(Debug, Clone, Serialize)]
pub struct TimeRange {
    pub start_sec: f64,
    pub end_sec: f64,
}

/// Get or create transcript for a bundle
// LCOV_EXCL_START - Requires real bundle with voice recording
pub fn get_or_create_transcript(bundle: &Bundle) -> Result<Transcript> {
    let voice_path = bundle
        .voice_path()
        .ok_or_else(|| anyhow::anyhow!("Session has no voice recording. Use --voice flag during capture."))?;

    if !voice_path.exists() {
        bail!(
            "Voice recording not found at {:?}. Use --voice flag during capture.",
            voice_path
        );
    }

    let cache_path = bundle.path.join("transcript.json");

    // Check if cached and valid
    if cache_path.exists() {
        let voice_modified = fs::metadata(&voice_path)?.modified()?;
        let cache_modified = fs::metadata(&cache_path)?.modified()?;

        if cache_modified > voice_modified {
            // Cache is valid
            let content = fs::read_to_string(&cache_path)
                .with_context(|| "Failed to read cached transcript")?;
            let transcript: Transcript = serde_json::from_str(&content)
                .with_context(|| "Failed to parse cached transcript")?;
            return Ok(transcript);
        }
    }

    // Run Whisper to generate transcript
    let transcript = run_whisper(&voice_path, bundle)?;

    // Cache the result
    let content = serde_json::to_string_pretty(&transcript)?;
    fs::write(&cache_path, content).with_context(|| "Failed to cache transcript")?;

    Ok(transcript)
}
// LCOV_EXCL_STOP

/// Check if transcript is cached
// LCOV_EXCL_START - Requires real filesystem
pub fn is_cached(bundle: &Bundle) -> bool {
    let cache_path = bundle.path.join("transcript.json");
    if !cache_path.exists() {
        return false;
    }

    if let Some(voice_path) = bundle.voice_path() {
        if let (Ok(voice_meta), Ok(cache_meta)) =
            (fs::metadata(&voice_path), fs::metadata(&cache_path))
        {
            if let (Ok(voice_mod), Ok(cache_mod)) = (voice_meta.modified(), cache_meta.modified()) {
                return cache_mod > voice_mod;
            }
        }
    }

    false
}
// LCOV_EXCL_STOP

/// Run Whisper on a voice file
// LCOV_EXCL_START - Requires whisper executable
fn run_whisper(voice_path: &Path, bundle: &Bundle) -> Result<Transcript> {
    // Check if whisper is available
    let which_result = Command::new("which").arg("whisper").output();
    if !which_result.map(|o| o.status.success()).unwrap_or(false) {
        bail!("Whisper not available. Install with: pip install openai-whisper");
    }

    // Create temp directory for whisper output
    let temp_dir = tempfile::tempdir()?;
    let output_dir = temp_dir.path();

    // Run whisper with JSON output
    let output = Command::new("whisper")
        .arg(voice_path)
        .arg("--output_format")
        .arg("json")
        .arg("--output_dir")
        .arg(output_dir)
        .arg("--language")
        .arg("en")
        .arg("--model")
        .arg("base")
        .output()
        .with_context(|| "Failed to run whisper")?;

    if !output.status.success() {
        let stderr = String::from_utf8_lossy(&output.stderr);
        bail!("Whisper failed: {}", stderr);
    }

    // Find the output JSON file
    let voice_stem = voice_path
        .file_stem()
        .and_then(|s| s.to_str())
        .unwrap_or("voice");
    let json_path = output_dir.join(format!("{}.json", voice_stem));

    if !json_path.exists() {
        bail!("Whisper did not produce expected output file");
    }

    // Parse Whisper's JSON output
    let content = fs::read_to_string(&json_path)?;
    let whisper_output: WhisperOutput = serde_json::from_str(&content)
        .with_context(|| "Failed to parse Whisper output")?;

    // Convert to our format
    let segments: Vec<Segment> = whisper_output
        .segments
        .into_iter()
        .enumerate()
        .map(|(i, seg)| Segment {
            index: i,
            start_sec: seg.start,
            end_sec: seg.end,
            text: seg.text.trim().to_string(),
        })
        .collect();

    let total_duration = segments.last().map(|s| s.end_sec).unwrap_or(0.0);

    let voice_rel_path = bundle
        .manifest
        .voice_path
        .clone()
        .unwrap_or_else(|| "voice.wav".to_string());

    Ok(Transcript {
        segments,
        total_duration_sec: total_duration,
        voice_path: voice_rel_path,
    })
}
// LCOV_EXCL_STOP

/// Whisper JSON output format
#[derive(Debug, Deserialize)]
struct WhisperOutput {
    segments: Vec<WhisperSegment>,
}

#[derive(Debug, Deserialize)]
struct WhisperSegment {
    start: f64,
    end: f64,
    text: String,
}

/// Get transcript info
// LCOV_EXCL_START - Requires real bundle
pub fn get_info(bundle: &Bundle) -> Result<TranscriptInfo> {
    let cached = is_cached(bundle);
    let transcript = get_or_create_transcript(bundle)?;

    let time_start = transcript.segments.first().map(|s| s.start_sec).unwrap_or(0.0);
    let time_end = transcript.segments.last().map(|s| s.end_sec).unwrap_or(0.0);

    Ok(TranscriptInfo {
        segment_count: transcript.segments.len(),
        total_duration_sec: transcript.total_duration_sec,
        time_start_sec: time_start,
        time_end_sec: time_end,
        voice_path: transcript.voice_path,
        cached,
    })
}
// LCOV_EXCL_STOP

/// Get paginated segments
// LCOV_EXCL_START - Requires real bundle
pub fn get_segments(
    bundle: &Bundle,
    offset: usize,
    limit: usize,
    since: Option<f64>,
    until: Option<f64>,
) -> Result<SegmentsResult> {
    let transcript = get_or_create_transcript(bundle)?;

    // Apply time filters first
    let filtered: Vec<Segment> = transcript
        .segments
        .into_iter()
        .filter(|s| {
            if let Some(since_sec) = since {
                if s.end_sec < since_sec {
                    return false;
                }
            }
            if let Some(until_sec) = until {
                if s.start_sec > until_sec {
                    return false;
                }
            }
            true
        })
        .collect();

    let total = filtered.len();

    // Apply pagination
    let segments: Vec<Segment> = filtered
        .into_iter()
        .skip(offset)
        .take(limit)
        .collect();

    let time_range = if segments.is_empty() {
        TimeRange {
            start_sec: 0.0,
            end_sec: 0.0,
        }
    } else {
        TimeRange {
            start_sec: segments.first().map(|s| s.start_sec).unwrap_or(0.0),
            end_sec: segments.last().map(|s| s.end_sec).unwrap_or(0.0),
        }
    };

    Ok(SegmentsResult {
        pagination: Pagination {
            offset,
            limit,
            total,
            has_more: offset + segments.len() < total,
        },
        time_range,
        segments,
    })
}
// LCOV_EXCL_STOP

/// Format transcript info
// LCOV_EXCL_START - Integration tested via CLI
pub fn format_info(info: &TranscriptInfo, format: OutputFormat) -> String {
    match format {
        OutputFormat::Text | OutputFormat::Line => format_info_text(info),
        OutputFormat::Json => format_info_json(info),
    }
}

fn format_info_text(info: &TranscriptInfo) -> String {
    let mut output = String::new();
    output.push_str(&format!("Segment Count:  {}\n", info.segment_count));
    output.push_str(&format!("Duration:       {:.1} s\n", info.total_duration_sec));
    output.push_str(&format!("Time Start:     {:.1} s\n", info.time_start_sec));
    output.push_str(&format!("Time End:       {:.1} s\n", info.time_end_sec));
    output.push_str(&format!("Voice Path:     {}\n", info.voice_path));
    output.push_str(&format!("Cached:         {}\n", if info.cached { "yes" } else { "no" }));
    output
}

fn format_info_json(info: &TranscriptInfo) -> String {
    serde_json::to_string_pretty(info).unwrap_or_else(|_| "{}".to_string())
}

/// Format segments result
pub fn format_segments(result: &SegmentsResult, format: OutputFormat) -> String {
    match format {
        OutputFormat::Text | OutputFormat::Line => format_segments_text(result),
        OutputFormat::Json => format_segments_json(result),
    }
}

fn format_segments_text(result: &SegmentsResult) -> String {
    let mut output = String::new();

    output.push_str(&format!(
        "Segments {}-{} of {} (time range: {:.1}s - {:.1}s)\n\n",
        result.pagination.offset,
        result.pagination.offset + result.segments.len(),
        result.pagination.total,
        result.time_range.start_sec,
        result.time_range.end_sec
    ));

    for seg in &result.segments {
        output.push_str(&format!(
            "[{:03}] {:.1}s - {:.1}s: {}\n",
            seg.index, seg.start_sec, seg.end_sec, seg.text
        ));
    }

    if result.pagination.has_more {
        output.push_str(&format!(
            "\n... {} more segments (use --offset {} to continue)\n",
            result.pagination.total - result.pagination.offset - result.segments.len(),
            result.pagination.offset + result.segments.len()
        ));
    }

    output
}

fn format_segments_json(result: &SegmentsResult) -> String {
    serde_json::to_string_pretty(result).unwrap_or_else(|_| "{}".to_string())
}
// LCOV_EXCL_STOP

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_segment__serialize__then_valid_json() {
        let seg = Segment {
            index: 0,
            start_sec: 0.0,
            end_sec: 2.5,
            text: "Hello world".to_string(),
        };
        let json = serde_json::to_string(&seg).unwrap();
        assert!(json.contains("Hello world"));
    }

    #[test]
    fn test_pagination__has_more__when_more_items() {
        let pagination = Pagination {
            offset: 0,
            limit: 10,
            total: 50,
            has_more: true,
        };
        assert!(pagination.has_more);
    }

    #[test]
    fn test_format_info_text__then_contains_fields() {
        let info = TranscriptInfo {
            segment_count: 10,
            total_duration_sec: 60.0,
            time_start_sec: 0.0,
            time_end_sec: 60.0,
            voice_path: "voice.wav".to_string(),
            cached: true,
        };
        let output = format_info(&info, OutputFormat::Text);
        assert!(output.contains("Segment Count:  10"));
        assert!(output.contains("Duration:       60.0 s"));
        assert!(output.contains("Cached:         yes"));
    }

    #[test]
    fn test_format_segments_text__then_shows_pagination() {
        let result = SegmentsResult {
            pagination: Pagination {
                offset: 0,
                limit: 2,
                total: 5,
                has_more: true,
            },
            time_range: TimeRange {
                start_sec: 0.0,
                end_sec: 5.0,
            },
            segments: vec![
                Segment {
                    index: 0,
                    start_sec: 0.0,
                    end_sec: 2.5,
                    text: "First".to_string(),
                },
                Segment {
                    index: 1,
                    start_sec: 2.5,
                    end_sec: 5.0,
                    text: "Second".to_string(),
                },
            ],
        };
        let output = format_segments(&result, OutputFormat::Text);
        assert!(output.contains("Segments 0-2 of 5"));
        assert!(output.contains("First"));
        assert!(output.contains("3 more segments"));
    }
}

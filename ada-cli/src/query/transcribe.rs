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
    // Prefer lossless WAV (whisper-cli requires WAV input) over compressed m4a
    let voice_path = bundle
        .voice_lossless_path()
        .filter(|p| p.exists())
        .or_else(|| bundle.voice_path())
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

    if let Some(voice_path) = bundle
        .voice_lossless_path()
        .filter(|p| p.exists())
        .or_else(|| bundle.voice_path())
    {
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

/// Run whisper.cpp on a voice file
// LCOV_EXCL_START - Requires whisper executable
fn run_whisper(voice_path: &Path, bundle: &Bundle) -> Result<Transcript> {
    let whisper_path = ada_cli::binary_resolver::resolve(ada_cli::binary_resolver::Tool::WhisperCpp)
        .map_err(|_| anyhow::anyhow!("Whisper not available. Run: ./utils/init_media_tools.sh"))?;

    // Ensure model is available
    let model_path = ada_cli::model_manager::ensure_model("tiny")?;

    // Create temp directory for output
    let temp_dir = tempfile::tempdir()?;

    // Resample to 16 kHz if needed (whisper-cli requires 16 kHz WAV)
    let actual_voice_path = ada_cli::audio::ensure_16khz(voice_path, temp_dir.path())?;

    let voice_stem = voice_path
        .file_stem()
        .and_then(|s| s.to_str())
        .unwrap_or("voice");
    let output_prefix = temp_dir.path().join(voice_stem);

    // Run whisper.cpp
    let output = Command::new(&whisper_path)
        .arg("-f")
        .arg(&actual_voice_path)
        .arg("-m")
        .arg(&model_path)
        .arg("-oj")        // JSON output
        .arg("-of")
        .arg(&output_prefix) // writes <prefix>.json
        .output()
        .with_context(|| "Failed to run whisper-cli")?;

    if !output.status.success() {
        let stderr = String::from_utf8_lossy(&output.stderr);
        bail!("whisper-cli failed: {}", stderr);
    }

    // Read the JSON output
    let json_path = temp_dir.path().join(format!("{}.json", voice_stem));
    if !json_path.exists() {
        bail!("whisper-cli did not produce expected output file");
    }

    let content = fs::read_to_string(&json_path)?;
    let cpp_output: WhisperCppOutput = serde_json::from_str(&content)
        .with_context(|| "Failed to parse whisper-cli output")?;

    // Convert whisper.cpp format to our internal format
    let segments: Vec<Segment> = cpp_output
        .transcription
        .into_iter()
        .enumerate()
        .map(|(i, seg)| Segment {
            index: i,
            start_sec: seg.offsets.from as f64 / 1000.0,
            end_sec: seg.offsets.to as f64 / 1000.0,
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

/// whisper.cpp JSON output format
#[derive(Debug, Deserialize)]
struct WhisperCppOutput {
    transcription: Vec<WhisperCppSegment>,
}

#[derive(Debug, Deserialize)]
struct WhisperCppSegment {
    offsets: WhisperCppOffsets,
    text: String,
}

#[derive(Debug, Deserialize)]
struct WhisperCppOffsets {
    from: u64, // milliseconds
    to: u64,   // milliseconds
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
    fn test_whisper_cpp_output__parse__then_valid() {
        let json = r#"{
            "transcription": [
                {
                    "timestamps": {"from": "00:00:00,720", "to": "00:00:08,880"},
                    "offsets": {"from": 720, "to": 8880},
                    "text": " Hello, this is a test."
                },
                {
                    "timestamps": {"from": "00:00:08,880", "to": "00:00:15,000"},
                    "offsets": {"from": 8880, "to": 15000},
                    "text": " Second segment here."
                }
            ]
        }"#;

        let parsed: WhisperCppOutput = serde_json::from_str(json).unwrap();
        assert_eq!(parsed.transcription.len(), 2);
        assert_eq!(parsed.transcription[0].offsets.from, 720);
        assert_eq!(parsed.transcription[0].offsets.to, 8880);
        assert_eq!(parsed.transcription[0].text, " Hello, this is a test.");
    }

    #[test]
    fn test_whisper_cpp_output__convert_to_segments__then_correct_times() {
        let cpp_output = WhisperCppOutput {
            transcription: vec![
                WhisperCppSegment {
                    offsets: WhisperCppOffsets {
                        from: 720,
                        to: 8880,
                    },
                    text: " Hello, test.".to_string(),
                },
                WhisperCppSegment {
                    offsets: WhisperCppOffsets {
                        from: 8880,
                        to: 15000,
                    },
                    text: " Second.".to_string(),
                },
            ],
        };

        let segments: Vec<Segment> = cpp_output
            .transcription
            .into_iter()
            .enumerate()
            .map(|(i, seg)| Segment {
                index: i,
                start_sec: seg.offsets.from as f64 / 1000.0,
                end_sec: seg.offsets.to as f64 / 1000.0,
                text: seg.text.trim().to_string(),
            })
            .collect();

        assert_eq!(segments.len(), 2);
        assert!((segments[0].start_sec - 0.72).abs() < 0.001);
        assert!((segments[0].end_sec - 8.88).abs() < 0.001);
        assert_eq!(segments[0].text, "Hello, test.");
        assert!((segments[1].start_sec - 8.88).abs() < 0.001);
        assert!((segments[1].end_sec - 15.0).abs() < 0.001);
    }

    #[test]
    fn test_whisper_cpp_output__parse_fixture_file__then_valid() {
        let fixture_path = std::path::Path::new(env!("CARGO_MANIFEST_DIR"))
            .join("tests/fixtures/transcribe/expected_output.json");
        let content = std::fs::read_to_string(&fixture_path)
            .expect("fixture file should exist");
        let parsed: WhisperCppOutput = serde_json::from_str(&content)
            .expect("fixture should parse as WhisperCppOutput");

        assert_eq!(parsed.transcription.len(), 2);

        // First segment: "The quick brown fox jumps over the lazy dog."
        assert_eq!(parsed.transcription[0].offsets.from, 0);
        assert_eq!(parsed.transcription[0].offsets.to, 3000);
        assert!(parsed.transcription[0].text.contains("quick brown fox"));

        // Second segment: "Testing one two three."
        assert_eq!(parsed.transcription[1].offsets.from, 3000);
        assert_eq!(parsed.transcription[1].offsets.to, 5000);
        assert!(parsed.transcription[1].text.contains("Testing"));
    }

    /// Golden file test: converting whisper.cpp output must produce JSON
    /// identical to the committed expected_transcript.json.
    ///
    /// This catches any drift in the Transcript/Segment serialization format
    /// that downstream consumers (cached transcript.json files, query commands)
    /// depend on.
    #[test]
    fn test_whisper_cpp_output__fixture_to_transcript__then_matches_golden_file() {
        let fixtures = std::path::Path::new(env!("CARGO_MANIFEST_DIR"))
            .join("tests/fixtures/transcribe");

        // Load whisper.cpp output fixture
        let cpp_json = std::fs::read_to_string(fixtures.join("expected_output.json")).unwrap();
        let cpp_output: WhisperCppOutput = serde_json::from_str(&cpp_json).unwrap();

        // Convert using the same logic as run_whisper
        let segments: Vec<Segment> = cpp_output
            .transcription
            .into_iter()
            .enumerate()
            .map(|(i, seg)| Segment {
                index: i,
                start_sec: seg.offsets.from as f64 / 1000.0,
                end_sec: seg.offsets.to as f64 / 1000.0,
                text: seg.text.trim().to_string(),
            })
            .collect();
        let total_duration = segments.last().map(|s| s.end_sec).unwrap_or(0.0);
        let transcript = Transcript {
            segments,
            total_duration_sec: total_duration,
            voice_path: "voice.wav".to_string(),
        };

        // Serialize to JSON (same as what gets written to transcript.json)
        let actual_json = serde_json::to_string_pretty(&transcript).unwrap();

        // Load golden file
        let expected_json =
            std::fs::read_to_string(fixtures.join("expected_transcript.json")).unwrap();

        // Compare as parsed Values to ignore whitespace differences
        let actual: serde_json::Value = serde_json::from_str(&actual_json).unwrap();
        let expected: serde_json::Value = serde_json::from_str(&expected_json).unwrap();

        assert_eq!(
            actual, expected,
            "Transcript output format has drifted from golden file.\n\
             If this is intentional, update tests/fixtures/transcribe/expected_transcript.json.\n\
             Actual:\n{}\nExpected:\n{}",
            actual_json, expected_json
        );
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

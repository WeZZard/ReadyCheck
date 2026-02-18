//! Integration test for 48 kHz → 16 kHz resampling before whisper-cli.
//!
//! Requires:
//! - `ffprobe` and `ffmpeg` binaries (bundled or via env override)
//! - `whisper-cli` binary (built via `./utils/init_media_tools.sh`)
//! - `ggml-tiny.bin` model (auto-downloaded by model_manager)
//!
//! Skipped automatically when required tools are not available.

use std::path::Path;
use std::process::Command;

fn ffprobe_available() -> bool {
    ada_cli::binary_resolver::is_available(ada_cli::binary_resolver::Tool::Ffprobe)
}

fn ffmpeg_available() -> bool {
    ada_cli::binary_resolver::is_available(ada_cli::binary_resolver::Tool::Ffmpeg)
}

fn whisper_cli_available() -> bool {
    ada_cli::binary_resolver::is_available(ada_cli::binary_resolver::Tool::WhisperCpp)
}

/// Probe the sample rate of a WAV file via ffprobe.
fn probe_sample_rate(path: &Path) -> u32 {
    let ffprobe = ada_cli::binary_resolver::resolve(ada_cli::binary_resolver::Tool::Ffprobe)
        .expect("ffprobe should resolve");
    let output = Command::new(&ffprobe)
        .args(["-v", "error", "-select_streams", "a:0"])
        .args(["-show_entries", "stream=sample_rate"])
        .args(["-of", "default=noprint_wrappers=1:nokey=1"])
        .arg(path)
        .output()
        .expect("ffprobe should execute");
    let rate_str = String::from_utf8_lossy(&output.stdout);
    rate_str.trim().parse().expect("sample rate should be a number")
}

#[test]
fn ensure_16khz__48khz_input__then_resamples_to_16khz() {
    if !ffprobe_available() || !ffmpeg_available() {
        eprintln!("SKIPPED: ffprobe/ffmpeg not available");
        return;
    }

    let fixture_48k = Path::new(env!("CARGO_MANIFEST_DIR"))
        .join("tests/fixtures/transcribe/test_voice_48k.wav");
    assert!(fixture_48k.exists(), "48 kHz fixture must exist");

    // Confirm fixture is actually 48 kHz
    assert_eq!(probe_sample_rate(&fixture_48k), 48_000);

    let temp_dir = tempfile::tempdir().unwrap();
    let result = ada_cli::audio::ensure_16khz(&fixture_48k, temp_dir.path())
        .expect("ensure_16khz should succeed");

    // Should have created a new file (not returned the original)
    assert_ne!(result, fixture_48k, "should resample, not return original");
    assert!(result.exists(), "resampled file should exist");

    // Verify the output is 16 kHz
    assert_eq!(probe_sample_rate(&result), 16_000);
}

#[test]
fn ensure_16khz__16khz_input__then_returns_original_path() {
    if !ffprobe_available() {
        eprintln!("SKIPPED: ffprobe not available");
        return;
    }

    let fixture_16k = Path::new(env!("CARGO_MANIFEST_DIR"))
        .join("tests/fixtures/transcribe/test_voice.wav");
    assert!(fixture_16k.exists(), "16 kHz fixture must exist");

    let temp_dir = tempfile::tempdir().unwrap();
    let result = ada_cli::audio::ensure_16khz(&fixture_16k, temp_dir.path())
        .expect("ensure_16khz should succeed");

    // Should return the original path unchanged
    assert_eq!(result, fixture_16k, "16 kHz input should pass through unchanged");
}

#[test]
fn transcribe__48khz_fixture__then_whisper_accepts_resampled_audio() {
    if !ffprobe_available() || !ffmpeg_available() || !whisper_cli_available() {
        eprintln!("SKIPPED: ffprobe/ffmpeg/whisper-cli not available");
        return;
    }

    let fixture_48k = Path::new(env!("CARGO_MANIFEST_DIR"))
        .join("tests/fixtures/transcribe/test_voice_48k.wav");
    assert!(fixture_48k.exists(), "48 kHz fixture must exist");

    let temp_dir = tempfile::tempdir().unwrap();

    // Resample
    let resampled = ada_cli::audio::ensure_16khz(&fixture_48k, temp_dir.path())
        .expect("resampling should succeed");

    // Run whisper-cli on the resampled file
    let whisper_path = ada_cli::binary_resolver::resolve(ada_cli::binary_resolver::Tool::WhisperCpp)
        .expect("whisper-cli should resolve");
    let model_path = ada_cli::model_manager::ensure_model("tiny")
        .expect("model should be available");

    let output_prefix = temp_dir.path().join("test_48k");
    let output = Command::new(&whisper_path)
        .arg("-f")
        .arg(&resampled)
        .arg("-m")
        .arg(&model_path)
        .arg("-oj")
        .arg("-of")
        .arg(&output_prefix)
        .output()
        .expect("whisper-cli should execute");

    assert!(
        output.status.success(),
        "whisper-cli should accept resampled 48 kHz audio: {}",
        String::from_utf8_lossy(&output.stderr)
    );

    let json_path = temp_dir.path().join("test_48k.json");
    assert!(json_path.exists(), "whisper-cli should produce JSON output");

    let content = std::fs::read_to_string(&json_path).unwrap();
    let parsed: serde_json::Value = serde_json::from_str(&content)
        .expect("output should be valid JSON");
    let transcription = parsed["transcription"]
        .as_array()
        .expect("should have transcription array");

    assert!(
        !transcription.is_empty(),
        "transcription should have at least one segment"
    );
}

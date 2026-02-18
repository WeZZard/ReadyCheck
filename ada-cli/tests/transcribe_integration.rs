//! Integration test for the whisper.cpp transcription pipeline.
//!
//! Requires:
//! - `whisper-cli` binary (built via `./utils/init_media_tools.sh`)
//! - `ggml-tiny.bin` model (auto-downloaded by model_manager)
//!
//! Skipped automatically when whisper-cli is not available.

use std::path::Path;
use std::process::Command;

/// Check if whisper-cli is available (bundled or in PATH)
fn whisper_cli_available() -> bool {
    ada_cli::binary_resolver::is_available(ada_cli::binary_resolver::Tool::WhisperCpp)
}

#[test]
fn transcribe__wav_fixture__then_produces_json_with_segments() {
    if !whisper_cli_available() {
        eprintln!("SKIPPED: whisper-cli not available (run ./utils/init_media_tools.sh)");
        return;
    }

    let whisper_path = ada_cli::binary_resolver::resolve(ada_cli::binary_resolver::Tool::WhisperCpp)
        .expect("whisper-cli should resolve");
    let model_path = ada_cli::model_manager::ensure_model("tiny")
        .expect("model should be available");

    let fixture_wav = Path::new(env!("CARGO_MANIFEST_DIR"))
        .join("tests/fixtures/transcribe/test_voice.wav");
    assert!(fixture_wav.exists(), "WAV fixture must exist");

    let temp_dir = tempfile::tempdir().unwrap();
    let output_prefix = temp_dir.path().join("test_voice");

    let output = Command::new(&whisper_path)
        .arg("-f")
        .arg(&fixture_wav)
        .arg("-m")
        .arg(&model_path)
        .arg("-oj")
        .arg("-of")
        .arg(&output_prefix)
        .output()
        .expect("whisper-cli should execute");

    assert!(
        output.status.success(),
        "whisper-cli failed: {}",
        String::from_utf8_lossy(&output.stderr)
    );

    let json_path = temp_dir.path().join("test_voice.json");
    assert!(json_path.exists(), "whisper-cli should produce JSON output");

    let content = std::fs::read_to_string(&json_path).unwrap();

    // Parse as whisper.cpp output format
    let parsed: serde_json::Value = serde_json::from_str(&content)
        .expect("output should be valid JSON");

    let transcription = parsed["transcription"]
        .as_array()
        .expect("should have transcription array");

    assert!(
        !transcription.is_empty(),
        "transcription should have at least one segment"
    );

    // Each segment should have offsets and text
    for seg in transcription {
        assert!(seg["offsets"]["from"].is_u64(), "segment should have offsets.from");
        assert!(seg["offsets"]["to"].is_u64(), "segment should have offsets.to");
        assert!(seg["text"].is_string(), "segment should have text");
    }

    // Concatenated text should contain recognizable words from the fixture
    let full_text: String = transcription
        .iter()
        .map(|seg| seg["text"].as_str().unwrap_or(""))
        .collect::<Vec<_>>()
        .join("");
    let full_text_lower = full_text.to_lowercase();

    // The fixture says "The quick brown fox jumps over the lazy dog. Testing one two three."
    // Whisper may not be 100% accurate, but should get most words
    let expected_words = ["fox", "dog", "testing"];
    let matched = expected_words
        .iter()
        .filter(|w| full_text_lower.contains(**w))
        .count();

    assert!(
        matched >= 2,
        "Expected at least 2 of {:?} in transcription, got {}: \"{}\"",
        expected_words,
        matched,
        full_text.trim()
    );
}

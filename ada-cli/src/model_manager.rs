//! Whisper model download and cache management.
//!
//! Ensures whisper.cpp models are available locally before transcription.
//! Models are resolved from bundled paths relative to the executable,
//! then fall back to downloading from HuggingFace and caching locally
//! next to the executable.

use std::path::PathBuf;
use std::process::Command;

use anyhow::{bail, Context, Result};

/// Known model names and their HuggingFace URLs
const MODELS: &[(&str, &str)] = &[
    (
        "tiny",
        "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-tiny.bin",
    ),
    (
        "base",
        "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-base.bin",
    ),
    (
        "small",
        "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-small.bin",
    ),
];

/// Ensure a whisper model is available, downloading if necessary.
///
/// Resolution order:
/// 1. Bundled model relative to executable (`../models/`, `models/`)
/// 2. Download from HuggingFace and cache next to the executable
///
/// Returns the path to the model file.
pub fn ensure_model(name: &str) -> Result<PathBuf> {
    let (_, url) = MODELS
        .iter()
        .find(|(n, _)| *n == name)
        .ok_or_else(|| anyhow::anyhow!("Unknown whisper model: {}", name))?;

    let filename = format!("ggml-{}.bin", name);

    // 1. Check bundled paths relative to executable
    if let Some(path) = find_bundled_model(&filename) {
        return Ok(path);
    }

    // 2. Download and cache locally next to the executable
    let models_dir = local_models_dir()?;
    let model_path = models_dir.join(&filename);

    if model_path.exists() {
        return Ok(model_path);
    }

    std::fs::create_dir_all(&models_dir)
        .with_context(|| format!("Failed to create models directory at {}", models_dir.display()))?;

    eprintln!("Downloading whisper model '{}' (~75MB)...", name);
    eprintln!("  From: {}", url);
    eprintln!("  To:   {}", model_path.display());

    let status = Command::new("curl")
        .arg("-L")
        .arg("--progress-bar")
        .arg("-o")
        .arg(&model_path)
        .arg(url)
        .status()
        .context("Failed to run curl for model download")?;

    if !status.success() {
        let _ = std::fs::remove_file(&model_path);
        bail!("Failed to download whisper model '{}'", name);
    }

    // Basic validation: file should be at least 1MB
    let metadata = std::fs::metadata(&model_path)
        .with_context(|| "Failed to read downloaded model metadata")?;
    if metadata.len() < 1_000_000 {
        let _ = std::fs::remove_file(&model_path);
        bail!(
            "Downloaded model file is too small ({}B), likely corrupted",
            metadata.len()
        );
    }

    eprintln!("Model '{}' downloaded successfully.", name);
    Ok(model_path)
}

/// Search for a bundled model relative to the current executable.
///
/// Checks:
/// - `<exe_dir>/../models/<filename>` (plugin layout: bin/ada, models/ggml-tiny.bin)
/// - `<exe_dir>/models/<filename>`
/// - `<exe_dir>/<filename>` (flat layout)
fn find_bundled_model(filename: &str) -> Option<PathBuf> {
    let exe_path = std::env::current_exe().ok()?;
    let exe_dir = exe_path.parent()?;

    let candidates = [
        exe_dir.parent().map(|p| p.join("models").join(filename)),
        Some(exe_dir.join("models").join(filename)),
        Some(exe_dir.join(filename)),
    ];

    for candidate in candidates.into_iter().flatten() {
        if candidate.exists() {
            return Some(candidate);
        }
    }

    None
}

/// Get the local models cache directory relative to the executable.
///
/// Returns `<exe_dir>/../models/` (plugin layout) or falls back to
/// `<exe_dir>/models/`.
fn local_models_dir() -> Result<PathBuf> {
    let exe_path = std::env::current_exe()
        .context("Failed to determine executable path")?;
    let exe_dir = exe_path
        .parent()
        .ok_or_else(|| anyhow::anyhow!("Executable has no parent directory"))?;

    // Prefer plugin layout: ../models/ relative to bin/
    if let Some(parent) = exe_dir.parent() {
        let models_dir = parent.join("models");
        // Only use plugin layout if ../models/ already exists (real plugin distribution)
        if models_dir.exists() {
            return Ok(models_dir);
        }
    }

    Ok(exe_dir.join("models"))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn ensure_model__unknown_name__then_error() {
        let result = ensure_model("nonexistent");
        assert!(result.is_err());
        assert!(result.unwrap_err().to_string().contains("Unknown whisper model"));
    }

    #[test]
    fn find_bundled_model__nonexistent__then_none() {
        let result = find_bundled_model("ggml-nonexistent.bin");
        assert!(result.is_none());
    }

    #[test]
    fn local_models_dir__returns_path_relative_to_exe() {
        let dir = local_models_dir().unwrap();
        assert!(
            dir.to_str().unwrap().contains("models"),
            "Should contain 'models': {}",
            dir.display()
        );
        // Without a real plugin layout (../models/ doesn't exist), should
        // fall back to <exe_dir>/models rather than <exe_dir>/../models
        let exe_path = std::env::current_exe().unwrap();
        let exe_dir = exe_path.parent().unwrap();
        let parent_models = exe_dir.parent().map(|p| p.join("models"));
        if parent_models.as_ref().map_or(true, |p| !p.exists()) {
            assert_eq!(dir, exe_dir.join("models"),
                "Without plugin layout, should use <exe_dir>/models");
        }
    }

    #[test]
    fn models_table__has_tiny() {
        let found = MODELS.iter().any(|(name, _)| *name == "tiny");
        assert!(found, "Models table should include 'tiny'");
    }

    #[test]
    fn models_table__urls_are_huggingface() {
        for (_, url) in MODELS {
            assert!(
                url.starts_with("https://huggingface.co/"),
                "Model URL should be from HuggingFace: {}",
                url
            );
        }
    }
}

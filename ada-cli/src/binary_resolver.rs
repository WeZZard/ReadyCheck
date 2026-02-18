//! Centralized binary resolution for external tools.
//!
//! Provides a unified way to locate FFmpeg, FFprobe, and whisper.cpp binaries.
//! Resolution order: env var override → bundled binary only.
//! System PATH is NOT searched — all tools must be bundled.

use std::path::PathBuf;

use anyhow::{bail, Result};

/// External tools that ADA depends on
#[derive(Debug, Clone, Copy)]
pub enum Tool {
    Ffmpeg,
    Ffprobe,
    WhisperCpp,
}

impl Tool {
    /// Environment variable name for overriding this tool's path
    fn env_var(self) -> &'static str {
        match self {
            Tool::Ffmpeg => "ADA_FFMPEG_PATH",
            Tool::Ffprobe => "ADA_FFPROBE_PATH",
            Tool::WhisperCpp => "ADA_WHISPER_PATH",
        }
    }

    /// Binary name when bundled
    fn bundled_name(self) -> &'static str {
        match self {
            Tool::Ffmpeg => "ffmpeg",
            Tool::Ffprobe => "ffprobe",
            Tool::WhisperCpp => "whisper-cli",
        }
    }

    /// Human-readable name for error messages
    fn display_name(self) -> &'static str {
        match self {
            Tool::Ffmpeg => "ffmpeg",
            Tool::Ffprobe => "ffprobe",
            Tool::WhisperCpp => "whisper",
        }
    }
}

/// Resolve the path to a tool binary.
///
/// Resolution order:
/// 1. Environment variable override (e.g. `ADA_FFMPEG_PATH`)
/// 2. Bundled binary relative to current executable
///
/// System PATH is intentionally NOT searched. All tools must be bundled
/// via `init_media_tools.sh` or distributed with the plugin.
pub fn resolve(tool: Tool) -> Result<PathBuf> {
    // 1. Check env var override
    if let Ok(path) = std::env::var(tool.env_var()) {
        let path = PathBuf::from(&path);
        if path.exists() {
            return Ok(path);
        }
    }

    // 2. Check bundled paths relative to current executable
    if let Some(path) = find_bundled(tool) {
        return Ok(path);
    }

    bail!(
        "{} not found.\n\
         Fix: Run ./utils/init_media_tools.sh (development)\n\
         or reinstall the plugin (production)",
        tool.display_name()
    )
}

/// Check if a tool is available (without erroring)
pub fn is_available(tool: Tool) -> bool {
    resolve(tool).is_ok()
}

/// Search for a bundled binary relative to the current executable.
///
/// Checks:
/// - Same directory as executable
/// - `../bin/` (plugin layout: bin/ada, bin/ffmpeg)
/// - `media_tools/` subdirectory
fn find_bundled(tool: Tool) -> Option<PathBuf> {
    let exe_path = std::env::current_exe().ok()?;
    let exe_dir = exe_path.parent()?;

    let name = tool.bundled_name();

    let candidates: Vec<PathBuf> = [
        Some(exe_dir.join(name)),
        exe_dir.parent().map(|p| p.join("bin").join(name)),
        Some(exe_dir.join("media_tools").join(name)),
    ]
    .into_iter()
    .flatten()
    .collect();

    for candidate in candidates {
        if candidate.exists() {
            return Some(candidate);
        }
    }

    None
}

#[cfg(test)]
mod tests {
    use super::*;
    use tempfile::TempDir;

    fn with_env<F, R>(key: &str, value: Option<&str>, f: F) -> R
    where
        F: FnOnce() -> R,
    {
        let _guard = crate::test_utils::ENV_MUTEX.lock().unwrap();
        let original = std::env::var(key).ok();

        match value {
            Some(v) => std::env::set_var(key, v),
            None => std::env::remove_var(key),
        }

        let result = f();

        match original {
            Some(v) => std::env::set_var(key, v),
            None => std::env::remove_var(key),
        }

        result
    }

    #[test]
    fn tool__env_var_names__then_correct() {
        assert_eq!(Tool::Ffmpeg.env_var(), "ADA_FFMPEG_PATH");
        assert_eq!(Tool::Ffprobe.env_var(), "ADA_FFPROBE_PATH");
        assert_eq!(Tool::WhisperCpp.env_var(), "ADA_WHISPER_PATH");
    }

    #[test]
    fn tool__bundled_names__then_correct() {
        assert_eq!(Tool::Ffmpeg.bundled_name(), "ffmpeg");
        assert_eq!(Tool::Ffprobe.bundled_name(), "ffprobe");
        assert_eq!(Tool::WhisperCpp.bundled_name(), "whisper-cli");
    }

    #[test]
    fn resolve__env_override_exists__then_uses_env() {
        let temp_dir = TempDir::new().unwrap();
        let fake_ffmpeg = temp_dir.path().join("ffmpeg");
        std::fs::write(&fake_ffmpeg, "#!/bin/sh\nexit 0").unwrap();

        let result = with_env(
            "ADA_FFMPEG_PATH",
            Some(fake_ffmpeg.to_str().unwrap()),
            || resolve(Tool::Ffmpeg),
        );

        assert!(result.is_ok());
        assert_eq!(result.unwrap(), fake_ffmpeg);
    }

    #[test]
    fn resolve__env_override_nonexistent__then_falls_through() {
        let result = with_env(
            "ADA_FFMPEG_PATH",
            Some("/nonexistent/ffmpeg"),
            || resolve(Tool::Ffmpeg),
        );

        // With no bundled binary and a nonexistent env path, resolve should fail
        // (no PATH fallback)
        if let Ok(path) = &result {
            // If it succeeded, it found a bundled binary — not the nonexistent env path
            assert_ne!(path, &PathBuf::from("/nonexistent/ffmpeg"));
        }
    }

    #[test]
    fn is_available__returns_bool() {
        // Just ensure it doesn't panic
        let _ = is_available(Tool::Ffmpeg);
        let _ = is_available(Tool::Ffprobe);
        let _ = is_available(Tool::WhisperCpp);
    }

    #[test]
    fn resolve__no_env_no_bundled__then_error() {
        let result = with_env(
            "ADA_FFMPEG_PATH",
            None,
            || resolve(Tool::Ffmpeg),
        );

        // Without env var or bundled binary, resolve must fail
        // (no PATH fallback — bundled binaries are required)
        if result.is_ok() {
            // Only passes if a bundled binary happens to exist next to the test binary
            return;
        }
        let err = result.unwrap_err().to_string();
        assert!(err.contains("not found"), "Error should say not found: {err}");
        assert!(err.contains("init_media_tools"), "Error should mention init script: {err}");
    }
}

//! Bundle reader for ADA capture bundles
//!
//! Parses session directories or .adabundle directories with manifest.json
//! to route queries to the appropriate data sources (trace, screen, voice).
//!
//! ## Bundle Resolution
//!
//! The `resolve_bundle_path` function accepts:
//! - `@latest` - Most recent session
//! - Session ID - e.g., `session_2026_01_24_14_56_19_a1b2c3`
//! - Direct path - Any directory containing `manifest.json`

use std::fs;
use std::path::{Path, PathBuf};

use anyhow::{bail, Context, Result};
use serde::Deserialize;

use crate::session_state;

/// Bundle manifest structure from manifest.json
#[derive(Debug, Deserialize)]
pub struct BundleManifest {
    /// Manifest version
    pub version: u32,
    /// Relative path to trace root directory (default: "trace")
    #[serde(default)]
    pub trace_root: Option<String>,
    /// Relative path to trace session directory (specific session within trace_root)
    #[serde(default)]
    pub trace_session: Option<String>,
    /// Relative path to screen recording (optional)
    #[serde(default)]
    pub screen_path: Option<String>,
    /// Relative path to voice recording (optional)
    #[serde(default)]
    pub voice_path: Option<String>,
    /// Relative path to lossless voice recording (optional)
    #[serde(default)]
    pub voice_lossless_path: Option<String>,
}

/// Resolve user input to a bundle directory path
///
/// Accepts:
/// - `@latest` - Returns the most recent session's directory
/// - Session ID (e.g., `session_2026_01_24_14_56_19_a1b2c3`) - Looks up in ~/.ada/sessions/
/// - Direct path - Returns as-is if it contains manifest.json
pub fn resolve_bundle_path(input: &Path) -> Result<PathBuf> {
    let input_str = input.to_string_lossy();

    // Handle special tokens starting with '@'
    // LCOV_EXCL_START - Session state integration requires real HOME directory
    if let Some(token) = input_str.strip_prefix('@') {
        return match token {
            "latest" => {
                let session = session_state::latest()?
                    .ok_or_else(|| anyhow::anyhow!("No sessions found"))?;
                Ok(session.session_path)
            }
            _ => bail!("Unknown token: @{}", token),
        };
    }
    // LCOV_EXCL_STOP

    // Direct path with manifest.json
    if input.exists() && input.join("manifest.json").exists() {
        return Ok(input.to_path_buf());
    }

    // Session ID lookup (matches session_YYYY_MM_DD_hh_mm_ss_hash format)
    // LCOV_EXCL_START - Session state integration requires real HOME directory
    if input_str.starts_with("session_") {
        if let Ok(Some(session)) = session_state::get(&input_str) {
            return Ok(session.session_path);
        }
        // Try as session directory even if session.json doesn't exist
        // (for direct queries to session directories)
        let session_dir = session_state::session_dir(&input_str)?;
        if session_dir.join("manifest.json").exists() {
            return Ok(session_dir);
        }
    }
    // LCOV_EXCL_STOP

    // If input exists as a directory but has no manifest, give a helpful error
    if input.exists() && input.is_dir() {
        bail!(
            "Directory exists but contains no manifest.json: {:?}\n\
             A valid bundle directory must contain manifest.json",
            input
        );
    }

    bail!(
        "Bundle not found: {}\n\
         Expected: @latest, session ID (session_...), or path to directory with manifest.json",
        input.display()
    )
}

/// An opened ADA bundle with validated manifest
#[derive(Debug)]
pub struct Bundle {
    /// Path to the bundle directory
    pub path: PathBuf,
    /// Parsed and validated manifest
    pub manifest: BundleManifest,
}

impl Bundle {
    /// Open a bundle from a path
    ///
    /// Validates that:
    /// 1. The path exists (or can be resolved via resolve_bundle_path)
    /// 2. manifest.json exists in the path
    /// 3. The manifest is valid JSON
    pub fn open(path: &Path) -> Result<Self> {
        // 1. Resolve the path (handles @latest, session IDs, etc.)
        let resolved = resolve_bundle_path(path)?;

        // 2. Read manifest
        let manifest_path = resolved.join("manifest.json");
        if !manifest_path.exists() {
            bail!("No manifest.json found in bundle: {:?}", resolved);
        }

        let content = fs::read_to_string(&manifest_path)
            .with_context(|| format!("Failed to read bundle manifest at {:?}", manifest_path))?;

        let manifest: BundleManifest = serde_json::from_str(&content)
            .with_context(|| "Failed to parse bundle manifest")?;

        Ok(Bundle {
            path: resolved,
            manifest,
        })
    }

    /// Get the trace session path for trace queries
    ///
    /// Returns the most specific path available:
    /// 1. If trace_session is set, returns bundle_path/trace_session
    /// 2. Otherwise, returns bundle_path/trace_root (default: "trace")
    pub fn trace_path(&self) -> PathBuf {
        if let Some(ref session) = self.manifest.trace_session {
            self.path.join(session)
        } else {
            let trace_root = self.manifest.trace_root.as_deref().unwrap_or("trace");
            self.path.join(trace_root)
        }
    }

    /// Get screen recording path if available
    #[allow(dead_code)]
    pub fn screen_path(&self) -> Option<PathBuf> {
        self.manifest
            .screen_path
            .as_ref()
            .map(|p| self.path.join(p))
    }

    /// Get voice recording path if available
    #[allow(dead_code)]
    pub fn voice_path(&self) -> Option<PathBuf> {
        self.manifest
            .voice_path
            .as_ref()
            .map(|p| self.path.join(p))
    }

    /// Get lossless voice recording path if available
    #[allow(dead_code)]
    pub fn voice_lossless_path(&self) -> Option<PathBuf> {
        self.manifest
            .voice_lossless_path
            .as_ref()
            .map(|p| self.path.join(p))
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Write;
    use tempfile::TempDir;

    fn create_valid_bundle() -> TempDir {
        let temp_dir = TempDir::new().unwrap();

        // Create trace directory
        let trace_dir = temp_dir.path().join("trace");
        fs::create_dir_all(&trace_dir).unwrap();

        // Create bundle manifest
        let manifest = r#"{
            "version": 1,
            "trace_session": "trace",
            "screen_path": "screen.mp4",
            "voice_path": "voice.m4a"
        }"#;

        let manifest_path = temp_dir.path().join("manifest.json");
        let mut f = fs::File::create(&manifest_path).unwrap();
        f.write_all(manifest.as_bytes()).unwrap();

        temp_dir
    }

    #[test]
    fn test_bundle__open_valid_bundle__succeeds() {
        let temp_dir = create_valid_bundle();
        let bundle = Bundle::open(temp_dir.path()).unwrap();

        assert_eq!(bundle.manifest.version, 1);
        assert_eq!(
            bundle.manifest.trace_session,
            Some("trace".to_string())
        );
        assert_eq!(bundle.manifest.screen_path, Some("screen.mp4".to_string()));
        assert_eq!(bundle.manifest.voice_path, Some("voice.m4a".to_string()));
    }

    #[test]
    fn test_bundle__trace_path__with_trace_session__returns_joined_path() {
        let temp_dir = create_valid_bundle();
        let bundle = Bundle::open(temp_dir.path()).unwrap();

        let trace_path = bundle.trace_path();
        assert_eq!(trace_path, temp_dir.path().join("trace"));
    }

    #[test]
    fn test_bundle__trace_path__without_trace_session__uses_trace_root() {
        let temp_dir = TempDir::new().unwrap();

        // Create manifest with trace_root but no trace_session
        let manifest = r#"{
            "version": 1,
            "trace_root": "my_traces"
        }"#;

        let manifest_path = temp_dir.path().join("manifest.json");
        let mut f = fs::File::create(&manifest_path).unwrap();
        f.write_all(manifest.as_bytes()).unwrap();

        let bundle = Bundle::open(temp_dir.path()).unwrap();
        assert_eq!(bundle.trace_path(), temp_dir.path().join("my_traces"));
    }

    #[test]
    fn test_bundle__trace_path__no_trace_fields__uses_default() {
        let temp_dir = TempDir::new().unwrap();

        // Create manifest with no trace fields
        let manifest = r#"{ "version": 1 }"#;

        let manifest_path = temp_dir.path().join("manifest.json");
        let mut f = fs::File::create(&manifest_path).unwrap();
        f.write_all(manifest.as_bytes()).unwrap();

        let bundle = Bundle::open(temp_dir.path()).unwrap();
        // Should default to "trace"
        assert_eq!(bundle.trace_path(), temp_dir.path().join("trace"));
    }

    #[test]
    fn test_bundle__screen_path__returns_joined_path() {
        let temp_dir = create_valid_bundle();
        let bundle = Bundle::open(temp_dir.path()).unwrap();

        let screen_path = bundle.screen_path().unwrap();
        assert_eq!(screen_path, temp_dir.path().join("screen.mp4"));
    }

    #[test]
    fn test_bundle__missing_manifest__fails() {
        let temp_dir = TempDir::new().unwrap();
        // Don't create manifest.json

        let result = Bundle::open(temp_dir.path());
        assert!(result.is_err());
        let err_msg = result.unwrap_err().to_string();
        assert!(
            err_msg.contains("manifest.json"),
            "Expected error about manifest.json, got: {}",
            err_msg
        );
    }

    #[test]
    fn test_bundle__nonexistent_path__fails() {
        let result = Bundle::open(Path::new("/nonexistent/path/to/bundle"));
        assert!(result.is_err());
        let err_msg = result.unwrap_err().to_string();
        assert!(
            err_msg.contains("not found") || err_msg.contains("does not exist"),
            "Expected error about path not found, got: {}",
            err_msg
        );
    }

    #[test]
    fn test_bundle__optional_fields_missing__succeeds() {
        let temp_dir = TempDir::new().unwrap();

        // Create manifest with only version (everything else optional)
        let manifest = r#"{ "version": 1 }"#;

        let manifest_path = temp_dir.path().join("manifest.json");
        let mut f = fs::File::create(&manifest_path).unwrap();
        f.write_all(manifest.as_bytes()).unwrap();

        let bundle = Bundle::open(temp_dir.path()).unwrap();
        assert!(bundle.screen_path().is_none());
        assert!(bundle.voice_path().is_none());
        assert!(bundle.voice_lossless_path().is_none());
    }

    #[test]
    fn test_resolve_bundle_path__directory_with_manifest__then_returns_path() {
        let temp_dir = TempDir::new().unwrap();
        let manifest = r#"{ "version": 1 }"#;
        fs::write(temp_dir.path().join("manifest.json"), manifest).unwrap();

        let result = resolve_bundle_path(temp_dir.path()).unwrap();
        assert_eq!(result, temp_dir.path());
    }

    #[test]
    fn test_resolve_bundle_path__directory_without_manifest__then_error() {
        let temp_dir = TempDir::new().unwrap();
        // Don't create manifest.json

        let result = resolve_bundle_path(temp_dir.path());
        assert!(result.is_err());
        assert!(result
            .unwrap_err()
            .to_string()
            .contains("no manifest.json"));
    }

    #[test]
    fn test_resolve_bundle_path__nonexistent__then_error() {
        let result = resolve_bundle_path(Path::new("/nonexistent/bundle"));
        assert!(result.is_err());
        assert!(result.unwrap_err().to_string().contains("not found"));
    }

    #[test]
    fn test_resolve_bundle_path__unknown_token__then_error() {
        let result = resolve_bundle_path(Path::new("@unknown"));
        assert!(result.is_err());
        assert!(result
            .unwrap_err()
            .to_string()
            .contains("Unknown token: @unknown"));
    }
}

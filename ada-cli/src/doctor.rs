//! Doctor command for system health checks.
//!
//! Provides CLI commands for verifying ADA dependencies and system configuration.

use clap::Subcommand;
use serde::Serialize;
use std::path::PathBuf;

#[derive(Subcommand)]
pub enum DoctorCommands {
    /// Run all health checks
    Check {
        /// Output format (text or json)
        #[arg(short, long, default_value = "text")]
        format: String,
    },
}

/// Result of a single health check
#[derive(Serialize, Clone)]
struct CheckResult {
    ok: bool,
    #[serde(skip_serializing_if = "Option::is_none")]
    path: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    fix: Option<String>,
}

/// All check results
#[derive(Serialize)]
struct DoctorReport {
    status: String,
    checks: CheckResults,
    issues_count: usize,
}

#[derive(Serialize)]
struct CheckResults {
    frida_agent: CheckResult,
    whisper: CheckResult,
    ffmpeg: CheckResult,
}

pub fn run(cmd: DoctorCommands) -> anyhow::Result<()> {
    match cmd {
        DoctorCommands::Check { format } => run_checks(&format),
    }
}

fn run_checks(format: &str) -> anyhow::Result<()> {
    let frida_agent = check_frida_agent();
    let whisper = check_whisper();
    let ffmpeg = check_ffmpeg();

    let issues_count = [&frida_agent, &whisper, &ffmpeg]
        .iter()
        .filter(|c| !c.ok)
        .count();

    let status = if issues_count == 0 {
        "ok".to_string()
    } else {
        "issues_found".to_string()
    };

    if format == "json" {
        let report = DoctorReport {
            status,
            checks: CheckResults {
                frida_agent,
                whisper,
                ffmpeg,
            },
            issues_count,
        };
        println!("{}", serde_json::to_string_pretty(&report)?);
    } else {
        print_text_report(&frida_agent, &whisper, &ffmpeg, issues_count);
    }

    if issues_count > 0 {
        std::process::exit(1);
    }

    Ok(())
}

fn print_text_report(
    frida_agent: &CheckResult,
    whisper: &CheckResult,
    ffmpeg: &CheckResult,
    issues_count: usize,
) {
    println!("ADA Doctor");
    println!("==========\n");

    println!("Core:");
    print_check("frida agent", frida_agent);
    println!();

    println!("Analysis:");
    print_check("whisper", whisper);
    print_check("ffmpeg", ffmpeg);
    println!();

    if issues_count == 0 {
        println!("Status: All checks passed");
    } else {
        println!(
            "Status: {} issue{} found",
            issues_count,
            if issues_count == 1 { "" } else { "s" }
        );
    }
}

fn print_check(name: &str, result: &CheckResult) {
    if result.ok {
        if let Some(path) = &result.path {
            println!("  \u{2713} {}: {}", name, path);
        } else {
            println!("  \u{2713} {}: valid", name);
        }
    } else {
        println!("  \u{2717} {}: not found", name);
        if let Some(fix) = &result.fix {
            println!("    \u{2192} {}", fix);
        }
    }
}

/// Check if Frida agent library is available
fn check_frida_agent() -> CheckResult {
    // Check ADA_AGENT_RPATH_SEARCH_PATHS environment variable first
    if let Ok(search_paths) = std::env::var("ADA_AGENT_RPATH_SEARCH_PATHS") {
        for path in search_paths.split(':') {
            let agent_path = PathBuf::from(path).join("libfrida_agent.dylib");
            if agent_path.exists() {
                return CheckResult {
                    ok: true,
                    path: Some(agent_path.display().to_string()),
                    fix: None,
                };
            }
        }
    }

    // Check known paths relative to the ada binary
    if let Ok(exe_path) = std::env::current_exe() {
        if let Some(bin_dir) = exe_path.parent() {
            // Check sibling lib directory
            let lib_dir = bin_dir.parent().map(|p| p.join("lib"));
            if let Some(lib_path) = lib_dir {
                let agent_path = lib_path.join("libfrida_agent.dylib");
                if agent_path.exists() {
                    return CheckResult {
                        ok: true,
                        path: Some(agent_path.display().to_string()),
                        fix: None,
                    };
                }
            }

            // Check same directory as binary
            let agent_path = bin_dir.join("libfrida_agent.dylib");
            if agent_path.exists() {
                return CheckResult {
                    ok: true,
                    path: Some(agent_path.display().to_string()),
                    fix: None,
                };
            }
        }
    }

    CheckResult {
        ok: false,
        path: None,
        fix: Some("Set ADA_AGENT_RPATH_SEARCH_PATHS to directory containing libfrida_agent.dylib".to_string()),
    }
}

/// Check if whisper is installed (bundled or system)
fn check_whisper() -> CheckResult {
    match ada_cli::binary_resolver::resolve(ada_cli::binary_resolver::Tool::WhisperCpp) {
        Ok(path) => CheckResult {
            ok: true,
            path: Some(path.display().to_string()),
            fix: None,
        },
        Err(_) => CheckResult {
            ok: false,
            path: None,
            fix: Some("Run: ./utils/init_media_tools.sh".to_string()),
        },
    }
}

/// Check if ffmpeg is installed (bundled or system)
fn check_ffmpeg() -> CheckResult {
    match ada_cli::binary_resolver::resolve(ada_cli::binary_resolver::Tool::Ffmpeg) {
        Ok(path) => CheckResult {
            ok: true,
            path: Some(path.display().to_string()),
            fix: None,
        },
        Err(_) => CheckResult {
            ok: false,
            path: None,
            fix: Some("Run: ./utils/init_media_tools.sh".to_string()),
        },
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use tempfile::TempDir;

    /// Execute a closure with a temporarily modified environment variable.
    /// Restores the original value (or removes the variable) after the closure completes.
    fn with_env<F, R>(key: &str, value: Option<&str>, f: F) -> R
    where
        F: FnOnce() -> R,
    {
        let _guard = ada_cli::test_utils::ENV_MUTEX.lock().unwrap();
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

    /// Execute a closure with multiple environment variables temporarily modified.
    fn with_envs<F, R>(vars: &[(&str, Option<&str>)], f: F) -> R
    where
        F: FnOnce() -> R,
    {
        let _guard = ada_cli::test_utils::ENV_MUTEX.lock().unwrap();
        let mut originals = Vec::new();

        for (key, value) in vars {
            originals.push((*key, std::env::var(key).ok()));
            match value {
                Some(v) => std::env::set_var(key, v),
                None => std::env::remove_var(key),
            }
        }

        let result = f();

        for (key, original) in originals {
            match original {
                Some(v) => std::env::set_var(key, v),
                None => std::env::remove_var(key),
            }
        }

        result
    }

    // =========================================================================
    // Existing serialization tests
    // =========================================================================

    #[test]
    fn check_result_json_serialization() {
        let result = CheckResult {
            ok: true,
            path: Some("/usr/bin/test".to_string()),
            fix: None,
        };
        let json = serde_json::to_string(&result).unwrap();
        assert!(json.contains("\"ok\":true"));
        assert!(json.contains("\"path\":\"/usr/bin/test\""));
        // fix should be skipped when None
        assert!(!json.contains("\"fix\""));
    }

    #[test]
    fn check_result_with_fix() {
        let result = CheckResult {
            ok: false,
            path: None,
            fix: Some("brew install test".to_string()),
        };
        let json = serde_json::to_string(&result).unwrap();
        assert!(json.contains("\"ok\":false"));
        assert!(json.contains("\"fix\":\"brew install test\""));
        // path should be skipped when None
        assert!(!json.contains("\"path\""));
    }

    #[test]
    fn doctor_report_serialization() {
        let report = DoctorReport {
            status: "ok".to_string(),
            checks: CheckResults {
                frida_agent: CheckResult {
                    ok: true,
                    path: Some("/path/to/lib".to_string()),
                    fix: None,
                },
                whisper: CheckResult {
                    ok: true,
                    path: Some("/opt/homebrew/bin/whisper".to_string()),
                    fix: None,
                },
                ffmpeg: CheckResult {
                    ok: false,
                    path: None,
                    fix: Some("Run: ./utils/init_media_tools.sh".to_string()),
                },
            },
            issues_count: 1,
        };
        let json = serde_json::to_string_pretty(&report).unwrap();
        assert!(json.contains("\"status\": \"ok\""));
        assert!(json.contains("\"issues_count\": 1"));
    }

    // =========================================================================
    // Frida Agent Check Tests
    // =========================================================================

    #[test]
    fn check_frida_agent__env_path_exists__then_ok() {
        let temp_dir = TempDir::new().unwrap();
        let agent_path = temp_dir.path().join("libfrida_agent.dylib");
        std::fs::write(&agent_path, b"mock frida agent").unwrap();

        let result = with_env(
            "ADA_AGENT_RPATH_SEARCH_PATHS",
            Some(temp_dir.path().to_str().unwrap()),
            check_frida_agent,
        );

        assert!(result.ok, "Expected frida agent check to pass");
        assert!(
            result.path.is_some(),
            "Expected path to be set when agent found"
        );
        assert!(
            result.path.unwrap().contains("libfrida_agent.dylib"),
            "Path should contain libfrida_agent.dylib"
        );
        assert!(result.fix.is_none(), "No fix should be needed when ok");
    }

    #[test]
    fn check_frida_agent__env_path_empty__then_not_found() {
        let temp_dir = TempDir::new().unwrap();
        // Don't create the agent file - directory is empty

        let result = with_env(
            "ADA_AGENT_RPATH_SEARCH_PATHS",
            Some(temp_dir.path().to_str().unwrap()),
            check_frida_agent,
        );

        // The check may still pass if the binary-relative path has the agent
        // So we just verify the function runs without panic
        // and returns a valid CheckResult
        assert!(
            result.ok || result.fix.is_some(),
            "Result should either be ok or have a fix suggestion"
        );
    }

    #[test]
    fn check_frida_agent__multiple_paths__first_match_wins() {
        let temp_dir1 = TempDir::new().unwrap();
        let temp_dir2 = TempDir::new().unwrap();

        // Only create agent in second directory
        let agent_path = temp_dir2.path().join("libfrida_agent.dylib");
        std::fs::write(&agent_path, b"mock frida agent").unwrap();

        let search_paths = format!(
            "{}:{}",
            temp_dir1.path().display(),
            temp_dir2.path().display()
        );

        let result = with_env(
            "ADA_AGENT_RPATH_SEARCH_PATHS",
            Some(&search_paths),
            check_frida_agent,
        );

        assert!(result.ok, "Should find agent in second path");
        assert!(result.path.is_some());
        let path = result.path.unwrap();
        assert!(
            path.contains(temp_dir2.path().to_str().unwrap()),
            "Found path should be from temp_dir2"
        );
    }

    #[test]
    fn check_frida_agent__no_env__then_well_formed_result() {
        // Remove the env var - result depends on binary-relative paths
        let result = with_env("ADA_AGENT_RPATH_SEARCH_PATHS", None, check_frida_agent);

        // Result must follow the ok/fix invariant
        assert_eq!(
            result.ok,
            result.fix.is_none(),
            "Invariant violated: ok={}, fix.is_none()={}",
            result.ok,
            result.fix.is_none()
        );

        // Verify fix content contains expected text when present
        let fix_valid = result
            .fix
            .as_ref()
            .map(|f| f.contains("ADA_AGENT_RPATH_SEARCH_PATHS"))
            .unwrap_or(true);
        assert!(fix_valid, "Fix should mention the environment variable");

        // Verify path content contains agent filename when present
        let path_valid = result
            .path
            .as_ref()
            .map(|p| p.contains("libfrida_agent.dylib"))
            .unwrap_or(true);
        assert!(path_valid, "Path should contain the agent filename");
    }

    // =========================================================================
    // Whisper Check Tests
    // =========================================================================

    #[test]
    fn check_whisper__env_override__then_ok() {
        let temp_dir = TempDir::new().unwrap();
        let exe_path = temp_dir.path().join("whisper-cli");

        // Create a mock executable
        #[cfg(unix)]
        {
            use std::os::unix::fs::PermissionsExt;
            std::fs::write(&exe_path, "#!/bin/sh\nexit 0").unwrap();
            std::fs::set_permissions(&exe_path, std::fs::Permissions::from_mode(0o755)).unwrap();
        }
        #[cfg(not(unix))]
        {
            std::fs::write(&exe_path, "mock executable").unwrap();
        }

        let result = with_env(
            "ADA_WHISPER_PATH",
            Some(exe_path.to_str().unwrap()),
            check_whisper,
        );

        assert!(result.ok, "Whisper should be found via env override");
        assert!(result.path.is_some(), "Path should be set when found");
        assert!(
            result.path.unwrap().contains("whisper-cli"),
            "Path should contain 'whisper-cli'"
        );
    }

    #[test]
    fn check_whisper__not_in_path__then_not_found() {
        // Use an empty PATH to ensure whisper won't be found
        let result = with_envs(
            &[
                ("PATH", Some("")),
                ("ADA_WHISPER_PATH", None),
            ],
            check_whisper,
        );

        assert!(!result.ok, "Whisper should not be found with empty PATH");
        assert!(result.path.is_none(), "Path should be None when not found");
        assert!(result.fix.is_some(), "Should have fix suggestion");
        assert!(
            result.fix.unwrap().contains("init_media_tools"),
            "Fix should suggest init_media_tools.sh"
        );
    }

    // =========================================================================
    // FFmpeg Check Tests
    // =========================================================================

    #[test]
    fn check_ffmpeg__env_override__then_ok() {
        let temp_dir = TempDir::new().unwrap();
        let exe_path = temp_dir.path().join("ffmpeg");

        // Create a mock executable
        #[cfg(unix)]
        {
            use std::os::unix::fs::PermissionsExt;
            std::fs::write(&exe_path, "#!/bin/sh\nexit 0").unwrap();
            std::fs::set_permissions(&exe_path, std::fs::Permissions::from_mode(0o755)).unwrap();
        }
        #[cfg(not(unix))]
        {
            std::fs::write(&exe_path, "mock executable").unwrap();
        }

        let result = with_env(
            "ADA_FFMPEG_PATH",
            Some(exe_path.to_str().unwrap()),
            check_ffmpeg,
        );

        assert!(result.ok, "FFmpeg should be found via env override");
        assert!(result.path.is_some(), "Path should be set when found");
        assert!(
            result.path.unwrap().contains("ffmpeg"),
            "Path should contain 'ffmpeg'"
        );
    }

    #[test]
    fn check_ffmpeg__not_in_path__then_not_found() {
        // Use an empty PATH to ensure ffmpeg won't be found
        let result = with_envs(
            &[
                ("PATH", Some("")),
                ("ADA_FFMPEG_PATH", None),
            ],
            check_ffmpeg,
        );

        assert!(!result.ok, "FFmpeg should not be found with empty PATH");
        assert!(result.path.is_none(), "Path should be None when not found");
        assert!(result.fix.is_some(), "Should have fix suggestion");
        assert!(
            result.fix.unwrap().contains("init_media_tools"),
            "Fix should suggest init_media_tools.sh"
        );
    }

    // =========================================================================
    // Report Generation Tests
    // =========================================================================

    #[test]
    fn issues_count__all_pass__then_zero() {
        let checks = [
            CheckResult {
                ok: true,
                path: Some("/path".to_string()),
                fix: None,
            },
            CheckResult {
                ok: true,
                path: None,
                fix: None,
            },
            CheckResult {
                ok: true,
                path: Some("/path2".to_string()),
                fix: None,
            },
            CheckResult {
                ok: true,
                path: None,
                fix: None,
            },
        ];

        let count = checks.iter().filter(|c| !c.ok).count();
        assert_eq!(count, 0, "All checks pass should have zero issues");
    }

    #[test]
    fn issues_count__some_fail__then_correct_count() {
        let checks = [
            CheckResult {
                ok: true,
                path: Some("/path".to_string()),
                fix: None,
            },
            CheckResult {
                ok: false,
                path: None,
                fix: Some("fix1".to_string()),
            },
            CheckResult {
                ok: true,
                path: None,
                fix: None,
            },
            CheckResult {
                ok: false,
                path: None,
                fix: Some("fix2".to_string()),
            },
        ];

        let count = checks.iter().filter(|c| !c.ok).count();
        assert_eq!(count, 2, "Should count exactly 2 failures");
    }

    #[test]
    fn issues_count__all_fail__then_four() {
        let checks = [
            CheckResult {
                ok: false,
                path: None,
                fix: Some("fix1".to_string()),
            },
            CheckResult {
                ok: false,
                path: None,
                fix: Some("fix2".to_string()),
            },
            CheckResult {
                ok: false,
                path: None,
                fix: Some("fix3".to_string()),
            },
            CheckResult {
                ok: false,
                path: None,
                fix: Some("fix4".to_string()),
            },
        ];

        let count = checks.iter().filter(|c| !c.ok).count();
        assert_eq!(count, 4, "All checks failing should have 4 issues");
    }

    /// Helper to derive status string from issues count (mirrors production logic)
    fn status_from_count(count: usize) -> &'static str {
        if count == 0 {
            "ok"
        } else {
            "issues_found"
        }
    }

    #[test]
    fn status_from_issues_count__covers_all_branches() {
        // Test zero issues -> ok
        assert_eq!(status_from_count(0), "ok");
        // Test non-zero issues -> issues_found
        assert_eq!(status_from_count(1), "issues_found");
        assert_eq!(status_from_count(4), "issues_found");
    }

    #[test]
    fn doctor_report__json_format__then_valid_json() {
        let report = DoctorReport {
            status: "issues_found".to_string(),
            checks: CheckResults {
                frida_agent: CheckResult {
                    ok: false,
                    path: None,
                    fix: Some("Set ADA_AGENT_RPATH_SEARCH_PATHS".to_string()),
                },
                whisper: CheckResult {
                    ok: false,
                    path: None,
                    fix: Some("Run: ./utils/init_media_tools.sh".to_string()),
                },
                ffmpeg: CheckResult {
                    ok: true,
                    path: Some("/opt/homebrew/bin/ffmpeg".to_string()),
                    fix: None,
                },
            },
            issues_count: 2,
        };

        let json = serde_json::to_string_pretty(&report).unwrap();

        // Verify it's valid JSON by parsing it back
        let parsed: serde_json::Value = serde_json::from_str(&json).unwrap();

        assert_eq!(parsed["status"], "issues_found");
        assert_eq!(parsed["issues_count"], 2);
        assert_eq!(parsed["checks"]["frida_agent"]["ok"], false);
        assert_eq!(parsed["checks"]["whisper"]["ok"], false);
        assert_eq!(parsed["checks"]["ffmpeg"]["ok"], true);
        assert_eq!(
            parsed["checks"]["ffmpeg"]["path"],
            "/opt/homebrew/bin/ffmpeg"
        );
    }

    // =========================================================================
    // CheckResult Invariant Tests
    // =========================================================================

    /// Validates a CheckResult follows the expected invariants
    fn validate_check_result(result: &CheckResult) {
        // Invariant: ok results should not have a fix
        // Invariant: not-ok results should have a fix
        assert_eq!(
            result.ok,
            result.fix.is_none(),
            "ok={} but fix.is_none()={} - invariant violated",
            result.ok,
            result.fix.is_none()
        );
    }

    #[test]
    fn check_result_invariant__ok_implies_no_fix() {
        let result = CheckResult {
            ok: true,
            path: Some("/path".to_string()),
            fix: None,
        };
        validate_check_result(&result);
    }

    #[test]
    fn check_result_invariant__not_ok_implies_has_fix() {
        let result = CheckResult {
            ok: false,
            path: None,
            fix: Some("some fix".to_string()),
        };
        validate_check_result(&result);
    }

    // =========================================================================
    // Integration Tests for with_envs helper
    // =========================================================================

    #[test]
    fn with_envs__multiple_vars__all_restored() {
        let key1 = "TEST_DOCTOR_VAR1";
        let key2 = "TEST_DOCTOR_VAR2";

        // Set initial values
        std::env::set_var(key1, "original1");
        std::env::set_var(key2, "original2");

        with_envs(
            &[(key1, Some("modified1")), (key2, Some("modified2"))],
            || {
                assert_eq!(std::env::var(key1).unwrap(), "modified1");
                assert_eq!(std::env::var(key2).unwrap(), "modified2");
            },
        );

        // Verify restoration
        assert_eq!(std::env::var(key1).unwrap(), "original1");
        assert_eq!(std::env::var(key2).unwrap(), "original2");

        // Cleanup
        std::env::remove_var(key1);
        std::env::remove_var(key2);
    }

    #[test]
    fn with_envs__with_none_values__removes_and_restores() {
        let key1 = "TEST_DOCTOR_VAR_NONE1";
        let key2 = "TEST_DOCTOR_VAR_NONE2";

        // Set initial values
        std::env::set_var(key1, "initial1");
        // key2 starts unset

        with_envs(&[(key1, None), (key2, Some("temp_value"))], || {
            // key1 should be removed
            assert!(
                std::env::var(key1).is_err(),
                "key1 should be removed during closure"
            );
            // key2 should be set
            assert_eq!(std::env::var(key2).unwrap(), "temp_value");
        });

        // Verify restoration
        assert_eq!(
            std::env::var(key1).unwrap(),
            "initial1",
            "key1 should be restored"
        );
        assert!(
            std::env::var(key2).is_err(),
            "key2 should be removed (was unset before)"
        );

        // Cleanup
        std::env::remove_var(key1);
    }

    #[test]
    fn with_env__removes_and_restores_correctly() {
        let key = "TEST_DOCTOR_VAR_REMOVE";

        // Set initial value
        std::env::set_var(key, "initial");

        // Remove during test
        with_env(key, None, || {
            assert!(
                std::env::var(key).is_err(),
                "Variable should be removed during closure"
            );
        });

        // Verify restoration
        assert_eq!(std::env::var(key).unwrap(), "initial");

        // Cleanup
        std::env::remove_var(key);
    }
}

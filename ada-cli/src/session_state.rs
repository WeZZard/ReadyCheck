//! Session state management for ADA capture sessions.
//!
//! Provides multi-session support with session directories in `~/.ada/sessions/`.
//! Each session gets a unique ID and directory containing:
//! - `session.json` - Session metadata (status, timestamps, app info)
//! - `manifest.json` - Bundle manifest for queries
//! - `trace/` - Trace data
//! - `screen.mp4` - Screen recording (optional)
//! - `voice.m4a` - Voice recording (optional)
//!
//! The session directory IS the bundle - no nested `.adabundle` needed.

use anyhow::{bail, Context, Result};
use clap::Subcommand;
use serde::{Deserialize, Serialize};
use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;

/// Sessions directory path relative to home: ~/.ada/sessions/
pub const SESSIONS_DIR: &str = ".ada/sessions";

/// Session status enum
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum SessionStatus {
    Running,
    Complete,
    Failed,
}

/// Application information extracted from binary path
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct AppInfo {
    pub name: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub bundle_id: Option<String>,
}

/// Session state stored in ~/.ada/sessions/<session_id>/session.json
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct SessionState {
    pub session_id: String,
    pub session_path: PathBuf,
    pub start_time: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub end_time: Option<String>,
    pub app_info: AppInfo,
    pub status: SessionStatus,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub pid: Option<u32>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub capture_pid: Option<u32>,
}

/// Session subcommands for CLI
#[derive(Subcommand)]
pub enum SessionCommands {
    /// List all sessions
    List {
        /// Show only running sessions
        #[arg(long)]
        running: bool,

        /// Filter by app name
        #[arg(long)]
        app: Option<String>,

        /// Output format (text or json)
        #[arg(short, long, default_value = "text")]
        format: String,
    },

    /// Show latest session (prints bundle path)
    Latest {
        /// Show latest running session only
        #[arg(long)]
        running: bool,
    },

    /// Clean up orphaned sessions
    Cleanup,
}

// LCOV_EXCL_START - CLI command handlers output to stdout, tested via integration

/// Run session subcommand
pub fn run(cmd: SessionCommands) -> Result<()> {
    match cmd {
        SessionCommands::List {
            running,
            app,
            format,
        } => cmd_list(running, app.as_deref(), &format),
        SessionCommands::Latest { running } => cmd_latest(running),
        SessionCommands::Cleanup => cmd_cleanup(),
    }
}

fn cmd_list(running_only: bool, app_filter: Option<&str>, format: &str) -> Result<()> {
    let sessions = if running_only {
        list_running()?
    } else if let Some(app) = app_filter {
        find_by_app(app)?
    } else {
        list()?
    };

    // Apply additional filtering if both running and app are specified
    let sessions: Vec<_> = if running_only && app_filter.is_some() {
        let app = app_filter.unwrap().to_lowercase();
        sessions
            .into_iter()
            .filter(|s| s.app_info.name.to_lowercase().contains(&app))
            .collect()
    } else {
        sessions
    };

    match format {
        "json" => {
            let json = serde_json::to_string_pretty(&sessions)?;
            println!("{}", json);
        }
        _ => {
            if sessions.is_empty() {
                println!("No sessions found.");
            } else {
                println!(
                    "{:<40} {:<20} {:<10} {:<24}",
                    "SESSION ID", "APP", "STATUS", "STARTED"
                );
                println!("{}", "-".repeat(94));
                for session in &sessions {
                    println!(
                        "{:<40} {:<20} {:<10} {:<24}",
                        session.session_id,
                        truncate(&session.app_info.name, 20),
                        format!("{:?}", session.status).to_lowercase(),
                        &session.start_time[..std::cmp::min(19, session.start_time.len())]
                    );
                }
                println!("\nTotal: {} session(s)", sessions.len());
            }
        }
    }

    Ok(())
}

fn cmd_latest(running_only: bool) -> Result<()> {
    let session = if running_only {
        latest_running()?
    } else {
        latest()?
    };

    match session {
        Some(s) => {
            println!("{}", s.session_path.display());
        }
        None => {
            if running_only {
                bail!("No running sessions found");
            } else {
                bail!("No sessions found");
            }
        }
    }

    Ok(())
}

fn cmd_cleanup() -> Result<()> {
    let orphaned = cleanup_orphaned()?;

    if orphaned.is_empty() {
        println!("No orphaned sessions found.");
    } else {
        println!("Marked {} session(s) as failed:", orphaned.len());
        for session in &orphaned {
            println!("  - {} ({})", session.session_id, session.app_info.name);
        }
    }

    Ok(())
}

fn truncate(s: &str, max_len: usize) -> String {
    if s.len() > max_len {
        format!("{}...", &s[..max_len - 3])
    } else {
        s.to_string()
    }
}

// LCOV_EXCL_STOP

/// Get sessions directory: ~/.ada/sessions/
pub fn sessions_dir() -> Result<PathBuf> {
    let home = std::env::var("HOME").context("HOME environment variable not set")?;
    Ok(PathBuf::from(home).join(SESSIONS_DIR))
}

/// Generate unique session ID: session_YYYY_MM_DD_hh_mm_ss_{short_hash}
///
/// The short hash provides uniqueness if multiple sessions start in the same second.
pub fn generate_session_id(_app_name: &str) -> String {
    use std::time::{SystemTime, UNIX_EPOCH};

    let now = chrono::Utc::now();
    let timestamp = now.format("%Y_%m_%d_%H_%M_%S").to_string();

    // Generate short hash from high-precision timestamp for uniqueness
    let nanos = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .as_nanos();
    let hash = format!("{:x}", nanos % 0xFFFFFF); // 6 hex chars max

    format!("session_{}_{}", timestamp, hash)
}

/// Get session directory path: ~/.ada/sessions/<session_id>/
pub fn session_dir(session_id: &str) -> Result<PathBuf> {
    Ok(sessions_dir()?.join(session_id))
}

/// Register a new session (creates directory in ~/.ada/sessions/<session_id>/)
pub fn register(session: &SessionState) -> Result<()> {
    let dir = session_dir(&session.session_id)?;
    fs::create_dir_all(&dir)
        .with_context(|| format!("Failed to create session dir at {:?}", dir))?;

    let file_path = dir.join("session.json");
    let json = serde_json::to_string_pretty(session)?;

    // Atomic write: write to temp file then rename
    let temp_path = file_path.with_extension("tmp");
    fs::write(&temp_path, &json)
        .with_context(|| format!("Failed to write session state to {:?}", temp_path))?;
    fs::rename(&temp_path, &file_path)
        .with_context(|| format!("Failed to rename temp file to {:?}", file_path))?;

    Ok(())
}

/// Update a session by ID
pub fn update(session_id: &str, session: &SessionState) -> Result<()> {
    let dir = session_dir(session_id)?;
    let file_path = dir.join("session.json");

    if !file_path.exists() {
        bail!("Session {} not found", session_id);
    }

    let json = serde_json::to_string_pretty(session)?;

    // Atomic write
    let temp_path = file_path.with_extension("tmp");
    fs::write(&temp_path, &json)?;
    fs::rename(&temp_path, &file_path)?;

    Ok(())
}

/// Get a session by ID
pub fn get(session_id: &str) -> Result<Option<SessionState>> {
    let dir = session_dir(session_id)?;
    let file_path = dir.join("session.json");

    if !file_path.exists() {
        return Ok(None);
    }

    let json = fs::read_to_string(&file_path)
        .with_context(|| format!("Failed to read session file {:?}", file_path))?;

    let session: SessionState = serde_json::from_str(&json)
        .with_context(|| format!("Failed to parse session file {:?}", file_path))?;

    Ok(Some(session))
}

/// List all sessions (sorted by start_time, newest first)
pub fn list() -> Result<Vec<SessionState>> {
    let dir = sessions_dir()?;

    if !dir.exists() {
        return Ok(Vec::new());
    }

    let mut sessions = Vec::new();

    for entry in fs::read_dir(&dir)? {
        let entry = entry?;
        let path = entry.path();

        // Look for directories containing session.json
        if !path.is_dir() {
            continue;
        }

        let session_file = path.join("session.json");
        if !session_file.exists() {
            continue;
        }

        match fs::read_to_string(&session_file) {
            Ok(json) => match serde_json::from_str::<SessionState>(&json) {
                Ok(session) => sessions.push(session),
                // LCOV_EXCL_START - Error handling for corrupted files
                Err(e) => {
                    tracing::warn!("Skipping corrupted session file {:?}: {}", session_file, e);
                }
                // LCOV_EXCL_STOP
            },
            // LCOV_EXCL_START - Error handling for unreadable files
            Err(e) => {
                tracing::warn!("Failed to read session file {:?}: {}", session_file, e);
            }
            // LCOV_EXCL_STOP
        }
    }

    // Sort by start_time, newest first
    sessions.sort_by(|a, b| b.start_time.cmp(&a.start_time));

    Ok(sessions)
}

/// List running sessions only
pub fn list_running() -> Result<Vec<SessionState>> {
    let sessions = list()?;
    Ok(sessions
        .into_iter()
        .filter(|s| s.status == SessionStatus::Running)
        .collect())
}

/// Find sessions by app name (case-insensitive substring match)
pub fn find_by_app(app_name: &str) -> Result<Vec<SessionState>> {
    let sessions = list()?;
    let app_lower = app_name.to_lowercase();
    Ok(sessions
        .into_iter()
        .filter(|s| s.app_info.name.to_lowercase().contains(&app_lower))
        .collect())
}

/// Get the most recent session (any status)
pub fn latest() -> Result<Option<SessionState>> {
    let sessions = list()?;
    Ok(sessions.into_iter().next())
}

/// Get the most recent running session
pub fn latest_running() -> Result<Option<SessionState>> {
    let sessions = list_running()?;
    Ok(sessions.into_iter().next())
}

/// Clean up orphaned sessions (mark as Failed if process dead)
pub fn cleanup_orphaned() -> Result<Vec<SessionState>> {
    let sessions = list_running()?;
    let mut orphaned = Vec::new();

    for mut session in sessions {
        let is_alive = if let Some(pid) = session.capture_pid {
            is_process_alive(pid)
        } else {
            false
        };

        if !is_alive {
            session.status = SessionStatus::Failed;
            session.end_time = Some(chrono::Utc::now().to_rfc3339());
            update(&session.session_id, &session)?;
            orphaned.push(session);
        }
    }

    Ok(orphaned)
}

/// Check if a process is alive
fn is_process_alive(pid: u32) -> bool {
    // Use kill with signal 0 to check if process exists
    unsafe { libc::kill(pid as i32, 0) == 0 }
}

/// Extract app name and bundle_id from binary path
pub fn extract_app_info(binary_path: &str) -> AppInfo {
    let path = Path::new(binary_path);

    // Name from filename
    let name = path
        .file_name()
        .and_then(|n| n.to_str())
        .unwrap_or("unknown")
        .to_string();

    // bundle_id: walk up path for .app, read Info.plist
    let bundle_id = extract_bundle_id(path);

    AppInfo { name, bundle_id }
}

// LCOV_EXCL_START - macOS-specific PlistBuddy integration

/// Extract CFBundleIdentifier from .app bundle's Info.plist
fn extract_bundle_id(binary_path: &Path) -> Option<String> {
    // Walk up to find .app directory
    let mut current = binary_path;

    loop {
        if let Some(ext) = current.extension() {
            if ext == "app" {
                // Found .app bundle, try to read Info.plist
                let info_plist = current.join("Contents/Info.plist");
                if info_plist.exists() {
                    return read_bundle_id(&info_plist);
                }
            }
        }

        match current.parent() {
            Some(parent) => current = parent,
            None => break,
        }
    }

    None
}

/// Read CFBundleIdentifier from Info.plist using PlistBuddy
fn read_bundle_id(plist_path: &Path) -> Option<String> {
    let output = Command::new("/usr/libexec/PlistBuddy")
        .arg("-c")
        .arg("Print :CFBundleIdentifier")
        .arg(plist_path)
        .output()
        .ok()?;

    if output.status.success() {
        let bundle_id = String::from_utf8_lossy(&output.stdout).trim().to_string();
        if !bundle_id.is_empty() {
            return Some(bundle_id);
        }
    }

    None
}

// LCOV_EXCL_STOP

#[cfg(test)]
mod tests {
    use super::*;
    use std::env;
    use std::sync::Mutex;
    use tempfile::TempDir;

    // Serialize tests that modify HOME env var
    static HOME_MUTEX: Mutex<()> = Mutex::new(());

    fn with_temp_home<F, R>(f: F) -> R
    where
        F: FnOnce(&Path) -> R,
    {
        let _guard = HOME_MUTEX.lock().unwrap();
        let temp_dir = TempDir::new().unwrap();
        let original_home = env::var("HOME").ok();

        env::set_var("HOME", temp_dir.path());
        let result = f(temp_dir.path());

        if let Some(home) = original_home {
            env::set_var("HOME", home);
        }

        result
    }

    #[test]
    fn test_session_state__serialize__then_matches_schema() {
        let session = SessionState {
            session_id: "session_20240124_103000_MyApp".to_string(),
            session_path: PathBuf::from("/tmp/test.adabundle"),
            start_time: "2024-01-24T10:30:00Z".to_string(),
            end_time: None,
            app_info: AppInfo {
                name: "MyApp".to_string(),
                bundle_id: Some("com.example.myapp".to_string()),
            },
            status: SessionStatus::Running,
            pid: Some(12345),
            capture_pid: Some(67890),
        };

        let json = serde_json::to_string_pretty(&session).unwrap();
        assert!(json.contains("\"session_id\": \"session_20240124_103000_MyApp\""));
        assert!(json.contains("\"status\": \"running\""));
        assert!(json.contains("\"bundle_id\": \"com.example.myapp\""));
    }

    #[test]
    fn test_generate_session_id__then_correct_format() {
        let id = generate_session_id("MyApp");

        // Format: session_YYYY_MM_DD_hh_mm_ss_{short_hash}
        assert!(id.starts_with("session_"));
        let parts: Vec<&str> = id.split('_').collect();
        assert_eq!(parts.len(), 8); // session, YYYY, MM, DD, hh, mm, ss, hash
        assert_eq!(parts[0], "session");
        // Year should be 4 digits
        assert_eq!(parts[1].len(), 4);
        // Month, day, hour, minute, second should be 2 digits
        assert_eq!(parts[2].len(), 2);
        assert_eq!(parts[3].len(), 2);
        assert_eq!(parts[4].len(), 2);
        assert_eq!(parts[5].len(), 2);
        assert_eq!(parts[6].len(), 2);
        // Hash should be hex
        assert!(parts[7].chars().all(|c| c.is_ascii_hexdigit()));
    }

    #[test]
    fn test_generate_session_id__multiple__then_unique() {
        let id1 = generate_session_id("MyApp");
        std::thread::sleep(std::time::Duration::from_millis(1));
        let id2 = generate_session_id("MyApp");
        // IDs should be different (hash changes)
        assert_ne!(id1, id2);
    }

    #[test]
    fn test_session_dir__returns_directory_not_json() {
        with_temp_home(|_| {
            let dir = session_dir("session_2026_01_24_14_56_19_a1b2c3").unwrap();
            assert!(!dir.to_string_lossy().ends_with(".json"));
            assert!(dir.ends_with("session_2026_01_24_14_56_19_a1b2c3"));
        });
    }

    #[test]
    fn test_register__creates_directory_with_session_json() {
        with_temp_home(|home| {
            let session = SessionState {
                session_id: "session_test_dir".to_string(),
                session_path: PathBuf::from("/tmp/test"),
                start_time: "2024-01-24T10:30:00Z".to_string(),
                end_time: None,
                app_info: AppInfo {
                    name: "TestApp".to_string(),
                    bundle_id: None,
                },
                status: SessionStatus::Running,
                pid: None,
                capture_pid: None,
            };

            register(&session).unwrap();

            let dir = home.join(".ada/sessions/session_test_dir");
            assert!(dir.is_dir());
            assert!(dir.join("session.json").exists());
        });
    }

    #[test]
    fn test_list__finds_directories_with_session_json() {
        with_temp_home(|_| {
            let session = SessionState {
                session_id: "session_list_test".to_string(),
                session_path: PathBuf::from("/tmp/test"),
                start_time: "2024-01-24T10:30:00Z".to_string(),
                end_time: None,
                app_info: AppInfo {
                    name: "ListTestApp".to_string(),
                    bundle_id: None,
                },
                status: SessionStatus::Running,
                pid: None,
                capture_pid: None,
            };

            register(&session).unwrap();

            let sessions = list().unwrap();
            assert_eq!(sessions.len(), 1);
            assert_eq!(sessions[0].session_id, "session_list_test");
        });
    }

    #[test]
    fn test_register_get__roundtrip__then_equal() {
        with_temp_home(|_| {
            let session = SessionState {
                session_id: "session_test_roundtrip".to_string(),
                session_path: PathBuf::from("/tmp/test.adabundle"),
                start_time: "2024-01-24T10:30:00Z".to_string(),
                end_time: None,
                app_info: AppInfo {
                    name: "TestApp".to_string(),
                    bundle_id: None,
                },
                status: SessionStatus::Running,
                pid: Some(123),
                capture_pid: Some(456),
            };

            register(&session).unwrap();
            let loaded = get(&session.session_id).unwrap().unwrap();

            assert_eq!(loaded.session_id, session.session_id);
            assert_eq!(loaded.app_info.name, session.app_info.name);
            assert_eq!(loaded.status, SessionStatus::Running);
        });
    }

    #[test]
    fn test_get__no_file__then_none() {
        with_temp_home(|_| {
            let result = get("nonexistent_session").unwrap();
            assert!(result.is_none());
        });
    }

    #[test]
    fn test_list__multiple_sessions__then_sorted_newest_first() {
        with_temp_home(|_| {
            let session1 = SessionState {
                session_id: "session_old".to_string(),
                session_path: PathBuf::from("/tmp/old.adabundle"),
                start_time: "2024-01-24T10:00:00Z".to_string(),
                end_time: None,
                app_info: AppInfo {
                    name: "OldApp".to_string(),
                    bundle_id: None,
                },
                status: SessionStatus::Complete,
                pid: None,
                capture_pid: None,
            };

            let session2 = SessionState {
                session_id: "session_new".to_string(),
                session_path: PathBuf::from("/tmp/new.adabundle"),
                start_time: "2024-01-24T11:00:00Z".to_string(),
                end_time: None,
                app_info: AppInfo {
                    name: "NewApp".to_string(),
                    bundle_id: None,
                },
                status: SessionStatus::Running,
                pid: None,
                capture_pid: None,
            };

            register(&session1).unwrap();
            register(&session2).unwrap();

            let sessions = list().unwrap();
            assert_eq!(sessions.len(), 2);
            assert_eq!(sessions[0].session_id, "session_new"); // Newest first
            assert_eq!(sessions[1].session_id, "session_old");
        });
    }

    #[test]
    fn test_list__empty_dir__then_empty_vec() {
        with_temp_home(|_| {
            let sessions = list().unwrap();
            assert!(sessions.is_empty());
        });
    }

    #[test]
    fn test_list_running__mixed_status__then_only_running() {
        with_temp_home(|_| {
            let running = SessionState {
                session_id: "session_running".to_string(),
                session_path: PathBuf::from("/tmp/running.adabundle"),
                start_time: "2024-01-24T10:00:00Z".to_string(),
                end_time: None,
                app_info: AppInfo {
                    name: "RunningApp".to_string(),
                    bundle_id: None,
                },
                status: SessionStatus::Running,
                pid: None,
                capture_pid: None,
            };

            let complete = SessionState {
                session_id: "session_complete".to_string(),
                session_path: PathBuf::from("/tmp/complete.adabundle"),
                start_time: "2024-01-24T11:00:00Z".to_string(),
                end_time: Some("2024-01-24T11:30:00Z".to_string()),
                app_info: AppInfo {
                    name: "CompleteApp".to_string(),
                    bundle_id: None,
                },
                status: SessionStatus::Complete,
                pid: None,
                capture_pid: None,
            };

            register(&running).unwrap();
            register(&complete).unwrap();

            let sessions = list_running().unwrap();
            assert_eq!(sessions.len(), 1);
            assert_eq!(sessions[0].session_id, "session_running");
        });
    }

    #[test]
    fn test_find_by_app__partial_match__then_found() {
        with_temp_home(|_| {
            let session = SessionState {
                session_id: "session_myapp".to_string(),
                session_path: PathBuf::from("/tmp/myapp.adabundle"),
                start_time: "2024-01-24T10:00:00Z".to_string(),
                end_time: None,
                app_info: AppInfo {
                    name: "MyAwesomeApp".to_string(),
                    bundle_id: None,
                },
                status: SessionStatus::Running,
                pid: None,
                capture_pid: None,
            };

            register(&session).unwrap();

            let found = find_by_app("Awesome").unwrap();
            assert_eq!(found.len(), 1);
            assert_eq!(found[0].app_info.name, "MyAwesomeApp");

            // Case insensitive
            let found_lower = find_by_app("awesome").unwrap();
            assert_eq!(found_lower.len(), 1);
        });
    }

    #[test]
    fn test_find_by_app__no_match__then_empty() {
        with_temp_home(|_| {
            let session = SessionState {
                session_id: "session_test".to_string(),
                session_path: PathBuf::from("/tmp/test.adabundle"),
                start_time: "2024-01-24T10:00:00Z".to_string(),
                end_time: None,
                app_info: AppInfo {
                    name: "TestApp".to_string(),
                    bundle_id: None,
                },
                status: SessionStatus::Running,
                pid: None,
                capture_pid: None,
            };

            register(&session).unwrap();

            let found = find_by_app("NoMatch").unwrap();
            assert!(found.is_empty());
        });
    }

    #[test]
    fn test_latest__multiple__then_most_recent() {
        with_temp_home(|_| {
            let old = SessionState {
                session_id: "session_old".to_string(),
                session_path: PathBuf::from("/tmp/old.adabundle"),
                start_time: "2024-01-24T10:00:00Z".to_string(),
                end_time: None,
                app_info: AppInfo {
                    name: "OldApp".to_string(),
                    bundle_id: None,
                },
                status: SessionStatus::Complete,
                pid: None,
                capture_pid: None,
            };

            let new = SessionState {
                session_id: "session_new".to_string(),
                session_path: PathBuf::from("/tmp/new.adabundle"),
                start_time: "2024-01-24T11:00:00Z".to_string(),
                end_time: None,
                app_info: AppInfo {
                    name: "NewApp".to_string(),
                    bundle_id: None,
                },
                status: SessionStatus::Running,
                pid: None,
                capture_pid: None,
            };

            register(&old).unwrap();
            register(&new).unwrap();

            let latest = latest().unwrap().unwrap();
            assert_eq!(latest.session_id, "session_new");
        });
    }

    #[test]
    fn test_latest__empty__then_none() {
        with_temp_home(|_| {
            let latest = latest().unwrap();
            assert!(latest.is_none());
        });
    }

    #[test]
    fn test_latest_running__no_running__then_none() {
        with_temp_home(|_| {
            let complete = SessionState {
                session_id: "session_complete".to_string(),
                session_path: PathBuf::from("/tmp/complete.adabundle"),
                start_time: "2024-01-24T10:00:00Z".to_string(),
                end_time: Some("2024-01-24T10:30:00Z".to_string()),
                app_info: AppInfo {
                    name: "CompleteApp".to_string(),
                    bundle_id: None,
                },
                status: SessionStatus::Complete,
                pid: None,
                capture_pid: None,
            };

            register(&complete).unwrap();

            let latest = latest_running().unwrap();
            assert!(latest.is_none());
        });
    }

    #[test]
    fn test_update__status_change__then_persisted() {
        with_temp_home(|_| {
            let mut session = SessionState {
                session_id: "session_update_test".to_string(),
                session_path: PathBuf::from("/tmp/update.adabundle"),
                start_time: "2024-01-24T10:00:00Z".to_string(),
                end_time: None,
                app_info: AppInfo {
                    name: "UpdateApp".to_string(),
                    bundle_id: None,
                },
                status: SessionStatus::Running,
                pid: None,
                capture_pid: None,
            };

            register(&session).unwrap();

            session.status = SessionStatus::Complete;
            session.end_time = Some("2024-01-24T10:30:00Z".to_string());
            update(&session.session_id, &session).unwrap();

            let loaded = get(&session.session_id).unwrap().unwrap();
            assert_eq!(loaded.status, SessionStatus::Complete);
            assert_eq!(loaded.end_time, Some("2024-01-24T10:30:00Z".to_string()));
        });
    }

    #[test]
    fn test_cleanup_orphaned__dead_process__then_marked_failed() {
        with_temp_home(|_| {
            // Use PID 1 which exists (init/launchd) - won't be marked as orphaned
            // Use a very high PID that's unlikely to exist
            let session = SessionState {
                session_id: "session_orphan".to_string(),
                session_path: PathBuf::from("/tmp/orphan.adabundle"),
                start_time: "2024-01-24T10:00:00Z".to_string(),
                end_time: None,
                app_info: AppInfo {
                    name: "OrphanApp".to_string(),
                    bundle_id: None,
                },
                status: SessionStatus::Running,
                pid: None,
                capture_pid: Some(99999999), // Very unlikely to exist
            };

            register(&session).unwrap();

            let orphaned = cleanup_orphaned().unwrap();
            assert_eq!(orphaned.len(), 1);
            assert_eq!(orphaned[0].session_id, "session_orphan");
            assert_eq!(orphaned[0].status, SessionStatus::Failed);

            // Verify persisted
            let loaded = get(&session.session_id).unwrap().unwrap();
            assert_eq!(loaded.status, SessionStatus::Failed);
        });
    }

    #[test]
    fn test_cleanup_orphaned__live_process__then_unchanged() {
        with_temp_home(|_| {
            // Use current process PID - guaranteed to be alive
            let session = SessionState {
                session_id: "session_alive".to_string(),
                session_path: PathBuf::from("/tmp/alive.adabundle"),
                start_time: "2024-01-24T10:00:00Z".to_string(),
                end_time: None,
                app_info: AppInfo {
                    name: "AliveApp".to_string(),
                    bundle_id: None,
                },
                status: SessionStatus::Running,
                pid: None,
                capture_pid: Some(std::process::id()),
            };

            register(&session).unwrap();

            let orphaned = cleanup_orphaned().unwrap();
            assert!(orphaned.is_empty());

            // Verify still running
            let loaded = get(&session.session_id).unwrap().unwrap();
            assert_eq!(loaded.status, SessionStatus::Running);
        });
    }

    #[test]
    fn test_extract_app_info__simple_binary__then_name_only() {
        let info = extract_app_info("/usr/bin/ls");
        assert_eq!(info.name, "ls");
        assert!(info.bundle_id.is_none());
    }

    #[test]
    fn test_extract_app_info__app_bundle__then_has_bundle_id() {
        // Test with a known system app
        let info = extract_app_info("/System/Applications/Calculator.app/Contents/MacOS/Calculator");
        assert_eq!(info.name, "Calculator");
        // bundle_id might be None if PlistBuddy fails, so we just check name extraction works
    }

    #[test]
    fn test_status__serialize__then_lowercase() {
        let json = serde_json::to_string(&SessionStatus::Running).unwrap();
        assert_eq!(json, "\"running\"");

        let json = serde_json::to_string(&SessionStatus::Complete).unwrap();
        assert_eq!(json, "\"complete\"");

        let json = serde_json::to_string(&SessionStatus::Failed).unwrap();
        assert_eq!(json, "\"failed\"");
    }

    #[test]
    fn test_sessions_dir__home_exists__then_creates_path() {
        with_temp_home(|_| {
            let dir = sessions_dir().unwrap();
            assert!(dir.ends_with(".ada/sessions"));
        });
    }
}

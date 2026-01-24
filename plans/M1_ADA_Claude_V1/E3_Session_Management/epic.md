# E3: Session Management Layer

## Layer

Session lifecycle and state persistence

## Depends On

E1_Format_Adapter (uses `ada query` output)

## Design Decision: Multi-Session with Context-Based Memory

**Key Insight:** Claude Code's conversation context IS the session persistence layer.

When a capture starts, ADA CLI outputs structured session info to stdout:
```
ADA Session Started:
  ID: session_20240124_103000_MyApp
  App: MyApp (com.example.myapp)
  Bundle: /path/to/session.adabundle
  Time: 2024-01-24T10:30:00Z
```

This information:
1. Becomes part of Claude's conversation context
2. Survives Claude Code restarts (context is restored on resume)
3. Is findable via LLM attention when user asks about "that session"
4. No complex env var / process detection needed

## Architecture

```
                                     ┌──────────────────────┐
                                     │ Claude conversation  │
┌─────────────┐  outputs session     │ context (persisted)  │
│  /run skill │ ────────────────────►│                      │
└─────────────┘     info             └──────────────────────┘
      │                                        │
      │ ada capture start                      │ user resumes
      ▼                                        │ asks about session
┌─────────────┐                                ▼
│ ~/.ada/     │◄──── ada query <bundle> ───── Claude finds bundle
│ sessions/   │      ada session latest        path via attention
└─────────────┘
```

## Directory Structure

```
~/.ada/
├── sessions/
│   ├── session_20240124_103000_MyApp.json
│   ├── session_20240124_110500_OtherApp.json
│   └── ...
└── config.json                 # Future: user preferences
```

## Session State Schema

Location: `~/.ada/sessions/<session_id>.json`

```json
{
  "session_id": "session_20240124_103000_MyApp",
  "session_path": "/path/to/session.adabundle",
  "start_time": "2024-01-24T10:30:00Z",
  "end_time": "2024-01-24T10:35:00Z",
  "app_info": {
    "name": "MyApp",
    "bundle_id": "com.example.myapp"
  },
  "status": "running" | "complete" | "failed",
  "pid": 12345,
  "capture_pid": 67890
}
```

**Session ID format**: `session_<YYYYMMDD_HHMMSS>_<app_name>` (unique, sortable)

**Note:** No `claude_session_id` field - linking to Claude happens via conversation context, not stored state.

## CLI Commands

```bash
# List sessions
ada session list                  # All sessions (table format)
ada session list --running        # Running sessions only
ada session list --app MyApp      # Filter by app name
ada session list --format json    # JSON output for parsing

# Get latest session
ada session latest                # Print bundle path of most recent session
ada session latest --running      # Print bundle path of most recent running session

# Cleanup
ada session cleanup               # Mark dead sessions as Failed
```

## Responsibility Split

| Component | Responsibility |
|-----------|----------------|
| ADA CLI | Store session state in `~/.ada/sessions/`, provide `ada session list/latest/cleanup` |
| Skills | Output session info to context, pass bundle path to queries |
| Claude | Find session info in context via attention, use bundle path for queries |

## Concurrency Model

```
┌─────────────────┐                    ┌───────────────────────────┐
│  ada capture    │───── registers ───►│ ~/.ada/sessions/          │
│  (App A)        │                    │   session_..._AppA.json   │
└─────────────────┘                    └───────────────────────────┘

┌─────────────────┐                    ┌───────────────────────────┐
│  ada capture    │───── registers ───►│ ~/.ada/sessions/          │
│  (App B)        │                    │   session_..._AppB.json   │
└─────────────────┘                    └───────────────────────────┘
                                                   │
                    ┌──────────────────────────────┴──────────────────────────────┐
                    │ reads                        reads                          reads
                    ▼                              ▼                              ▼
          ┌─────────────────┐            ┌─────────────────┐            ┌─────────────────┐
          │ Claude Code #1  │            │ Claude Code #2  │            │ Claude Code #3  │
          │ queries App A   │            │ queries App B   │            │ queries both    │
          └─────────────────┘            └─────────────────┘            └─────────────────┘
```

**Key Points:**
- Multiple captures: Each app gets its own session file (different session_id)
- Multiple readers: Any Claude Code can list/read any session
- Atomic writes: Write to temp file + rename ensures readers never see partial data
- Session isolation: Each session file is independent - no conflicts

## Implementation

### Files Created
- `ada-cli/src/session_state.rs` - Session registry module

### Files Modified
- `ada-cli/Cargo.toml` - Added `chrono` dependency
- `ada-cli/src/main.rs` - Added `Session` command
- `ada-cli/src/capture.rs` - Integrated session registration

### Key Functions

```rust
pub fn sessions_dir() -> Result<PathBuf>;
pub fn generate_session_id(app_name: &str) -> String;
pub fn register(session: &SessionState) -> Result<()>;
pub fn update(session_id: &str, session: &SessionState) -> Result<()>;
pub fn get(session_id: &str) -> Result<Option<SessionState>>;
pub fn list() -> Result<Vec<SessionState>>;
pub fn list_running() -> Result<Vec<SessionState>>;
pub fn find_by_app(app_name: &str) -> Result<Vec<SessionState>>;
pub fn latest() -> Result<Option<SessionState>>;
pub fn latest_running() -> Result<Option<SessionState>>;
pub fn cleanup_orphaned() -> Result<Vec<SessionState>>;
pub fn extract_app_info(binary_path: &str) -> AppInfo;
```

### Capture Integration

On `ada capture start`:
1. Cleanup orphaned sessions
2. Extract app info from binary path
3. Generate unique session ID
4. Register session with Running status
5. Output session info to stdout (for Claude context)
6. Update session with target PID after spawn

On capture complete:
1. Update session status to Complete
2. Set end_time

On capture error:
1. Session remains Running (cleaned up by next `cleanup_orphaned`)

## Open Questions (Resolved)

- [x] Should ADA CLI write session state or should the skill do it?
  - **Answer:** ADA CLI writes to `~/.ada/sessions/`, outputs info for skills to capture in context
- [x] How to detect capture completion?
  - **Answer:** Session state file is updated on clean exit; orphaned sessions detected by checking if `capture_pid` is alive

## Status

**Complete** - Implemented with 20 unit tests passing

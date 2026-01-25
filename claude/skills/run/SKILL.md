---
name: run
description: "Launch application with ADA tracing - captures execution traces, voice narration, and screen recording"
---

# Run Application with ADA Capture

## Purpose

Launch an application with ADA tracing enabled, capturing execution traces, optional voice narration, and optional screen recording for later analysis.

## Workflow

### Step 1: Project Detection

Check for project files in current directory:
- `*.xcodeproj` or `*.xcworkspace` → Xcode project
- `Cargo.toml` → Rust/Cargo project
- `Package.swift` → Swift Package Manager
- User-specified binary path → Generic binary

### Step 2: Build (if applicable)

```bash
# For Cargo projects:
cargo build --release

# For Xcode projects:
xcodebuild -scheme <scheme> -configuration Release build

# For Swift Package:
swift build -c release
```

### Step 3: Start Capture

```bash
# Basic capture
ada capture start <binary_path>

# With voice narration
ada capture start <binary_path> --voice

# With screen recording
ada capture start <binary_path> --screen

# With both
ada capture start <binary_path> --voice --screen
```

### Step 4: Provide Feedback

Report to user:
- Session directory path
- Capture status
- How to stop: `ada capture stop`
- How to analyze: Use `ada:analyze` skill

## Error Handling

- **Build failure**: Show build errors, suggest fixes
- **Binary not found**: Guide user to specify path manually
- **Permission denied**: Check code signing (macOS)
- **ADA not installed**: Show installation instructions

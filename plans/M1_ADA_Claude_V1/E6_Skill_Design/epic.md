# E6: Skill Design

## Layer

Claude Code skills - plugin packaging and detailed workflow design

## Depends On

- E5_Skills (skill files and directory structure)

## Scope

### Plugin Packaging (This Epic)

This epic adds the plugin manifest files that enable ADA skills to be distributed and installed as a Claude Code plugin.

**Files to Create:**
- `claude/.claude-plugin/plugin.json` - Plugin manifest
- `claude/.claude-plugin/marketplace.json` - Marketplace metadata

**Plugin.json Structure:**
```json
{
  "name": "ada",
  "version": "0.1.0",
  "description": "ADA - AI-powered application debugging assistant",
  "skills": [
    {
      "name": "run",
      "path": "skills/run/SKILL.md"
    },
    {
      "name": "analyze",
      "path": "skills/analyze/SKILL.md"
    }
  ]
}
```

**Marketplace.json Structure:**
```json
{
  "displayName": "ADA Debugging Assistant",
  "author": "WeZZard",
  "repository": "https://github.com/WeZZard/project-ada",
  "license": "MIT",
  "keywords": ["debugging", "tracing", "analysis"]
}
```

### Workflow Design Documentation

#### /run Skill Design

**Project Detection:**
- Xcode projects: `*.xcodeproj`, `*.xcworkspace`
- Cargo projects: `Cargo.toml`
- Swift Package: `Package.swift`
- Generic binary: User specifies path

**Build Integration:**
- Detect if build needed (check modification times)
- Run appropriate build command
- Handle build errors gracefully

**Binary Discovery:**
- Xcode: Parse build output for product path
- Cargo: `target/release/<name>` or `target/debug/<name>`
- Swift: `.build/release/<name>`

**Capture Launch:**
- Background execution: `nohup ada capture start <binary> &`
- Optional flags: `--voice`, `--screen`
- Session path feedback to user

#### /analyze Skill Design

**Time Correlation Workflow:**
1. Get session time bounds from `ada query @latest time-info`
2. Get transcript segments with timestamps
3. Map user verbal descriptions to nanosecond ranges
4. Query events in correlated time windows

**Multimodal Synthesis:**
1. Extract screenshots at key timestamps
2. Collect trace events for each time window
3. Use Task tool for multimodal analysis (image + events)
4. Present unified diagnosis

**Diagnostic Output:**
- Summary of issue
- Timeline of relevant events
- Screenshot evidence
- Suggested fixes

### Error Handling

- No session: Guide user to run `/run` first
- No voice recording: Skip transcript, use events only
- No screen recording: Skip screenshots
- Missing tools: Show install instructions from capabilities

### User Interaction

- Confirm session selection if multiple available
- Ask for clarification if transcript is ambiguous
- Present findings incrementally

## Deliverables

1. `claude/.claude-plugin/plugin.json`
2. `claude/.claude-plugin/marketplace.json`
3. Updated `utils/deploy.sh` for plugin deployment
4. Documentation of interaction patterns

## Testing

1. Validate plugin.json schema
2. Manual validation with real capture sessions
3. Test skill invocation via Skill tool

## Status

Not started

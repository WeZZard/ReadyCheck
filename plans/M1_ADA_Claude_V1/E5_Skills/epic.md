# E5: Skills Layer

## Layer

Claude Code skills - user-facing commands

## Depends On

- E3_Session_Management (uses `ada session` commands and `@latest` path resolution)
- E4_Analysis_Pipeline (orchestrates analysis stages)

## Interface Contract

### Skill Files

Location in repo: `claude/commands/`
Deployed to: `~/.claude/commands/`

| Skill | Trigger | ADA Usage |
|-------|---------|-----------|
| `/run` | "run my app", "start the app" | `ada capture start` |
| `/analyze` | "why did it freeze?", "analyze" | `ada query events` + Whisper + ffmpeg |
| `/build` | "build my app" | None (build system only) |

### /run Skill

```markdown
# Detects project type, launches with capture
1. Find app binary (xcodebuild, cargo, etc.)
2. Launch: nohup ada capture start <binary> &
   (Session auto-registered in ~/.ada/sessions/<session_id>/)
3. Return: "App running with capture. Describe issues when ready."
```

**Note:** No `--output` flag needed - capture automatically registers session in `~/.ada/sessions/`.

### /analyze Skill

```markdown
# Runs E4 pipeline, presents diagnosis
1. Get latest session: ada session latest
   Or use @latest directly in queries: ada query @latest events
2. Run Whisper on voice.wav from session directory
3. Extract time points from transcript (LLM)
4. For each time point:
   - Extract screenshot (ffmpeg)
   - Query events in window: ada query @latest events --format line-complete
5. Synthesize with Task tool (multimodal)
6. Present diagnosis
```

**Note:** No `active_session.json` needed - use `@latest` in queries or `ada session latest` for the path.

### /build Skill

```markdown
# Standard build, no ADA
1. Detect build system
2. Run build command
3. Parse errors, suggest fixes
```

## Deployment

`utils/deploy.sh`:
```bash
mkdir -p ~/.claude/commands
cp claude/commands/*.md ~/.claude/commands/
```

## Deliverables

1. `claude/commands/run.md`
2. `claude/commands/analyze.md`
3. `claude/commands/build.md`
4. `utils/deploy.sh`

## Testing

Headless mode validation:
```bash
claude --print -p "/run" --allowedTools Bash,Read,Write
claude --print -p "/analyze" --allowedTools Bash,Read,Task
```

## Open Questions

- [ ] Natural language triggers - how to detect "it crashed" without explicit /analyze?
- [ ] Project type detection - heuristics for iOS vs macOS vs Rust?

## Status

Not started

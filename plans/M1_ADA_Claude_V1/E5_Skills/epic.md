# E5: Skills Layer

## Layer

Claude Code skills - user-facing commands and skill definitions

## Depends On

- E3_Session_Management (uses `ada session` commands and `@latest` path resolution)
- E4_Analysis_Pipeline (orchestrates analysis stages)

## Architecture

### Skill/Command Separation

ADA uses a two-tier approach:

1. **Skills** (`claude/skills/<name>/SKILL.md`) - Full workflow definitions
2. **Commands** (`claude/commands/<name>.md`) - Thin redirects to skills

This separation allows:
- Skills to be reusable across different invocation methods
- Commands to be simple entry points
- Future plugin distribution via `plugin.json` (see E6)

### Directory Structure

```
claude/
├── commands/
│   ├── run.md               # Thin redirect → ada:run skill
│   └── analyze.md           # Thin redirect → ada:analyze skill
└── skills/
    ├── run/
    │   └── SKILL.md         # Full run workflow
    └── analyze/
        └── SKILL.md         # Full analyze workflow
```

### SKILL.md Format

Skills use YAML frontmatter for metadata:

```yaml
---
name: skill-name
description: Brief description of what the skill does
tools: Read, Glob, Grep, Bash, Edit   # Optional: restrict available tools
---

# Title

Workflow content...
```

## Interface Contract

### Skills

| Skill | Description | ADA Usage |
|-------|-------------|-----------|
| `ada:run` | Launch app with tracing | `ada capture start` |
| `ada:analyze` | Analyze capture session | `ada query events` + Whisper + ffmpeg |

### ada:run Skill

1. Detects project type (Xcode, Cargo, Swift Package, generic binary)
2. Builds if needed
3. Launches: `ada capture start <binary>` with optional `--voice`/`--screen`
4. Reports session path

### ada:analyze Skill

1. Gets session info via `ada query @latest time-info`
2. Runs Whisper on voice recording (if available)
3. Extracts time points from transcript
4. For each time point:
   - Extracts screenshot (if available)
   - Queries events in window
5. Synthesizes multimodal analysis
6. Presents diagnosis

## Deployment

Skills are deployed via the plugin system (see E6 for plugin.json):

```bash
./utils/deploy.sh
```

## Deliverables

1. `claude/skills/run/SKILL.md` ✅
2. `claude/skills/analyze/SKILL.md` ✅
3. `claude/commands/run.md` (thin redirect) ✅
4. `claude/commands/analyze.md` (thin redirect) ✅

## Testing

Manual validation:
```bash
# Verify SKILL.md format
head -10 claude/skills/run/SKILL.md
head -10 claude/skills/analyze/SKILL.md
```

## Open Questions

- [ ] Natural language triggers - how to detect "it crashed" without explicit /analyze?
- [ ] Project type detection heuristics - prioritization when multiple build systems exist?

## Status

Complete - skill files created, commands redirect to skills

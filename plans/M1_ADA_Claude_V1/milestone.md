# M1: ADA + Claude Code V1 Integration

## Goal

Validate the hypothesis: "Multimodal capture (voice + screen + trace) improves AI-assisted debugging efficiency"

## Happy Path (V1 Minimal Flow)

```
┌─────────────────────────────────────────────────────────────────────────────┐
│ PHASE 1: CAPTURE                                                            │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  User: "run my app"                                                         │
│           ↓                                                                 │
│  /run skill detects project type                                            │
│           ↓                                                                 │
│  ada capture start <binary>                                                 │
│  (auto-registers in ~/.ada/sessions/<session_id>/)                          │
│           ↓                                                                 │
│  ┌─────────────────────────────────────────┐                               │
│  │ ADA captures simultaneously:            │                               │
│  │  • Function calls (trace)               │                               │
│  │  • Screen recording (video)             │                               │
│  │  • Voice recording (audio)              │                               │
│  └─────────────────────────────────────────┘                               │
│           ↓                                                                 │
│  User interacts with app, speaks: "I'm tapping login... it froze"          │
│           ↓                                                                 │
│  User stops capture (Ctrl+C)                                                │
│           ↓                                                                 │
│  Session dir IS the bundle: ~/.ada/sessions/<session_id>/                   │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
                                    ↓
┌─────────────────────────────────────────────────────────────────────────────┐
│ PHASE 2: ANALYSIS                                                           │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  User: "why did it freeze?"                                                 │
│           ↓                                                                 │
│  /analyze skill uses @latest or ada session latest                          │
│           ↓                                                                 │
│  whisper voice.wav → timestamped transcript                                 │
│  "at 30 seconds I tapped login and it froze"                               │
│           ↓                                                                 │
│  Claude extracts: time=30s, observation="tapped login, froze"              │
│           ↓                                                                 │
│  ada query @latest events --format line-complete                            │
│  → Events around T=30s (all threads)                                        │
│           ↓                                                                 │
│  ffmpeg -ss 30 -i screen.mp4 -frames:v 1 screenshot.png                    │
│  → Screenshot at T=30s                                                      │
│           ↓                                                                 │
│  Claude synthesizes: screenshot + events + user description                 │
│           ↓                                                                 │
│  "The freeze occurred because NetworkManager.sendRequest was called        │
│   on the main thread, blocking the UI for 2.3 seconds..."                  │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

## Required ADA Commands (derived from happy path)

### Phase 1: Capture

| Command | Purpose | Output |
|---------|---------|--------|
| `ada capture start <binary>` | Start multimodal capture | Runs until Ctrl+C |
| (implicit) | Register session | `~/.ada/sessions/<session_id>/` directory |
| (implicit) | Write session metadata | `session.json` with session state |

### Phase 2: Analysis

| Command | Purpose | Output |
|---------|---------|--------|
| `ada session latest` | Get latest session path | `~/.ada/sessions/<session_id>/` |
| `ada query @latest summary` | Session overview | Thread count, event count, duration |
| `ada query @latest events` | Get trace events | Timestamped function calls |
| `ada query @latest events --format line-complete` | LLM-friendly format | `cycle=... \| T=... \| thread:... \| func()` |

**Note:** `@latest` resolves to the most recent session. You can also use a session ID or direct path.

### External Tools (not ADA)

| Tool | Purpose | Command |
|------|---------|---------|
| Whisper | Speech-to-text | `whisper voice.wav --model base --output_format json` |
| ffmpeg | Screenshot extraction | `ffmpeg -ss <time> -i screen.mp4 -frames:v 1 out.png` |

## Epics

| Epic | Purpose |
|------|---------|
| E1_Happy_Path_Spike | Run happy path manually, discover what works/breaks |
| E2_Format_Adapter | (Scope TBD after E1) |
| E3_Session_Management | (Scope TBD after E1) |
| E4_Analysis_Pipeline | (Scope TBD after E1) |
| E5_Skills | (Scope TBD after E1) |
| E6_Skill_Design | Detailed skill workflow design (/run, /analyze) |
| E7_Skill_Distribution | Marketplace and plugin distribution system |

**Note**: E2-E5 scopes will be refined based on E1 findings. E6-E7 extend the skills infrastructure.

## Execution Order

```
E1_Happy_Path_Spike
    ↓ findings inform
E2-E5 (scoped based on what E1 reveals)
```

## Success Criteria

- [ ] Happy path works end-to-end with a real app
- [ ] User can describe issues naturally, Claude correlates to code
- [ ] Time alignment between voice, screen, and trace is accurate

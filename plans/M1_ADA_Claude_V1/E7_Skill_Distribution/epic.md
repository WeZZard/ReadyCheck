# E7: Skill Distribution

## Layer

Claude Code skills - plugin distribution and marketplace integration

## Depends On

- E5_Skills (skill files and directory structure)
- E6_Skill_Design (plugin.json and marketplace.json)

## Scope

### Concept

Distribution of ADA skills as a Claude Code plugin, leveraging the built-in plugin system rather than a custom `ada skill` CLI.

### Distribution Model

**Primary Distribution: Claude Code Plugin System**

Instead of building custom `ada skill install/update/remove` commands, ADA skills are distributed through Claude Code's native plugin system:

1. Users install via Claude Code's plugin mechanism
2. Skills become available through the Skill tool
3. Updates handled by Claude Code's plugin update system

**Plugin Location:**
```
~/.claude/plugins/cache/ada/<version>/
├── plugin.json
├── marketplace.json
└── skills/
    ├── run/SKILL.md
    └── analyze/SKILL.md
```

### Installation Flow

1. User adds ADA plugin to their Claude Code configuration
2. Claude Code downloads plugin from registry/GitHub
3. Skills become available as `ada:run`, `ada:analyze`
4. Commands in `~/.claude/commands/` redirect to skills

### Publishing Flow

1. Bump version in `plugin.json`
2. Create GitHub release with tag
3. Update any registry entries (if using Claude Code marketplace)
4. Users receive updates through Claude Code's update mechanism

### Local Development

For local development and testing:

```bash
# Deploy locally without publishing
./utils/deploy.sh --local

# This copies skills to ~/.claude/plugins/local/ada/
```

### Security Considerations

- Skills are markdown/templates, not executable code
- Claude Code validates plugin manifest
- User consent through Claude Code's permission system

## Deliverables

1. Distribution documentation (README section)
2. Publishing workflow (GitHub Actions or manual)
3. Local development deployment script updates
4. User installation instructions

## Relationship to Original E7 Plan

The original plan proposed custom `ada skill` CLI commands:
- `ada skill list`
- `ada skill install`
- `ada skill update`
- `ada skill remove`
- `ada skill publish`

**Decision:** These are **not needed** because Claude Code's plugin system provides equivalent functionality. This simplifies ADA by:
- Removing need for skill registry infrastructure
- Leveraging proven plugin distribution mechanism
- Reducing CLI surface area

## Testing

- Local deployment validation
- GitHub release workflow test
- User installation path verification

## Status

Not started

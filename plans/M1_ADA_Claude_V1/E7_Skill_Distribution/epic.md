# E7: Skill Distribution

## Layer

Claude Code skills - marketplace and plugin distribution system

## Depends On

- E5_Skills (infrastructure)
- E6_Skill_Design (detailed skill workflows)

## Scope

### Concept

A marketplace/registry system where:
1. ADA skills are packaged as installable plugins
2. Users can discover, install, and update skills
3. Skill authors can publish and version their skills

### Plugin Format

**Skill Manifest (`skill.json`):**
```json
{
  "name": "my-skill",
  "version": "1.0.0",
  "description": "Brief description of the skill",
  "author": "author-name",
  "license": "MIT",
  "ada_version": ">=0.1.0",
  "dependencies": [],
  "files": ["skill.md", "templates/"],
  "keywords": ["category", "tag"]
}
```

**Package Structure:**
```
my-skill/
├── skill.json       # Manifest with metadata
├── skill.md         # Main skill file
├── README.md        # Documentation (optional)
└── templates/       # Supporting files (optional)
```

**Versioning:**
- Semantic versioning (semver): MAJOR.MINOR.PATCH
- Compatibility declarations for ADA versions

### Registry/Marketplace

**Discovery Mechanism:**
- Searchable index by name, description, keywords
- Category browsing (debugging, testing, deployment, etc.)
- Popularity/rating metrics

**Registry Options:**
1. **GitHub-based**: Skills as repos with releases
   - Pro: No infrastructure needed, familiar workflow
   - Con: No central search, fragmented discovery

2. **npm-style registry**: Dedicated service
   - Pro: Unified search, metadata API
   - Con: Infrastructure to maintain

3. **Git index file**: Central index pointing to git repos
   - Pro: Simple, decentralized hosting
   - Con: Manual index maintenance

**Recommended Approach:**
Start with GitHub-based distribution using a central index file (`skill-registry.json`) that references GitHub repos.

### CLI Commands

**List installed skills:**
```bash
ada skill list
ada skill list --available    # Show installable skills
```

**Install from marketplace:**
```bash
ada skill install <name>
ada skill install <name>@<version>
ada skill install <github-url>
```

**Update skills:**
```bash
ada skill update              # Update all skills
ada skill update <name>       # Update specific skill
```

**Remove skills:**
```bash
ada skill remove <name>
```

**Publish to marketplace:**
```bash
ada skill publish             # Publish from current directory
ada skill publish --dry-run   # Validate without publishing
```

**Skill information:**
```bash
ada skill info <name>         # Show skill details
ada skill search <query>      # Search marketplace
```

### Installation Flow

1. Resolve skill name to registry entry
2. Download skill package (git clone or tarball)
3. Validate manifest and compatibility
4. Install to `~/.ada/skills/<name>/`
5. Register in local skill index

### Publishing Flow

1. Validate `skill.json` manifest
2. Check required files exist
3. Run skill linter/validator
4. Tag git release (if GitHub-based)
5. Update central registry index (PR or API)

### Local Skill Directory Structure

```
~/.ada/
├── skills/
│   ├── my-skill/           # Installed skill
│   │   ├── skill.json
│   │   └── skill.md
│   └── another-skill/
├── skill-index.json        # Local registry cache
└── config.json             # User preferences
```

### Security Considerations

- Skills are markdown/templates, not executable code
- Manifest validation before installation
- Optional signature verification for trusted publishers
- User consent before installation

## Deliverables

1. Plugin format specification
2. `ada skill` CLI subcommands
3. Registry index format
4. Installation/publishing workflows
5. Documentation for skill authors

## Testing

- Unit tests for manifest parsing
- Integration tests for install/remove/update
- Mock registry for CI testing

## Status

Not started

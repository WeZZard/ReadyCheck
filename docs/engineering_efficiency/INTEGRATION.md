# INTEGRATION

## When This Applies
You are in INTEGRATION stage when:
- Merging components
- Preparing commits
- Creating pull requests
- Running quality gates

## MANDATORY Rules for Integration

### 1. Pre-Commit Quality Gates

#### Automatic Enforcement (via .git/hooks/pre-commit)
The quality gate runs automatically on commit and checks:

**Critical Metrics (Must Pass 100%)**:
- Build Success: All components build without errors
- Test Success: All tests pass
- Incremental Coverage: 100% on changed lines
- No compiled binaries in git
- No secrets/credentials in code

**Integration Score Calculation**:
```
Score = (Build * 25) + (Tests * 25) + (Coverage * 25) + (NoRegressions * 25)
```
Must achieve 100/100 to commit.

#### Manual Quality Check
```bash
# Run full quality gate
./utils/quality_gate.sh

# Check incremental coverage only
./utils/quality_gate.sh --incremental
```

### 2. Commit Process

#### Creating Commits
```bash
# Stage changes
git add -A

# Commit with descriptive message
git commit -m "component: Clear description

Detailed explanation if needed

🤖 Generated with [Claude Code](https://claude.ai/code)

Co-Authored-By: Claude <noreply@anthropic.com>"
```

#### Commit Message Format
```
<type>: <description>

[optional body]

[optional footer]
```

Types: feat, fix, docs, test, refactor, perf, style, chore

### 3. Pull Request Requirements

When creating PR:
```bash
# Ensure current branch tracks remote
git push -u origin branch-name

# Create PR with details
gh pr create --title "title" --body "$(cat <<'EOF'
## Summary
- What changed
- Why it changed

## Test Plan
- How to test
- What to verify

🤖 Generated with [Claude Code]
EOF
)"
```

### 4. Integration Checklist

Before marking integration complete:

#### Code Quality
- [ ] No compiler warnings
- [ ] No linter errors  
- [ ] Follows coding standards
- [ ] Has debug utilities

#### Documentation
- [ ] API documented
- [ ] Complex logic explained
- [ ] README updated if needed
- [ ] CHANGELOG updated

#### Testing
- [ ] Unit tests passing
- [ ] Integration tests passing
- [ ] Coverage ≥100% on changes
- [ ] Performance benchmarks run

#### Build System
- [ ] Builds through Cargo
- [ ] Tests in build.rs (if C/C++)
- [ ] No direct CMake usage
- [ ] Artifacts in correct locations

### 5. Common Integration Issues

#### Issue: Tests not found
**Cause**: Test not added to build.rs
**Fix**: Add to binaries list in build.rs

#### Issue: Coverage fails
**Cause**: New code not tested
**Fix**: Add tests for all new functions

#### Issue: Build fails in CI
**Cause**: Local environment differences
**Fix**: Clean build: `cargo clean && cargo build`

## Quality Gate Override (EMERGENCY ONLY)

⚠️ **WARNING**: Only use in critical situations!

```bash
# Bypass pre-commit hook (DO NOT USE)
git commit --no-verify -m "message"
```

If you must bypass:
1. Document WHY in commit message
2. Create immediate follow-up task
3. Fix in next commit

## Integration Metrics Dashboard

After successful integration:
- View metrics: `open target/coverage_report/dashboard.html`
- Integration report: `target/quality_gate_report.json`
- Trend analysis: `target/metrics/trends.json`

## Platform-Specific CI/CD Setup

### macOS Code Signing for CI
ADA requires Apple Developer certificates for tracing:

```bash
# Export certificate locally
security find-identity -v -p codesigning
security export -k ~/Library/Keychains/login.keychain-db \
  -t identities -f pkcs12 -o cert.p12

# Convert for GitHub secrets
base64 -i cert.p12 | pbcopy
```

Add to GitHub secrets:
- `APPLE_CERTIFICATE`: Base64 .p12 content
- `APPLE_CERTIFICATE_PASSWORD`: .p12 password

## Red Flags
If you're:
- Committing without running tests
- Pushing with <100% coverage on changes
- Creating PRs without test plans  
- Skipping quality gates
- Using --no-verify without emergency

STOP! You're violating integration requirements.
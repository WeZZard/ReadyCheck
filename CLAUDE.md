# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## MANDATORY: Development Philosophy

### Core Value (BUILD) vs Engineering Efficiency (USE EXISTING)

**BUILD - Focus Engineering Effort on Here:**
**Core components found at the root directory** like:
- **tracer** (Rust): Dual-lane flight recorder control plane
- **tracer_backend** (C/C++): Native tracer backend.
- **query_engine** (Python): Token-budget-aware analysis
- **mcp_server** (Python): Model Context Protocol interface
- **ATF format**: Custom trace format

These are ADA's innovation. Build carefully with focus on performance and correctness.

**USE EXISTING - Never Build These:**
**Engineering efficiency components** like:
- Coverage tools → Use `diff-cover`, `lcov`, `genhtml`
- Testing frameworks → Use Google Test, pytest, cargo test
- Linting/formatting → Use clippy, black, clang-format
- CI/CD → Use GitHub Actions
- Documentation generators → Use mdBook, Sphinx

**Decision Rule**: If it's not part of the ADA system shipped to the user, prefer to use an existing solution firstly.

## MANDATORY: Developer Requirements

### macOS Development
- **Apple Developer Certificate ($99/year)** - NO EXCEPTIONS
- Required for ALL testing and development
- See `docs/GETTING_STARTED.md#platform-specific-requirements`
- Without this: Quality gate fails, tests cannot run

## MANDATORY: Project Structure

Core components and critical directories:

```plaintext
project-root/
├── Cargo.toml                     # CRITICAL: Root workspace manifest - orchestrates ALL builds
├── docs/
│   ├── business/                  # Business analysis
│   ├── user_stories/              # User stories  
│   ├── specs/                     # Technical specifications
│   ├── technical_insights/        # Technical insights
│   ├── engineering_efficiency/    # Standards and tooling
│   └── progress_trackings/        # CRITICAL FOR PLANNERS: Development workflow artifacts
│       └── M{X}_{MILESTONE_NAME}/           # Milestone folders (X = milestone number)
│           ├── M{X}_{MILESTONE_NAME}.md     # Milestone target document
│           └── M{X}_E{Y}_{EPIC_NAME}/       # Epic folders (Y = epic number)
│               ├── M{X}_E{Y}_{EPIC_NAME}.md # Epic target document
│               └── M{X}_E{Y}_I{Z}_{ITERATION_NAME}/ # Iteration folders (Z = iteration number)
│                   ├── M{X}_E{Y}_I{Z}_TECH_DESIGN.md
│                   ├── M{X}_E{Y}_I{Z}_TEST_PLAN.md
│                   └── M{X}_E{Y}_I{Z}_BACKLOGS.md
│
├── tracer/                        # Rust tracer (control plane)
│   └── Cargo.toml                # Component manifest
├── tracer_backend/               # C/C++ backend (data plane)
│   ├── Cargo.toml                # CRITICAL: Rust manifest that orchestrates CMake
│   ├── build.rs                  # CRITICAL: Invokes CMake via cmake crate
│   └── CMakeLists.txt            # CMake config (invoked by build.rs, NEVER directly)
├── query_engine/                 # Python query engine
│   ├── Cargo.toml                # Rust manifest for Python binding
│   └── pyproject.toml            # Python config (built via maturin)
├── mcp_server/                   # Python MCP server
│   ├── Cargo.toml                # Rust manifest (if using maturin)
│   └── pyproject.toml            # Python config
├── utils/                        # Engineering efficiency scripts
├── third_parties/               # Frida SDK and dependencies
└── target/                      # Build outputs (git-ignored)
```

**Rules:**
1. Component directories use snake_case
2. Never commit binaries to git
3. All outputs go to target/
4. Progress tracking documents are MANDATORY for planning
5. ALL builds go through root Cargo.toml - NEVER run CMake/pytest directly

## 🔴 CRITICAL: Development Stage Rules

**YOU MUST FOLLOW THE RULES FOR YOUR CURRENT STAGE:**

- **[PLANNING](docs/engineering_efficiency/PLANNING.md)** - Requirements, design, interfaces
- **[ARCHITECTING](docs/engineering_efficiency/ARCHITECTING.md)** - System design, technical decisions
- **[CODING](docs/engineering_efficiency/CODING.md)** - Implementation (INCLUDES TEST ORCHESTRATION!)
- **[TESTING](docs/engineering_efficiency/TESTING.md)** - Testing, coverage, quality gates
- **[INTEGRATION](docs/engineering_efficiency/INTEGRATION.md)** - Commits, PRs, CI/CD
- **[DEBUGGING](docs/engineering_efficiency/DEBUGGING.md)** - Debugging to pay attention.

⚠️ **MOST COMMON VIOLATION**: Forgetting to add tests to `build.rs` when adding to CMakeLists.txt

## MANDATORY: Build System

**CARGO IS THE SINGLE DRIVER - NO EXCEPTIONS**

```bash
cargo build --release           # Builds everything
cargo test --all               # Runs all tests
./utils/run_coverage.sh        # Coverage with existing tools
```

NEVER:
- Run CMake directly
- Run pytest outside of Cargo orchestration
- Create custom build scripts

**⚠️ CRITICAL GOTCHA - TESTS NEED TWO PLACES:**
When adding C/C++ tests:
1. ✅ CMakeLists.txt: `add_executable(test_xyz ...)`  
2. ✅ **build.rs: `("build/test_xyz", "test/test_xyz")`** ← EASY TO FORGET!

Missing step 2 = test won't be accessible via Cargo!

## MANDATORY: Quality Requirements

**100% Mandatory - No Exceptions:**
1. Build Success: ALL components must build
2. Test Success: ALL tests must pass
3. Test Coverage: 100% coverage on changed lines
4. Integration Score: 100/100

**No Bypass Mechanisms:**
- NO `git commit --no-verify`
- NO ignoring failing tests
- NO reducing coverage requirements
- NO "temporary" workarounds

## Development Model

### Iteration Workflow (MANDATORY for planners)

Each iteration follows this TDD-based workflow:

1. **Plan** - Create Tech Design, Test Plan, Backlogs
2. **Prepare** - Create minimal compilable skeletons
3. **Specify** - Write failing unit tests first
4. **Build** - Implement until tests pass (TDD)
5. **Verify** - Module integration tests
6. **Validate** - System integration tests
7. **Accept** - User story validation
8. **Prove** - Performance benchmarks
9. **Close** - 100% coverage, docs updated

All artifacts go in: `docs/progress_trackings/M{X}_*/M{X}_E{Y}_*/M{X}_E{Y}_I{Z}_*/`
Where: X = milestone number, Y = epic number, Z = iteration number

## Key Documentation References

**Essential documents** - READ these when referenced:

- **Setup/Build**: [`docs/GETTING_STARTED.md`](docs/GETTING_STARTED.md)
- **Architecture**: [`docs/specs/ARCHITECTURE.md`](docs/specs/ARCHITECTURE.md)
- **Standards**: [`docs/engineering_efficiency/ENGINEERING_STANDARDS.md`](docs/engineering_efficiency/ENGINEERING_STANDARDS.md)
- **Quality Gates**: [`docs/engineering_efficiency/QUALITY_GATE_IMPLEMENTATION.md`](docs/engineering_efficiency/QUALITY_GATE_IMPLEMENTATION.md)
- **Coverage Analysis**: [`docs/engineering_efficiency/COVERAGE_SYSTEM_ANALYSIS_AND_REQUIREMENTS.md`](docs/engineering_efficiency/COVERAGE_SYSTEM_ANALYSIS_AND_REQUIREMENTS.md)

**Additional references**:
- Business analysis: `docs/business/`
- User stories: `docs/user_stories/`
- Technical insights: `docs/technical_insights/`
- Progress tracking: `docs/progress_trackings/`

Do NOT duplicate content from these documents - reference them.

## Test Naming Convention

Use behavioral naming for all tests:
```
<unit>__<condition>__then_<expected>
```
Example: `ring_buffer__overflow__then_wraps_around`

## Important Constraints

1. **C/C++ Testing**: MUST use Google Test framework
2. **Coverage**: MUST use diff-cover for enforcement
3. **Python**: Built via maturin, orchestrated by Cargo
4. **Frida**: Initialize with `./utils/init_third_parties.sh`
5. **Platform Security**: Platform-specific requirements for tracing
   - macOS: Run `./utils/sign_binary.sh <path>` for SSH/CI tracing
   - Linux: May need ptrace capabilities
   - See `docs/specs/PLATFORM_SECURITY_REQUIREMENTS.md`

## Focus Areas

When working on this project:
1. **Prioritize** core tracing components (tracer, backend, query engine)
2. **Reuse** existing tools for everything else
3. **Maintain** 100% quality gates
4. **Follow** existing patterns in the codebase

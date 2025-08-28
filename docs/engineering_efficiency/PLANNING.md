# PLANNING

## Development Philosophy

### BUILD vs BUY/REUSE Decision

**BUILD (Core Innovation)**:
Focus engineering effort on ADA's unique value:
- Tracer dual-lane architecture
- Native agent with Frida hooks
- Token-budget-aware query engine  
- MCP protocol implementation
- Custom ATF format

**BUY/REUSE (Engineering Efficiency)**:
Use existing mature tools for everything else:
- Coverage tools (lcov, diff-cover, genhtml)
- Testing frameworks (Google Test, pytest)
- Linting/formatting (clippy, black, clang-format)
- CI/CD (GitHub Actions)
- Documentation (mdBook, Sphinx)

**Decision Rule**: If it's not part of the tracing/analysis pipeline, use an existing solution.

## When This Applies
You are in PLANNING stage when:
- Creating iteration tech designs
- Writing test plans  
- Defining interfaces
- Creating backlogs

## MANDATORY Rules for Planning

### 1. Document Structure
- Tech design MUST exist at: `docs/progress_trackings/M*_*/M*_E*_*/M*_E*_I*_TECH_DESIGN.md`
- Test plan MUST exist at: `docs/progress_trackings/M*_*/M*_E*_*/M*_E*_I*_TEST_PLAN.md`
- Backlogs MUST exist at: `docs/progress_trackings/M*_*/M*_E*_*/M*_E*_I*_BACKLOGS.md`

### 2. Interface Definition
Before ANY code:
- C/C++: Create complete header files (.h)
- Rust: Define traits and types
- Python: Create Protocol/ABC definitions

### 3. Architect Role
If complex iteration (multiple components):
- Spawn architect agent FIRST
- Architect creates interface blueprints
- Developer and Tester receive IDENTICAL interfaces

## Examples

❌ **WRONG**: Jump straight to implementation
✅ **RIGHT**: Plan → Interface → Implement

## Red Flags
If you're writing code without:
- A tech design document
- Interface definitions
- Test plan

STOP! You're violating planning requirements.
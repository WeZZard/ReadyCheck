# TESTING

## When This Applies
You are in TESTING stage when:
- Writing unit tests
- Creating integration tests
- Running test suites
- Debugging test failures

## MANDATORY Rules for Testing

### 1. Test Orchestration
**ALL tests run through Cargo - NO EXCEPTIONS**

```bash
cargo test                    # Run all tests
cargo test --all             # Include all workspace members
cargo test thread_registry   # Run specific test
```

**NEVER** run tests directly:
```bash
# WRONG - DO NOT DO THIS
./tracer_backend/build/test_something        # ❌
cd tracer_backend && ctest                   # ❌  
python -m pytest                             # ❌
```

### 2. Test Naming Convention
All tests use behavioral naming:
```
<unit>__<condition>__then_<expected>
```

Examples:
- `registry__single_thread__then_succeeds`
- `ring_buffer__overflow__then_wraps`
- `lane__concurrent_access__then_thread_safe`

### 3. Test Location Rules

Tests must exist in THREE places:

1. **Source definition**:
   - C/C++: `tests/unit/` or `tests/integration/`
   - Rust: `src/lib.rs` with `#[cfg(test)]`
   - Python: `tests/` directory

2. **Build definition** (C/C++ only):
   - CMakeLists.txt: `add_executable(test_name ...)`
   
3. **Orchestration** (C/C++ only):
   - build.rs: `("build/test_name", "test/test_name")`

### 4. Coverage Requirements

#### MANDATORY (Blocking)
- **100% coverage on changed lines** (incremental coverage)
- Build Success: 100% across all languages
- Test Success: 100% (all tests must pass)
- No Regressions: No new failures vs baseline

#### Run Coverage
```bash
# Full coverage report
./utils/run_coverage.sh

# Incremental coverage (changed files only)
./utils/quality_gate.sh --incremental
```

#### Coverage Reports Location
- HTML: `target/coverage_report/index.html`
- LCOV: `target/coverage_report/lcov.info`
- By language:
  - Rust: `target/coverage_report/rust/`
  - C/C++: `target/coverage_report/cpp/`
  - Python: `target/coverage_report/python/`

### 5. Test Types

#### Unit Tests
- Test single components in isolation
- Mock external dependencies
- Fast execution (<1ms per test)

#### Integration Tests  
- Test component interactions
- Use real implementations
- May use shared memory

#### Performance Tests
- Measure latency/throughput
- Compare against requirements
- Document baseline metrics

## Debugging Test Failures

### For C/C++ Tests
```bash
# Find test binary
ls target/release/tracer_backend/test/

# Run with gtest filters
./target/release/tracer_backend/test/test_name --gtest_filter="*pattern*"

# Debug with lldb
lldb ./target/release/tracer_backend/test/test_name
```

### For Rust Tests
```bash
# Run with output
cargo test -- --nocapture

# Run specific test
cargo test test_name -- --exact
```

## Troubleshooting Common Issues

### Tool Installation
```bash
# cargo-llvm-cov not found
cargo install cargo-llvm-cov

# lcov/genhtml not found (macOS)
brew install lcov

# Python coverage tools
pip install coverage coverage-lcov pytest-cov

# LLVM tools for C/C++ coverage  
rustup component add llvm-tools-preview
```

### Quality Gate Failures
- **Build fails**: Check `cargo build --release` output
- **Tests fail**: Run failing test individually with filters
- **Coverage <100%**: Check `target/coverage_report/` for uncovered lines
- **Integration score <100**: Review critical metrics in report

## Red Flags
If you're:
- Running tests from CMake build directory
- Not updating build.rs for new tests
- Using pytest directly
- Skipping coverage requirements
- Ignoring quality gate failures

STOP! You're violating testing requirements.
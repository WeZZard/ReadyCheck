#!/bin/bash
# Run full coverage collection and reporting

set -e

WORKSPACE_ROOT=$(git rev-parse --show-toplevel)
cd "$WORKSPACE_ROOT"

echo "=== ADA Coverage Collection ==="
echo

# Clean previous coverage data
echo "Cleaning previous coverage data..."
rm -rf target/coverage/*.profraw
rm -rf target/coverage_report

# Build with coverage
echo "Building with coverage instrumentation..."
cargo build --release --features coverage -p tracer_backend

# Run C++ tests with coverage
echo "Running C++ tests with coverage..."
export LLVM_PROFILE_FILE="$WORKSPACE_ROOT/target/coverage/cpp-%p-%m.profraw"
for test in target/release/tracer_backend/test/test_*; do
    if [ -x "$test" ]; then
        echo "  Running $(basename $test)..."
        "$test" > /dev/null 2>&1 || true
    fi
done

# Run Rust tests with coverage (if needed)
echo "Running Rust tests with coverage..."
export LLVM_PROFILE_FILE="$WORKSPACE_ROOT/target/coverage/rust-%p-%m.profraw"
cargo test --release || true

# Collect coverage
echo
echo "Collecting coverage data..."
"$WORKSPACE_ROOT/target/release/coverage_helper" collect

# Generate HTML report
echo
echo "Generating HTML report..."
"$WORKSPACE_ROOT/target/release/coverage_helper" report --format html

# Run diff-cover
echo
echo "Checking coverage on changed lines..."
if which diff-cover > /dev/null 2>&1; then
    diff-cover "$WORKSPACE_ROOT/target/coverage_report/merged.lcov" \
               --compare-branch=main \
               --fail-under=100 || {
        echo "Warning: Some changed lines lack coverage"
    }
else
    echo "diff-cover not installed, skipping changed lines check"
fi

echo
echo "=== Coverage Collection Complete ==="
echo "Dashboard: file://$WORKSPACE_ROOT/target/coverage_report/index.html"
echo "Full Report: file://$WORKSPACE_ROOT/target/coverage_report/full/index.html"
#!/usr/bin/env bash
# run_e2e_bench.sh - Shell wrapper for running E2E benchmarks
# Usage: ./utils/run_e2e_bench.sh [--ci]

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Parse arguments
CI_MODE=false
for arg in "$@"; do
    case "$arg" in
        --ci) CI_MODE=true ;;
        *) echo "Unknown argument: $arg"; exit 1 ;;
    esac
done

echo "=== ADA E2E Benchmark Suite ==="
echo "Project root: $PROJECT_ROOT"

# Build everything
echo ""
echo "Building (release)..."
cargo build --release -p tracer_backend 2>&1

# Run benchmarks
echo ""
echo "Running E2E benchmarks..."
if [ "$CI_MODE" = true ]; then
    ADA_BENCH_CI=1 cargo test --release -p tracer_backend --test bench_e2e_overhead -- --ignored --nocapture 2>&1
else
    cargo test --release -p tracer_backend --test bench_e2e_overhead -- --ignored --nocapture 2>&1
fi

# Show results
RESULTS_FILE="$PROJECT_ROOT/target/bench_e2e_results.json"
if [ -f "$RESULTS_FILE" ]; then
    echo ""
    echo "Results written to: $RESULTS_FILE"
else
    echo ""
    echo "WARNING: Results file not found at $RESULTS_FILE"
    exit 1
fi

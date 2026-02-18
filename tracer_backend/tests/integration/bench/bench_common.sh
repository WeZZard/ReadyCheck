#!/bin/bash
# bench_common.sh - Shared logic for bench trace integration tests.
# Each test script calls: run_trace_test <lang> <config>

set -euo pipefail

BENCH_SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="${PROJECT_ROOT:-$(cd "$BENCH_SCRIPT_DIR/../../../.." && pwd)}"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

TRACER="${TRACER:-$PROJECT_ROOT/target/release/tracer}"

# Ensure the agent library can be found by the tracer
if [[ -z "${ADA_AGENT_RPATH_SEARCH_PATHS:-}" ]]; then
    _search_paths=""
    for _profile in release debug; do
        _candidate="$PROJECT_ROOT/target/$_profile/tracer_backend/lib"
        if [[ -f "$_candidate/libfrida_agent.dylib" ]] || [[ -f "$_candidate/libfrida_agent.so" ]]; then
            [[ -n "$_search_paths" ]] && _search_paths="$_search_paths:"
            _search_paths="$_search_paths$_candidate"
        fi
    done
    if [[ -n "$_search_paths" ]]; then
        export ADA_AGENT_RPATH_SEARCH_PATHS="$_search_paths"
    fi
fi

setup_test() {
    local test_name="$1"
    echo ""
    echo "━━━ $test_name ━━━"
}

pass() {
    echo -e "  ${GREEN}✓ PASS${NC}"
    exit 0
}

fail() {
    echo -e "  ${RED}✗ FAIL: $1${NC}"
    exit 1
}

skip() {
    echo -e "  ${YELLOW}⊘ SKIP: $1${NC}"
    exit 77
}

# Find workload binary by lang and config.
find_bench_binary() {
    local lang="$1" config="$2"
    local name="bench_workload_${lang}_${config}"

    for profile in release debug; do
        local base="$PROJECT_ROOT/target/$profile/tracer_backend/test"
        # CLI executable
        if [[ -f "$base/$name" ]]; then
            echo "$base/$name"; return 0
        fi
        # App bundle
        local app_dir="$base/${name}.app"
        if [[ -d "$app_dir" ]]; then
            local exe
            exe=$(find "$app_dir/Contents/MacOS" -type f -perm +111 2>/dev/null | head -1)
            if [[ -n "$exe" ]]; then
                echo "$exe"; return 0
            fi
        fi
    done
    return 1
}

# Assert trace output directory contains non-empty ATF files.
# Structure: output_dir/session_*/pid_*/thread_*/*.atf
assert_trace_data() {
    local output_dir="$1"

    local atf_count
    atf_count=$(find "$output_dir" -name "*.atf" 2>/dev/null | wc -l | tr -d ' ')
    if [[ "$atf_count" -eq 0 ]]; then
        echo -e "  ${RED}✗ no ATF files found in $output_dir${NC}"
        return 1
    fi

    echo "  ✓ trace output has $atf_count ATF file(s)"

    local non_empty=0
    while IFS= read -r atf_file; do
        if [[ -s "$atf_file" ]]; then
            non_empty=$((non_empty + 1))
        fi
    done < <(find "$output_dir" -name "*.atf" 2>/dev/null)

    if [[ "$non_empty" -eq 0 ]]; then
        echo -e "  ${RED}✗ all ATF files are empty${NC}"
        return 1
    fi
    echo "  ✓ $non_empty non-empty ATF file(s)"
    return 0
}

# For debug_dylib configs: assert .debug.dylib exists in app bundle
assert_debug_dylib_present() {
    local binary_path="$1"
    local app_dir
    app_dir=$(echo "$binary_path" | sed 's|/Contents/MacOS/.*||')
    local dylib
    dylib=$(find "$app_dir/Contents/MacOS" -name "*.debug.dylib" 2>/dev/null | head -1)

    if [[ -n "$dylib" ]]; then
        echo "  ✓ debug dylib found: $(basename "$dylib")"
        return 0
    else
        echo -e "  ${RED}✗ no .debug.dylib found in $(dirname "$binary_path")${NC}"
        return 1
    fi
}

# Main test function: trace a workload binary and verify output.
run_trace_test() {
    local lang="$1" config="$2"
    local test_label="${lang}/${config}"

    setup_test "bench_trace_${lang}_${config}"

    # Find binary
    local binary
    binary=$(find_bench_binary "$lang" "$config") || {
        skip "workload binary not found for $test_label"
    }

    echo "  Binary: $binary"

    # For debug_dylib, verify the dylib exists in the bundle (Swift only — C/C++/ObjC
    # do not produce .debug.dylib even with ENABLE_DEBUG_DYLIB=YES)
    if [[ "$config" == "app_debug_dylib" && "$lang" == "swift" ]]; then
        assert_debug_dylib_present "$binary" || fail "debug dylib not in bundle"
    fi

    # Run tracer
    local output_dir
    output_dir=$(mktemp -d)
    echo "  Tracing $test_label..."

    "$TRACER" spawn "$binary" --output "$output_dir" 2>/dev/null &
    local TRACER_PID=$!

    # Wait up to 60s
    local waited=0
    while kill -0 $TRACER_PID 2>/dev/null && [[ $waited -lt 60 ]]; do
        sleep 1
        waited=$((waited + 1))
    done

    if kill -0 $TRACER_PID 2>/dev/null; then
        kill -9 $TRACER_PID 2>/dev/null || true
        rm -rf "$output_dir"
        fail "tracer did not exit within 60s for $test_label"
    fi
    wait $TRACER_PID 2>/dev/null || true

    # Assert trace data was produced
    assert_trace_data "$output_dir" || {
        rm -rf "$output_dir"
        fail "no trace data for $test_label"
    }

    rm -rf "$output_dir"
    pass
}

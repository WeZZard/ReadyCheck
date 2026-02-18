#!/bin/bash
# test_app_debug_dylib.sh — Trace Swift app bundle (debug dylib), verify trace data
source "$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)/bench_common.sh"
run_trace_test "swift" "app_debug_dylib"

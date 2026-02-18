#!/bin/bash
# test_app_debug.sh — Trace C app bundle (debug), verify trace data
source "$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)/bench_common.sh"
run_trace_test "c" "app_debug"

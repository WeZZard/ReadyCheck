#!/bin/bash
# test_app_release.sh — Trace C++ app bundle (release), verify trace data
source "$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)/bench_common.sh"
run_trace_test "cpp" "app_release"

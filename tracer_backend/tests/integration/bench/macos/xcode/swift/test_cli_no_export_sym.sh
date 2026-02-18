#!/bin/bash
# test_cli_no_export_sym.sh — Trace Swift CLI without exported symbols, verify trace data
source "$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)/bench_common.sh"
run_trace_test "swift" "cli_no_export_sym"

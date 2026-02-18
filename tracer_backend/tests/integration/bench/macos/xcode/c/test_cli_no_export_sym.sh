#!/bin/bash
# test_cli_no_export_sym.sh — Trace C CLI without exported symbols, verify trace data
source "$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)/bench_common.sh"
run_trace_test "c" "cli_no_export_sym"

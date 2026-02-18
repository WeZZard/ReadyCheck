#!/bin/bash
# test_cli_export_sym.sh — Trace C CLI with exported symbols, verify trace data
source "$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)/bench_common.sh"
run_trace_test "c" "cli_export_sym"

#!/bin/bash
# Recursively discover and run all bench integration test scripts.
set -euo pipefail

BENCH_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PASSED=0
FAILED=0
SKIPPED=0

for test_script in $(find "$BENCH_DIR" -path "*/macos/xcode/*/test_*.sh" | sort); do
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    if bash "$test_script"; then
        PASSED=$((PASSED + 1))
    else
        ec=$?
        if [[ $ec -eq 77 ]]; then
            SKIPPED=$((SKIPPED + 1))
        else
            FAILED=$((FAILED + 1))
        fi
    fi
done

echo ""
echo "Bench Integration: $PASSED passed, $FAILED failed, $SKIPPED skipped"
[[ $FAILED -eq 0 ]]

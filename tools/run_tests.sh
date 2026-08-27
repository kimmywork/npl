#!/bin/bash
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
FAILED=0

run() {
    name=$1
    shift
    printf '%-32s' "$name"
    if "$@" > /tmp/npl-suite.out 2>&1; then
        echo PASS
    else
        echo FAIL
        tail -30 /tmp/npl-suite.out
        FAILED=$((FAILED + 1))
    fi
}

run "stage0 reader/executable" "$ROOT/tools/check_stage0.sh"
run "stage0 QBE" "$ROOT/tools/check_stage0_qbe.sh"
run "bootstrap SSA" "$ROOT/tools/check_bootstrap.sh"
run "runtime GC" "$ROOT/tools/check_gc.sh"
run "dynamic core" "$ROOT/tools/check_core.sh"
run "R7RS baseline" "$ROOT/tools/check_r7rs.sh"
run "properties" python3 "$ROOT/tools/prop_tests.py"

rm -f /tmp/npl-suite.out
[ "$FAILED" -eq 0 ] || exit "$FAILED"
echo "all suites: PASS"

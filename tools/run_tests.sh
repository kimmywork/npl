#!/bin/bash
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
FAILED=0
SKIP_CURRENT_FIXED_POINT=0

if [ "${1:-}" = "--skip-current-fixed-point" ]; then
    SKIP_CURRENT_FIXED_POINT=1
    shift
fi
if [ "$#" -ne 0 ]; then
    echo "usage: $0 [--skip-current-fixed-point]" >&2
    exit 2
fi

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
if [ "$SKIP_CURRENT_FIXED_POINT" -eq 0 ]; then
    run "current compiler fixed point" \
        "$ROOT/tools/check_current_fixed_point.sh"
fi
run "full compiler language" "$ROOT/tools/check_full_compiler.sh"
run "three-stage driver" "$ROOT/tools/check_driver.sh"
run "persistent REPL core" "$ROOT/tools/check_repl.sh"
run "runtime GC" "$ROOT/tools/check_gc.sh"
run "stage0 dynamic core" "$ROOT/tools/check_core.sh"
run "R7RS baseline" "$ROOT/tools/check_r7rs.sh"
run "properties" python3 "$ROOT/tools/prop_tests.py"

rm -f /tmp/npl-suite.out
[ "$FAILED" -eq 0 ] || exit "$FAILED"
echo "all suites: PASS"

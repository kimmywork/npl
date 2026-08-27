#!/bin/bash
# run_tests.sh — one entry point for every NPL test suite.
# Usage: ./tools/run_tests.sh [--quick]
#
#   --quick   skip bootstrap-checking suites (not yet wired)
#
# Exit code: number of failed suites (0 = all green).
set -u

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_DIR/build"
QUICK="${1:-}"

SUITES=(
    "stage0 reader/ast + smoke     : $SCRIPT_DIR/check_stage0.sh"
    "runtime mmap + GC             : $SCRIPT_DIR/check_gc.sh"
    "R7RS conformance              : $SCRIPT_DIR/check_r7rs.sh"
    "core features (increment-1/2) : $SCRIPT_DIR/check_core.sh"
    "closures + control flow       : $SCRIPT_DIR/check_control.sh"
    "type system                   : $SCRIPT_DIR/check_type.sh"
)

if [ "$QUICK" != "--quick" ]; then
    SUITES+=("property-based tests     : python3 $SCRIPT_DIR/prop_tests.py")
fi

echo "=== NPL test runner ==="
echo ""

build_first() {
    if [ ! -x "$BUILD_DIR/stage0/nplc0" ]; then
        echo "-- building compiler first --"
        ninja -C "$BUILD_DIR" nplc0 >/dev/null 2>&1 || {
            echo "FATAL: build failed"; return 1; }
    fi
}
build_first || exit 9

failed=0
for entry in "${SUITES[@]}"; do
    name="${entry%%:*}"
    cmd="${entry#*: }"
    echo "--- $name"
    if out=$(eval "$cmd" 2>&1); then
        tail_line=$(printf '%s\n' "$out" | grep -Ei "passed|summary|PASS\]" | tail -2 | tr '\n' ' | ')
        echo "    OK  $tail_line"
    else
        rc=$?
        failed=$((failed+1))
        echo "    FAIL (exit $rc)"
        printf '%s\n' "$out" | tail -25
    fi
done

echo ""
if [ "$failed" -eq 0 ]; then
    echo "=== ALL SUITES GREEN ==="
else
    echo "=== $failed SUITE(S) FAILED ==="
fi
exit "$failed"

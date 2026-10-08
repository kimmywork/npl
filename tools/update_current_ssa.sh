#!/bin/bash
# Recompute and atomically install bootstrap/current.ssa.
# The file is changed only after a successful gen1/gen2/gen3 fixed-point check.

set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
WORK=$(mktemp -d)
ACTIVE_PID=""
cleanup() {
    status=$?
    trap - EXIT INT TERM HUP
    if [ -n "$ACTIVE_PID" ]; then
        kill -TERM "$ACTIVE_PID" 2>/dev/null || true
        wait "$ACTIVE_PID" 2>/dev/null || true
    fi
    if [ "$status" -eq 0 ]; then
        rm -rf "$WORK"
    else
        printf 'incomplete SSA update; artifacts: %s\n' "$WORK" >&2
    fi
    exit "$status"
}
# SHA-256 of a file: sha256sum (GNU/Linux) or shasum (macOS).
sha256_file() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | awk '{print $1}'
    else
        shasum -a 256 "$1" | awk '{print $1}'
    fi
}

trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP

"$ROOT/tools/check_current_fixed_point.sh" --work-dir "$WORK" "$@" &
ACTIVE_PID=$!
status=0
wait "$ACTIVE_PID" || status=$?
ACTIVE_PID=""
[ "$status" -eq 0 ] || exit "$status"

candidate="$WORK/current.ssa"
cp "$WORK/gen3.ssa" "$candidate"
mv "$candidate" "$ROOT/bootstrap/current.ssa"

echo "updated: bootstrap/current.ssa"
printf '%s  %s\n' "$(sha256_file "$ROOT/bootstrap/current.ssa")" "$ROOT/bootstrap/current.ssa"

#!/bin/bash
# Verify the historical relay fixed point for the evolving compiler.
#
# Builds bootstrap/current.ssa, emits the compiler three times, and requires
# gen1 == gen2 == gen3 byte-for-byte. Use --work-dir to retain the artifacts.

set -eu
# Each stage owns a process group so cancellation also stops cc's children.
set -m

ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUILD="$ROOT/build"
QBE="$BUILD/qbe/qbe"
RUNTIME="$BUILD/runtime/libnpl_runtime.a"
SOURCE="$ROOT/src/compiler/main.npl"
CURRENT="$ROOT/bootstrap/current.ssa"

WORK=""
KEEP=0
while [ "$#" -gt 0 ]; do
    case "$1" in
        --work-dir)
            [ "$#" -ge 2 ] || { echo "--work-dir needs a directory" >&2; exit 2; }
            WORK=$2
            shift 2
            ;;
        --keep)
            KEEP=1
            shift
            ;;
        *)
            echo "usage: $0 [--work-dir DIR] [--keep]" >&2
            exit 2
            ;;
    esac
done

if [ -z "$WORK" ]; then
    WORK=$(mktemp -d)
    CLEANUP=1
else
    CLEANUP=0
    mkdir -p "$WORK"
fi
ACTIVE_PID=""
cleanup() {
    status=$?
    trap - EXIT INT TERM HUP
    if [ -n "$ACTIVE_PID" ]; then
        kill -TERM -- "-$ACTIVE_PID" 2>/dev/null || true
        wait "$ACTIVE_PID" 2>/dev/null || true
    fi
    if [ "$status" -ne 0 ] || [ "$CLEANUP" -eq 0 ] || [ "$KEEP" -eq 1 ]; then
        printf 'relay artifacts: %s\n' "$WORK" >&2
    else
        rm -rf "$WORK"
    fi
    exit "$status"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP

run_stage() {
    label=$1
    expected=$2
    shift 2
    started=$SECONDS
    printf 'relay: %s started\n' "$label" >&2
    "$@" &
    ACTIVE_PID=$!
    status=0
    wait "$ACTIVE_PID" || status=$?
    ACTIVE_PID=""
    printf 'relay: %s exit=%s elapsed=%ss\n' \
        "$label" "$status" "$((SECONDS - started))" >&2
    if [ "$status" -ne "$expected" ]; then
        printf 'relay: %s expected exit %s\n' "$label" "$expected" >&2
        return 1
    fi
}

run_stage configure 0 cmake -S "$ROOT" -B "$BUILD" -G Ninja >/dev/null
run_stage runtime-build 0 ninja -C "$BUILD" qbe_build npl_runtime >/dev/null

build_compiler() {
    input=$1
    output=$2
    run_stage "$(basename "$output")/qbe" 0 "$QBE" -o "$output.s" "$input"
    run_stage "$(basename "$output")/link" 0 cc -o "$output" "$output.s" "$RUNTIME"
}

emit_compiler() {
    compiler=$1
    output=$2
    run_stage "$(basename "$output")/emit" 44 \
        "$compiler" --emit-qbe "$SOURCE" > "$output"
    [ -s "$output" ]
}

build_compiler "$CURRENT" "$WORK/current-compiler"
emit_compiler "$WORK/current-compiler" "$WORK/gen1.ssa"
build_compiler "$WORK/gen1.ssa" "$WORK/gen1-compiler"
emit_compiler "$WORK/gen1-compiler" "$WORK/gen2.ssa"
build_compiler "$WORK/gen2.ssa" "$WORK/gen2-compiler"
emit_compiler "$WORK/gen2-compiler" "$WORK/gen3.ssa"

cmp "$WORK/gen1.ssa" "$WORK/gen2.ssa"
cmp "$WORK/gen2.ssa" "$WORK/gen3.ssa"
sha256=$(shasum -a 256 "$WORK/gen1.ssa" | awk '{print $1}')
lines=$(wc -l < "$WORK/gen1.ssa" | tr -d ' ')
echo "current compiler fixed point: PASS"
echo "SSA lines: $lines"
echo "SHA-256: $sha256"
if [ "$CLEANUP" -eq 0 ] || [ "$KEEP" -eq 1 ]; then
    echo "artifacts: $WORK"
fi

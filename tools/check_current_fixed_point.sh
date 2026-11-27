#!/bin/bash
# Verify the historical relay fixed point for the evolving compiler.
#
# Builds bootstrap/current.ssa, emits the compiler three times, and requires
# gen1 == gen2 == gen3 byte-for-byte. Use --work-dir to retain the artifacts.
#
# --relay-codegen accepts the one-generation transient of a compiler change
# that alters its own emitted code (for example a new frontend lowering): if
# gen1 differs from gen2, then gen2 == gen3 is the canonical fixed point and
# gen2.ssa is written as canonical.ssa.  The default strict mode never
# accepts that transient.

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
# NPL_GC_THRESHOLD_BYTES, if set by the caller, is inherited by every
# emission. It changes memory use and speed only, never the emitted SSA.
# Success status of the seed's SSA emission. Generated compilers always exit 0;
# a seed from before the unified nplc (2026-09-29) exits 44 and needs
# --seed-status 44 for the single relay that replaces it.
SEED_STATUS=0
RELAY_CODEGEN=0
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
        --seed-status)
            [ "$#" -ge 2 ] || { echo "--seed-status needs a value" >&2; exit 2; }
            SEED_STATUS=$2
            shift 2
            ;;
        --relay-codegen)
            RELAY_CODEGEN=1
            shift
            ;;
        *)
            echo "usage: $0 [--work-dir DIR] [--keep] [--seed-status N] [--relay-codegen]" >&2
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
        # Job control is unavailable in a non-TTY container, so the stage may
        # not lead its own process group.  Signal both forms before waiting.
        kill -TERM "$ACTIVE_PID" 2>/dev/null || true
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

# SHA-256 of a file: sha256sum (GNU/Linux) or shasum (macOS).
sha256_file() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | awk '{print $1}'
    else
        shasum -a 256 "$1" | awk '{print $1}'
    fi
}

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
    run_stage "$(basename "$output")/link" 0 cc -o "$output" "$output.s" "$RUNTIME" -lm
}

emit_compiler() {
    compiler=$1
    output=$2
    expected=$3
    run_stage "$(basename "$output")/emit" "$expected" \
        "$compiler" --emit-qbe --stdlib-root "$ROOT/lib" "$SOURCE" > "$output"
    [ -s "$output" ]
}

build_compiler "$CURRENT" "$WORK/current-compiler"
emit_compiler "$WORK/current-compiler" "$WORK/gen1.ssa" "$SEED_STATUS"
build_compiler "$WORK/gen1.ssa" "$WORK/gen1-compiler"
emit_compiler "$WORK/gen1-compiler" "$WORK/gen2.ssa" 0
build_compiler "$WORK/gen2.ssa" "$WORK/gen2-compiler"
emit_compiler "$WORK/gen2-compiler" "$WORK/gen3.ssa" 0

if cmp -s "$WORK/gen1.ssa" "$WORK/gen2.ssa"; then
    cp "$WORK/gen1.ssa" "$WORK/canonical.ssa"
elif [ "$RELAY_CODEGEN" -eq 1 ]; then
    cmp "$WORK/gen2.ssa" "$WORK/gen3.ssa"
    cp "$WORK/gen2.ssa" "$WORK/canonical.ssa"
    echo "current compiler codegen relay: gen1 differs; gen2 == gen3"
else
    cmp "$WORK/gen1.ssa" "$WORK/gen2.ssa"
    exit 1
fi
cmp "$WORK/gen2.ssa" "$WORK/gen3.ssa"
sha256=$(sha256_file "$WORK/canonical.ssa")
lines=$(wc -l < "$WORK/canonical.ssa" | tr -d ' ')
echo "current compiler fixed point: PASS"
echo "SSA lines: $lines"
echo "SHA-256: $sha256"
if [ "$CLEANUP" -eq 0 ] || [ "$KEEP" -eq 1 ]; then
    echo "artifacts: $WORK"
fi

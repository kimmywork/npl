#!/bin/bash
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUILD="$ROOT/build"
STAGE0="$BUILD/stage0/nplc0"
QBE="$BUILD/qbe/qbe"
# The permanent chain runs on its frozen runtime (bootstrap/runtime).
RUNTIME="$BUILD/bootstrap-runtime/libnpl_runtime_frozen.a"
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

cmake -S "$ROOT" -B "$BUILD" -G Ninja >/dev/null
ninja -C "$BUILD" nplc0 qbe_build npl_runtime_frozen >/dev/null

build_ssa() {
    "$QBE" -o "$2.s" "$1"
    cc -o "$2" "$2.s" "$RUNTIME" -lm
}

emit() {
    set +e
    "$1" --emit-qbe "$2" > "$3"
    rc=$?
    set -e
    [ "$rc" -eq 44 ]
    [ -s "$3" ]
}

BOOTSTRAP_SOURCE="$ROOT/bootstrap/compiler/main.npl"

"$STAGE0" "$BOOTSTRAP_SOURCE" -o "$WORK/seed"
emit "$WORK/seed" "$BOOTSTRAP_SOURCE" "$WORK/bootstrap.candidate.ssa"
cmp "$ROOT/bootstrap/bootstrap.ssa" "$WORK/bootstrap.candidate.ssa"

build_ssa "$ROOT/bootstrap/bootstrap.ssa" "$WORK/bootstrap-compiler"
emit "$WORK/bootstrap-compiler" "$BOOTSTRAP_SOURCE" "$WORK/bootstrap.self.ssa"
cmp "$ROOT/bootstrap/bootstrap.ssa" "$WORK/bootstrap.self.ssa"

echo "bootstrap fixed point: PASS ($(wc -l < "$ROOT/bootstrap/bootstrap.ssa") SSA lines)"

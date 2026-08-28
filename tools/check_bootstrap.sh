#!/bin/bash
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUILD="$ROOT/build"
STAGE0="$BUILD/stage0/nplc0"
QBE="$BUILD/qbe/qbe"
RUNTIME="$BUILD/runtime/libnpl_runtime.a"
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

cmake -S "$ROOT" -B "$BUILD" -G Ninja >/dev/null
ninja -C "$BUILD" nplc0 qbe_build npl_runtime >/dev/null

build_ssa() {
    "$QBE" -o "$2.s" "$1"
    cc -o "$2" "$2.s" "$RUNTIME"
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
FULL_SOURCE="$ROOT/src/compiler/main.npl"

"$STAGE0" "$BOOTSTRAP_SOURCE" -o "$WORK/seed"
emit "$WORK/seed" "$BOOTSTRAP_SOURCE" "$WORK/bootstrap.candidate.ssa"
cmp "$ROOT/bootstrap/bootstrap.ssa" "$WORK/bootstrap.candidate.ssa"

build_ssa "$ROOT/bootstrap/bootstrap.ssa" "$WORK/bootstrap-compiler"
emit "$WORK/bootstrap-compiler" "$BOOTSTRAP_SOURCE" "$WORK/bootstrap.self.ssa"
cmp "$ROOT/bootstrap/bootstrap.ssa" "$WORK/bootstrap.self.ssa"

build_ssa "$ROOT/bootstrap/current.ssa" "$WORK/current-compiler"
emit "$WORK/current-compiler" "$FULL_SOURCE" "$WORK/current.self.ssa"
cmp "$ROOT/bootstrap/current.ssa" "$WORK/current.self.ssa"

echo "bootstrap fixed point: PASS ($(wc -l < "$ROOT/bootstrap/bootstrap.ssa") SSA lines)"
echo "current compiler recovery: PASS ($(wc -l < "$ROOT/bootstrap/current.ssa") SSA lines)"

#!/bin/bash
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
DRIVER="$ROOT/build/bin/nplc"
RAW="$ROOT/build/bin/nplc-raw"
SOURCE="$ROOT/src/compiler/tests/driver-hello.npl"
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

"$ROOT/tools/build_nplc.sh" >/dev/null
mkdir -p "$WORK/tmp" "$WORK/path with spaces"
cp "$SOURCE" "$WORK/path with spaces/source program.npl"

OUTPUT="$WORK/path with spaces/native program"
TMPDIR="$WORK/tmp" "$DRIVER" \
    "$WORK/path with spaces/source program.npl" -o "$OUTPUT"
"$OUTPUT" > "$WORK/native.out"
printf 'hello from nplc\n' > "$WORK/native.expected"
cmp "$WORK/native.expected" "$WORK/native.out"

set +e
"$DRIVER" --emit-qbe "$SOURCE" > "$WORK/driver.ssa"
driver_status=$?
"$RAW" --emit-qbe --stdlib-root "$ROOT/lib" "$SOURCE" > "$WORK/raw.ssa"
raw_status=$?
set -e
[ "$driver_status" -eq 44 ]
[ "$raw_status" -eq 44 ]
cmp "$WORK/raw.ssa" "$WORK/driver.ssa"

set +e
(
    cd /tmp
    TMPDIR="$WORK/tmp" "$DRIVER" "$SOURCE" -o "$WORK/outside"
)
outside_status=$?
set -e
[ "$outside_status" -eq 0 ]
"$WORK/outside" > "$WORK/outside.out"
cmp "$WORK/native.expected" "$WORK/outside.out"

set +e
TMPDIR="$WORK/tmp" "$DRIVER" \
    "$ROOT/src/compiler/tests/contracts/library-unknown-import.npl" \
    -o "$WORK/rejected" > "$WORK/rejected.out" 2> "$WORK/rejected.err"
compile_status=$?
set -e
[ "$compile_status" -ne 0 ]
grep -F 'only import name not exported' "$WORK/rejected.err" >/dev/null
[ ! -e "$WORK/rejected" ]
[ -z "$(find "$WORK/tmp" -mindepth 1 -maxdepth 1 -print -quit)" ]

set +e
TMPDIR="$WORK/tmp" "$DRIVER" --qbe "$WORK/missing-qbe" \
    "$SOURCE" -o "$WORK/qbe-failure" > /dev/null 2> "$WORK/qbe.err"
qbe_status=$?
set -e
[ "$qbe_status" -eq 127 ]
grep -F 'nplc: qbe failed' "$WORK/qbe.err" >/dev/null
[ ! -e "$WORK/qbe-failure" ]
[ -z "$(find "$WORK/tmp" -mindepth 1 -maxdepth 1 -print -quit)" ]

echo "three-stage driver: PASS"

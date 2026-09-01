#!/bin/bash
# Restore the raw compiler, then compile the NPL three-stage driver.
# Usage: ./tools/build_nplc.sh [driver-output-path]
# Default output: build/bin/nplc

set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUILD="$ROOT/build"
QBE="$BUILD/qbe/qbe"
RUNTIME="$BUILD/runtime/libnpl_runtime.a"
CURRENT="$ROOT/bootstrap/current.ssa"
RAW="$BUILD/bin/nplc-raw"
OUTPUT=${1:-"$BUILD/bin/nplc"}
CC_BIN=${CC:-cc}
CC_PATH=$(command -v "$CC_BIN")

case "$OUTPUT" in
    -*)
        echo "output path cannot start with -" >&2
        exit 2
        ;;
esac

cmake -S "$ROOT" -B "$BUILD" -G Ninja >/dev/null
ninja -C "$BUILD" qbe_build npl_runtime >/dev/null
mkdir -p "$BUILD/bin" "$(dirname "$OUTPUT")"

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
"$QBE" -o "$work/nplc-raw.s" "$CURRENT"
"$CC_PATH" -o "$RAW" "$work/nplc-raw.s" "$RUNTIME"

npl_escape() {
    printf '%s' "$1" | sed 's/\\/\\\\/g; s/"/\\"/g'
}

{
    printf '%s\n' \
        '(import (scheme base)' \
        '        (scheme file)' \
        '        (scheme write)' \
        '        (scheme process-context)' \
        '        (npl process))'
    printf '(define DRIVER-RAW-COMPILER "%s")\n' "$(npl_escape "$RAW")"
    printf '(define DRIVER-QBE "%s")\n' "$(npl_escape "$QBE")"
    printf '(define DRIVER-CC "%s")\n' "$(npl_escape "$CC_PATH")"
    printf '(define DRIVER-RUNTIME "%s")\n' "$(npl_escape "$RUNTIME")"
    printf '(define DRIVER-STDLIB "%s")\n' "$(npl_escape "$ROOT/lib")"
    printf '(include "%s")\n' "$(npl_escape "$ROOT/src/driver/main.npl")"
} > "$work/driver.npl"

set +e
"$RAW" --emit-qbe --stdlib-root "$ROOT/lib" "$work/driver.npl" \
    > "$work/driver.ssa"
compile_status=$?
set -e
if [ "$compile_status" -ne 44 ]; then
    echo "failed to compile NPL driver" >&2
    cat "$work/driver.ssa" >&2
    exit 1
fi

"$QBE" -o "$work/driver.s" "$work/driver.ssa"
"$CC_PATH" -o "$OUTPUT" "$work/driver.s" "$RUNTIME"

printf 'built NPL driver: %s\n' "$OUTPUT"
printf 'built raw compiler: %s\n' "$RAW"

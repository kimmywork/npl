#!/bin/bash
# Restore the raw compiler and build the native three-stage nplc driver.
# Usage: ./tools/build_nplc.sh [driver-output-path]
# Default output: build/bin/nplc

set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUILD="$ROOT/build"
QBE="$BUILD/qbe/qbe"
RUNTIME="$BUILD/runtime/libnpl_runtime.a"
CURRENT="$ROOT/bootstrap/current.ssa"
RAW="$BUILD/bin/nplc-raw"
DRIVER="$BUILD/bin/nplc"
OUTPUT=${1:-"$DRIVER"}

case "$OUTPUT" in
    -*)
        echo "output path cannot start with -" >&2
        exit 2
        ;;
esac

cmake -S "$ROOT" -B "$BUILD" -G Ninja >/dev/null
ninja -C "$BUILD" qbe_build npl_runtime npl_driver >/dev/null
mkdir -p "$BUILD/bin" "$(dirname "$OUTPUT")"

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
"$QBE" -o "$work/nplc-raw.s" "$CURRENT"
cc -o "$RAW" "$work/nplc-raw.s" "$RUNTIME"

if [ "$OUTPUT" != "$DRIVER" ]; then
    cmake -E copy "$DRIVER" "$OUTPUT"
fi

printf 'built nplc driver: %s\n' "$OUTPUT"
printf 'built raw compiler: %s\n' "$RAW"

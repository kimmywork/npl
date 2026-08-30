#!/bin/bash
# Build the complete native nplc compiler from bootstrap/current.ssa.
# Usage: ./tools/build_nplc.sh [output-path]
# Default output: build/bin/nplc

set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUILD="$ROOT/build"
QBE="$BUILD/qbe/qbe"
RUNTIME="$BUILD/runtime/libnpl_runtime.a"
CURRENT="$ROOT/bootstrap/current.ssa"
OUTPUT=${1:-"$BUILD/bin/nplc"}

case "$OUTPUT" in
    -*)
        echo "output path cannot start with -" >&2
        exit 2
        ;;
esac

cmake -S "$ROOT" -B "$BUILD" -G Ninja >/dev/null
ninja -C "$BUILD" qbe_build npl_runtime >/dev/null
mkdir -p "$(dirname "$OUTPUT")"

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
"$QBE" -o "$work/nplc.s" "$CURRENT"
cc -o "$OUTPUT" "$work/nplc.s" "$RUNTIME"

printf 'built nplc: %s\n' "$OUTPUT"

#!/bin/bash
# Build the unified nplc from the canonical SSA seed.
# Usage: ./tools/build_nplc.sh [output-path]
# Default output: build/bin/nplc
#
# The seed is target-independent SSA; this script only assembles and links it
# against the runtime. Prefer tools/build_toolchain.sh, which also creates the
# build-tree install layout that nplc resolves its tools and libraries from.

set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUILD="$ROOT/build"
QBE="$BUILD/qbe/qbe"
RUNTIME="$BUILD/runtime/libnpl_runtime.a"
CURRENT="$ROOT/bootstrap/current.ssa"
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
mkdir -p "$(dirname "$OUTPUT")"

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
"$QBE" -o "$work/nplc.s" "$CURRENT"
"$CC_PATH" -o "$OUTPUT" "$work/nplc.s" "$RUNTIME" -lm
# The pre-unification helper binary is no longer produced or used.
rm -f "$BUILD/bin/nplc-raw"

printf 'built nplc: %s\n' "$OUTPUT"

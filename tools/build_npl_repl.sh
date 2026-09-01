#!/bin/bash
# Build the persistent REPL implemented in NPL.
# Usage: ./tools/build_npl_repl.sh [output-path]

set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUTPUT=${1:-"$ROOT/build/bin/npl-repl"}

"$ROOT/tools/build_nplc.sh" >/dev/null
mkdir -p "$(dirname "$OUTPUT")"
"$ROOT/build/bin/nplc" "$ROOT/src/repl/main.npl" -o "$OUTPUT"
printf 'built NPL REPL: %s\n' "$OUTPUT"

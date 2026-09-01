#!/bin/bash
# Build the persistent REPL implemented in NPL.
# Usage: ./tools/build_npl_repl.sh [output-path]

set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUTPUT=${1:-"$ROOT/build/bin/npl-repl"}

"$ROOT/tools/build_nplc.sh" >/dev/null
mkdir -p "$(dirname "$OUTPUT")"

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

npl_escape() {
    printf '%s' "$1" | sed 's/\\/\\\\/g; s/"/\\"/g'
}

{
    printf '%s\n' \
        '(import (scheme base)' \
        '        (scheme read)' \
        '        (scheme write)' \
        '        (scheme process-context)' \
        '        (scheme repl)' \
        '        (npl evaluator)' \
        '        (npl repl-io))'
    printf '(define REPL-STDLIB "%s")\n' "$(npl_escape "$ROOT/lib")"
    printf '(include "%s")\n' "$(npl_escape "$ROOT/src/repl/main.npl")"
} > "$work/repl.npl"

"$ROOT/build/bin/nplc" "$work/repl.npl" -o "$OUTPUT"
printf 'built NPL REPL: %s\n' "$OUTPUT"

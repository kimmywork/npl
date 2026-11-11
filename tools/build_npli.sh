#!/bin/bash
# Build npli (REPL and program runner) with nplc.
# Usage: ./tools/build_npli.sh [output-path]

set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUTPUT=${1:-"$ROOT/build/bin/npli"}

# CMake builds nplc first and sets NPL_SKIP_NPLC_BUILD to avoid recursion.
if [ -z "${NPL_SKIP_NPLC_BUILD:-}" ]; then
    cmake -S "$ROOT" -B "$ROOT/build" -G Ninja >/dev/null
    ninja -C "$ROOT/build" nplc >/dev/null
fi
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
        '        (scheme file)' \
        '        (npl process)' \
        '        (scheme repl)' \
        '        (npl evaluator)' \
        '        (npl repl-io))'
    printf '(define REPL-STDLIB "%s")\n' "$(npl_escape "$ROOT/lib")"
    printf '(include "%s")\n' "$(npl_escape "$ROOT/src/repl/main.npl")"
} > "$work/repl.npl"

# C libraries loaded with npli -l call back into the runtime through npli's
# own symbols (.agents/archive/design/design-primitives.md, section 5), so npli links every
# runtime member, not only those it uses, and exports them on Linux.
RUNTIME="$ROOT/build/lib/npl/libnpl_runtime.a"
"$ROOT/build/bin/nplc" -c "$work/repl.npl" -o "$work/repl.o"
case "$(uname -s)" in
    Darwin) whole=("-Wl,-force_load,$RUNTIME") ;;
    *) whole=(-rdynamic -Wl,--whole-archive "$RUNTIME" -Wl,--no-whole-archive
              -ldl) ;;
esac
"${NPL_CC:-${CC:-cc}}" -o "$OUTPUT" "$work/repl.o" "${whole[@]}" -lm
# The pre-rename binary is no longer produced or used.
rm -f "$ROOT/build/bin/npl-repl"
printf 'built npli: %s\n' "$OUTPUT"

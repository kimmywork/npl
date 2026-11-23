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
        '        (scheme internal evaluator)' \
        '        (npl internal repl-io))'
    printf '(define REPL-STDLIB "%s")\n' "$(npl_escape "$ROOT/lib")"
    printf '(include "%s")\n' "$(npl_escape "$ROOT/src/repl/main.npl")"
} > "$work/repl.npl"

# npli is the dynamic runtime: the runtime and core native packages are
# shared objects beside the executable.  This is one runtime instance, so
# shared package objects and C libraries loaded with npli -l resolve the same
# runtime symbols.
RUNTIME_DIR="$ROOT/build/lib/npl"
NATIVE="$ROOT/build/lib/npl/native"
"$ROOT/build/bin/nplc" --native=shared -c "$work/repl.npl" -o "$work/repl.o"
case "$(uname -s)" in
    Darwin)
        extra=()
        rpath=(-Wl,-rpath,@loader_path/../lib/npl
               -Wl,-rpath,@loader_path/../lib/npl/native)
        ;;
    *)
        extra=(-ldl -rdynamic)
        rpath=(-Wl,-rpath,'$ORIGIN/../lib/npl'
               -Wl,-rpath,'$ORIGIN/../lib/npl/native')
        ;;
esac
"${NPL_CC:-${CC:-cc}}" -o "$OUTPUT" "$work/repl.o" \
    -L"$RUNTIME_DIR" -lnpl_runtime \
    -L"$NATIVE" -lnpl_time -lnpl_unix "${rpath[@]}" \
    ${extra[@]+"${extra[@]}"} -lm
# The pre-rename binary is no longer produced or used.
rm -f "$ROOT/build/bin/npl-repl"
printf 'built npli: %s\n' "$OUTPUT"

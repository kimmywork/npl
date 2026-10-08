#!/bin/bash
# Shared execution results plus rejection/recovery contracts for malformed syntax.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
FIXTURE="$ROOT/tests/r7rs/fixtures/core-bindings"
NPLC=${NPLC:-"$ROOT/build/bin/nplc"}
NPLI=${NPLI:-"$ROOT/build/bin/npli"}
export NPLC NPLI
"$ROOT/tools/run_r7rs_fixture.sh" "$FIXTURE"
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
printf 'recovered\n' > "$WORK/expected.out"
count=0
while IFS=$'\t' read -r diagnostic form; do
    printf '(import (scheme base))\n(define (main) %s 0)\n' "$form" > "$WORK/invalid.npl"
    status=0
    "$NPLC" "$WORK/invalid.npl" -o "$WORK/invalid" > "$WORK/native.out" 2> "$WORK/native.err" || status=$?
    [ "$status" -ne 0 ]
    [ ! -x "$WORK/invalid" ]
    grep -F "$diagnostic" "$WORK/native.out" "$WORK/native.err" >/dev/null
    printf '(import (scheme base) (scheme write))\n%s\n(write (quote recovered))\n(newline)\n(exit)\n' "$form" > "$WORK/repl.in"
    "$NPLI" --no-prompt < "$WORK/repl.in" > "$WORK/repl.out" 2> "$WORK/repl.err"
    cmp "$WORK/expected.out" "$WORK/repl.out"
    printf 'error: %s\n' "$diagnostic" > "$WORK/expected.err"
    cmp "$WORK/expected.err" "$WORK/repl.err"
    printf '(import (scheme base))\n%s\n' "$form" > "$WORK/invalid.scm"
    status=0
    guile --r7rs -q --no-auto-compile -s "$WORK/invalid.scm" > "$WORK/guile.out" 2> "$WORK/guile.err" || status=$?
    [ "$status" -ne 0 ]
    [ ! -s "$WORK/guile.out" ]
    [ -s "$WORK/guile.err" ]
    count=$((count + 1))
done < "$FIXTURE/invalid-forms.tsv"
printf 'core syntax rejection and REPL recovery: PASS (%s common cases)\n' "$count"

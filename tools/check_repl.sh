#!/bin/bash
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

"$ROOT/tools/build_npl_repl.sh" >/dev/null
"$ROOT/build/bin/nplc" "$ROOT/src/evaluator/core-tests.npl" \
    -o "$WORK/evaluator-core"
"$WORK/evaluator-core" > "$WORK/evaluator.out"
grep -Fx 'PASS evaluator-core 11' "$WORK/evaluator.out" >/dev/null

"$ROOT/build/bin/npl-repl" --no-prompt \
    < "$ROOT/src/repl/tests/persistent.in" \
    > "$WORK/persistent.out" 2> "$WORK/persistent.err"
cmp "$ROOT/src/repl/tests/persistent.out" "$WORK/persistent.out"
cmp "$ROOT/src/repl/tests/persistent.err" "$WORK/persistent.err"

printf '(+ 40 2)\n' | "$ROOT/build/bin/npl-repl" \
    > "$WORK/prompt.out" 2> "$WORK/prompt.err"
printf '> 42\n> ' > "$WORK/prompt.expected"
cmp "$WORK/prompt.expected" "$WORK/prompt.out"
[ ! -s "$WORK/prompt.err" ]

echo "persistent NPL REPL: PASS"

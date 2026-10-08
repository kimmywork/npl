#!/bin/bash
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

[ -n "${NPL_TOOLCHAIN_BUILT:-}" ] || "$ROOT/tools/build_toolchain.sh" >/dev/null
"$ROOT/build/bin/nplc" "$ROOT/src/evaluator/core-tests.npl" \
    -o "$WORK/evaluator-core"
"$WORK/evaluator-core" > "$WORK/evaluator.out"
grep -Fx 'PASS evaluator-core 42' "$WORK/evaluator.out" >/dev/null

"$ROOT/build/bin/nplc" "$ROOT/src/repl/differential-native.npl" \
    -o "$WORK/differential-native"
"$ROOT/build/bin/nplc" "$ROOT/src/repl/differential-evaluator.npl" \
    -o "$WORK/differential-evaluator"
"$WORK/differential-native" \
    > "$WORK/differential-native.out" \
    2> "$WORK/differential-native.err"
"$WORK/differential-evaluator" \
    "$ROOT/src/repl/tests/differential-fixture.scm" \
    > "$WORK/differential-evaluator.out" \
    2> "$WORK/differential-evaluator.err"
cmp "$WORK/differential-native.out" "$WORK/differential-evaluator.out"
[ ! -s "$WORK/differential-native.err" ]
[ ! -s "$WORK/differential-evaluator.err" ]

(
    cd "$WORK"
    "$ROOT/build/bin/npli" --no-prompt
) < "$ROOT/src/repl/tests/persistent.in" \
    > "$WORK/persistent.out" 2> "$WORK/persistent.err"
cmp "$ROOT/src/repl/tests/persistent.out" "$WORK/persistent.out"
cmp "$ROOT/src/repl/tests/persistent.err" "$WORK/persistent.err"

(
    cd "$ROOT/src/repl/tests"
    "$ROOT/build/bin/npli" --no-prompt
) < "$ROOT/src/repl/tests/libraries.in" \
    > "$WORK/libraries.out" 2> "$WORK/libraries.err"
cmp "$ROOT/src/repl/tests/libraries.out" "$WORK/libraries.out"
cmp "$ROOT/src/repl/tests/libraries.err" "$WORK/libraries.err"

(
    cd "$WORK"
    "$ROOT/build/bin/npli" --no-prompt
) < "$ROOT/src/repl/tests/primitive-errors.in" \
    > "$WORK/primitive-errors.out" 2> "$WORK/primitive-errors.err"
cmp "$ROOT/src/repl/tests/primitive-errors.out" "$WORK/primitive-errors.out"
cmp "$ROOT/src/repl/tests/primitive-errors.err" "$WORK/primitive-errors.err"

(
    cd "$WORK"
    "$ROOT/build/bin/npli" --no-prompt
) < "$ROOT/src/repl/tests/eval.in" \
    > "$WORK/eval.out" 2> "$WORK/eval.err"
cmp "$ROOT/src/repl/tests/eval.out" "$WORK/eval.out"
cmp "$ROOT/src/repl/tests/eval.err" "$WORK/eval.err"

(
    cd "$ROOT/src/repl/tests"
    "$ROOT/build/bin/npli" --no-prompt
) < "$ROOT/src/repl/tests/load.in" \
    > "$WORK/load.out" 2> "$WORK/load.err"
cmp "$ROOT/src/repl/tests/load.out" "$WORK/load.out"
cmp "$ROOT/src/repl/tests/load.err" "$WORK/load.err"

printf '(+ 40 2)\n' | "$ROOT/build/bin/npli" \
    > "$WORK/prompt.out" 2> "$WORK/prompt.err"
printf '> 42\n> ' > "$WORK/prompt.expected"
cmp "$WORK/prompt.expected" "$WORK/prompt.out"
[ ! -s "$WORK/prompt.err" ]

# Script mode: same program through npli, nplc, and Guile; arguments are
# passed through untouched and (exit n) becomes the process status.
SCRIPT="$ROOT/src/repl/tests/script"
"$ROOT/build/bin/nplc" "$SCRIPT/native.npl" -o "$WORK/script-native"
for surface in npli native guile; do
    set +e
    case "$surface" in
        npli) NPLI_SCRIPT_CHECK=on "$ROOT/build/bin/npli" \
                  "$SCRIPT/program.scm" 7 "two words" > "$WORK/$surface.out" ;;
        native) NPLI_SCRIPT_CHECK=on "$WORK/script-native" \
                  7 "two words" > "$WORK/$surface.out" ;;
        guile) NPLI_SCRIPT_CHECK=on guile --r7rs -q --no-auto-compile \
                  -s "$SCRIPT/program.scm" 7 "two words" \
                  > "$WORK/$surface.out" 2>/dev/null ;;
    esac
    status=$?
    set -e
    [ "$status" -eq 7 ]
done
cmp "$WORK/npli.out" "$WORK/native.out"
cmp "$WORK/npli.out" "$WORK/guile.out"
grep -Fx '(arguments ("7" "two words"))' "$WORK/npli.out" >/dev/null

# Script errors and missing files use distinct statuses; -e composes.
printf '(import (scheme base))\n(car 1)\n' > "$WORK/bad.scm"
set +e
"$ROOT/build/bin/npli" "$WORK/bad.scm" > /dev/null 2> "$WORK/bad.err"
bad_status=$?
"$ROOT/build/bin/npli" "$WORK/missing.scm" > /dev/null 2>&1
missing_status=$?
"$ROOT/build/bin/npli" --bogus > /dev/null 2>&1
option_status=$?
set -e
[ "$bad_status" -eq 70 ]
grep -F 'error: car: expected a pair' "$WORK/bad.err" >/dev/null
[ "$missing_status" -eq 66 ]
[ "$option_status" -eq 2 ]
"$ROOT/build/bin/npli" -e '(import (scheme base) (scheme write))' \
    -e '(write (+ 40 2))' > "$WORK/expr.out"
printf '42' | cmp - "$WORK/expr.out"

echo "npli REPL and script mode: PASS"

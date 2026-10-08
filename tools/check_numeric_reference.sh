#!/bin/bash
# Compare the exact numeric substrate with Guile; no language integration claim.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
NUMERIC=${NUMERIC_STRESS:-"$ROOT/build/runtime/numeric_stress"}
if [ -z "${NUMERIC_STRESS:-}" ]; then
    cmake -S "$ROOT" -B "$ROOT/build" -G Ninja >/dev/null
    cmake --build "$ROOT/build" --target numeric_stress -j 2 >/dev/null
fi
"$NUMERIC"
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

guile --r7rs -q --no-auto-compile -s "$ROOT/tests/numeric/reference.scm" input > "$WORK/input"
guile --r7rs -q --no-auto-compile -s "$ROOT/tests/numeric/reference.scm" expected > "$WORK/expected"
"$NUMERIC" --oracle < "$WORK/input" > "$WORK/actual" 2> "$WORK/error"
test ! -s "$WORK/error"
cmp "$WORK/expected" "$WORK/actual"
for operation in quotient remainder divide rational; do
    status=0
    "$NUMERIC" "$operation" 1 0 > "$WORK/out" 2> "$WORK/error" || status=$?
    test "$status" -eq 1
    test ! -s "$WORK/out"
    who=$operation
    if [ "$operation" = divide ]; then who=/; fi
    message='division by zero'
    if [ "$operation" = rational ]; then message='zero denominator'; fi
    printf '%s: %s\n' "$who" "$message" > "$WORK/expected-error"
    cmp "$WORK/expected-error" "$WORK/error"
done
printf 'numeric Guile reference: PASS (%s results; 4 zero-divisor errors)\n' "$(wc -l < "$WORK/actual" | tr -d ' ')"

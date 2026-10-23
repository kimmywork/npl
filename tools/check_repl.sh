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
    cd "$WORK"
    "$ROOT/build/bin/npli" --no-prompt
) < "$ROOT/src/repl/tests/interop.in" \
    > "$WORK/interop.out" 2> "$WORK/interop.err"
cmp "$ROOT/src/repl/tests/interop.out" "$WORK/interop.out"
cmp "$ROOT/src/repl/tests/interop.err" "$WORK/interop.err"

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

# Declared C primitives under npli (runtime/ffi.c): the docs/ffi.md example
# as a shared library loaded with -l, the shared C primitive suite and its
# contracts, eval environments, and lookup and load failures.
TESTS="$ROOT/src/compiler/tests"
EXTENSION="$TESTS/c-extension"
case "$(uname -s)" in
    Darwin) shared=(-dynamiclib -undefined dynamic_lookup)
            library="$WORK/libdemoclamp.dylib" ;;
    *) shared=(-shared -fPIC); library="$WORK/libdemoclamp.so" ;;
esac
"${CC:-cc}" -std=c99 -Wall -Werror -I "$ROOT/runtime" "${shared[@]}" \
    -o "$library" "$EXTENSION/clamp.c"
{ cat "$EXTENSION/main.npl"; printf '(main)\n'; } > "$WORK/c-extension.scm"
"$ROOT/build/bin/npli" -l "$library" "$WORK/c-extension.scm" \
    > "$WORK/c-extension.out"
cmp "$EXTENSION/expected.out" "$WORK/c-extension.out"
cat > "$WORK/c-environments.scm" <<'SCHEME'
(import (scheme base) (scheme write) (scheme eval))
(define-c-primitive (clamp (x fixnum) (lo fixnum) (hi fixnum)) fixnum
  "demo_clamp")
(define env (environment '(scheme base)))
(write (list (clamp 15 0 10)
             (eval '(%c-call "demo_clamp" (any any any) -4 0 3) env)
             (eval '((%c-procedure "demo_clamp" (any any any)) 9 0 3) env)))
SCHEME
"$ROOT/build/bin/npli" -l "$library" "$WORK/c-environments.scm" \
    > "$WORK/c-environments.out"
printf '(10 0 3)' | cmp - "$WORK/c-environments.out"
{
    sed '/^(include "framework.npl")$/r '"$TESTS/framework.npl"'
/^(include "framework.npl")$/d' "$TESTS/c-primitives.npl"
    printf '(main)\n'
} > "$WORK/c-primitives.scm"
(cd "$TESTS" && "$ROOT/build/bin/npli" "$WORK/c-primitives.scm") \
    > "$WORK/c-primitives.out"
grep -Fx 'PASS c-primitives 11' "$WORK/c-primitives.out" >/dev/null
reject_npli() {
    set +e
    "$ROOT/build/bin/npli" "$@" > /dev/null 2> "$WORK/rejected.err"
    rejected_status=$?
    set -e
}
for contract in "nested:define-c-primitive is valid at top level only" \
                "type:unknown C primitive type: pear" \
                "conflict:C primitive declaration conflicts with registered npl_car" \
                "symbol:invalid C symbol: npl-f"; do
    { cat "$TESTS/contracts/c-primitive-${contract%%:*}.npl"; printf '(main)\n'; } \
        > "$WORK/contract.scm"
    reject_npli "$WORK/contract.scm"
    [ "$rejected_status" -eq 70 ]
    grep -F "error: ${contract#*:}" "$WORK/rejected.err" >/dev/null
done
reject_npli "$WORK/c-extension.scm"
[ "$rejected_status" -eq 70 ]
grep -F 'error: C function not found: demo_clamp' "$WORK/rejected.err" >/dev/null
reject_npli -l "$WORK/missing-library" "$WORK/c-extension.scm"
[ "$rejected_status" -eq 66 ]
grep -F "npli: cannot load $WORK/missing-library" "$WORK/rejected.err" >/dev/null

# Primitive error objects agree with native (src/compiler/tests, same file).
# Inline framework.npl in place of its include line, then call main.
{
    sed '/^(include "framework.npl")$/r '"$TESTS/framework.npl"'
/^(include "framework.npl")$/d' "$TESTS/primitive-errors.npl"
    printf '(main)\n'
} > "$WORK/primitive-errors.scm"
"$ROOT/build/bin/npli" "$WORK/primitive-errors.scm" > "$WORK/primitive-errors-suite.out"
grep -Fx 'PASS primitive-errors 37' "$WORK/primitive-errors-suite.out" >/dev/null
# Standard libraries are bound to npli's compiled procedures only when their
# source matches what npli was built from; an edited library is evaluated
# from source (lib/npl/evaluator-session.scm, EVAL-NATIVE-LIBRARIES).
mkdir -p "$WORK/stdlib-edited"
cp -R "$ROOT/lib/." "$WORK/stdlib-edited/"
sed 's/(define (current-jiffy) (npl_current_jiffy))/(define (current-jiffy) 42)/' \
    "$ROOT/lib/scheme/time.sld" > "$WORK/stdlib-edited/scheme/time.sld"
! cmp -s "$ROOT/lib/scheme/time.sld" "$WORK/stdlib-edited/scheme/time.sld"
printf '(import (scheme base) (scheme write) (scheme time))\n(write (list (current-jiffy) (+ 20 22)))\n' \
    > "$WORK/edited-library.scm"
"$ROOT/build/bin/npli" --stdlib-root "$WORK/stdlib-edited" \
    "$WORK/edited-library.scm" > "$WORK/edited-library.out"
printf '(42 42)' | cmp - "$WORK/edited-library.out"
"$ROOT/build/bin/npli" "$WORK/edited-library.scm" > "$WORK/native-library.out"
! grep -Fx '(42 42)' "$WORK/native-library.out" >/dev/null

# Raw primitive errors are native error objects under npli as well.
{
    sed '/^(include "framework.npl")$/r '"$TESTS/framework.npl"'
/^(include "framework.npl")$/d' "$TESTS/runtime-errors.npl"
    printf '(main)\n'
} > "$WORK/runtime-errors.scm"
"$ROOT/build/bin/npli" "$WORK/runtime-errors.scm" > "$WORK/runtime-errors-suite.out"
grep -Fx 'PASS runtime-errors 14' "$WORK/runtime-errors-suite.out" >/dev/null
[ "$missing_status" -eq 66 ]
[ "$option_status" -eq 2 ]
"$ROOT/build/bin/npli" -e '(import (scheme base) (scheme write))' \
    -e '(write (+ 40 2))' > "$WORK/expr.out"
printf '42' | cmp - "$WORK/expr.out"

echo "npli REPL and script mode: PASS"

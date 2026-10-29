#!/bin/bash
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
BUILD="$ROOT/build"
QBE="$BUILD/qbe/qbe"
RUNTIME="$BUILD/runtime/libnpl_runtime.a"
TESTS="$ROOT/src/compiler/tests"
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

cmake -S "$ROOT" -B "$BUILD" -G Ninja >/dev/null
ninja -C "$BUILD" qbe_build npl_runtime >/dev/null

build_ssa() {
    "$QBE" -o "$2.s" "$1"
    cc -o "$2" "$2.s" "$RUNTIME" -lm
}

emit() {
    set +e
    "$1" --emit-qbe --stdlib-root "$ROOT/lib" "$2" > "$3"
    rc=$?
    set -e
    [ "$rc" -eq 0 ]
    [ -s "$3" ]
}

build_ssa "$ROOT/bootstrap/current.ssa" "$WORK/compiler"

run_suite() {
    name=$1
    expected=$2
    emit "$WORK/compiler" "$TESTS/$name.npl" "$WORK/$name.ssa"
    build_ssa "$WORK/$name.ssa" "$WORK/$name"
    set +e
    # GC_THRESHOLD forces a per-suite threshold; otherwise inherit the caller's.
    if [ -n "${GC_THRESHOLD:-}" ]; then
        NPL_GC_THRESHOLD_BYTES=$GC_THRESHOLD "$WORK/$name" > "$WORK/$name.out" 2>&1
    else
        "$WORK/$name" > "$WORK/$name.out" 2>&1
    fi
    rc=$?
    set -e
    [ "$rc" -eq 0 ]
    printf '%s\n' "$expected" > "$WORK/$name.expected"
    cmp "$WORK/$name.expected" "$WORK/$name.out"
}

reject_compile() {
    source=$1
    name=$2
    message=$3
    set +e
    "$WORK/compiler" --emit-qbe --stdlib-root "$ROOT/lib" "$TESTS/$source" \
        > "$WORK/$name.out" 2>&1
    rc=$?
    set -e
    [ "$rc" -ne 0 ]
    grep -F "$message" "$WORK/$name.out" >/dev/null
}

reject_reader() {
    name=$1
    message=$2
    reject_compile "reader-errors/$name.npl" "$name" "$message"
}

reject_runtime() {
    source=$1
    name=$2
    message=$3
    emit "$WORK/compiler" "$TESTS/$source" "$WORK/$name.ssa"
    build_ssa "$WORK/$name.ssa" "$WORK/$name"
    set +e
    "$WORK/$name" > "$WORK/$name.out" 2>&1
    rc=$?
    set -e
    [ "$rc" -ne 0 ]
    grep -F "$message" "$WORK/$name.out" >/dev/null
}

reject_literal_mutation() {
    name=$1
    message=$2
    reject_runtime "contracts/$name.npl" "$name" "$message"
}

run_suite dynamic-core "PASS dynamic-core 4"
run_suite reader-datum "PASS reader-datum 24"
run_suite macros-basic "PASS macros-basic 10"
run_suite macro-ellipsis "PASS macro-ellipsis 11"
run_suite macro-scopes "PASS macro-scopes 10"
run_suite derived-forms "PASS derived-forms 17"
run_suite explicit-control "PASS explicit-control 2"
run_suite multiple-values "PASS multiple-values 13"
run_suite continuations "PASS continuations 7"
run_suite dynamic-wind "PASS dynamic-wind 5"
run_suite exceptions "PASS exceptions 6"
run_suite guard "PASS guard 9"
run_suite parameters "PASS parameters 10"
run_suite promises "PASS promises 10"
run_suite libraries "PASS libraries 16"
run_suite eval "PASS eval 14"
run_load() {
    emit "$WORK/compiler" "$TESTS/load.npl" "$WORK/load.ssa"
    build_ssa "$WORK/load.ssa" "$WORK/load"
    "$WORK/load" > "$WORK/load.out"
    printf '42\n' > "$WORK/load.expected"
    cmp "$WORK/load.expected" "$WORK/load.out"
}

run_load
run_suite ports "PASS ports 7"
run_suite standard-io "PASS standard-io 6"
run_suite binary-io "PASS binary-io 15"
run_suite string-io "PASS string-io 12"
run_suite first-wave-libraries "PASS first-wave-libraries 16"
run_suite process "PASS process 9"
run_suite error-objects "PASS error-objects 6"
run_suite primitive-errors "PASS primitive-errors 37"
run_suite runtime-errors "PASS runtime-errors 14"
run_suite c-primitives "PASS c-primitives 11"
# exit maps #t (and no argument) to 0, #f and non-integers to 1.
emit "$WORK/compiler" "$TESTS/exit-status.npl" "$WORK/exit-status.ssa"
build_ssa "$WORK/exit-status.ssa" "$WORK/exit-status"
for case in none:0 true:0 false:1 seven:7 symbol:1 emergency:0 \
            emergency-false:1 value:1; do
    set +e
    "$WORK/exit-status" "${case%%:*}"
    status=$?
    set -e
    [ "$status" -eq "${case#*:}" ] || {
        echo "exit-status ${case%%:*}: status $status" >&2; exit 1; }
done
set +e
"$WORK/exit-status" wind > "$WORK/exit-wind.out"
wind_status=$?
"$WORK/exit-status" emergency-wind > "$WORK/exit-emergency.out"
emergency_status=$?
set -e
[ "$wind_status" -eq 3 ] && [ "$emergency_status" -eq 4 ]
printf 'inner\nouter\n' | cmp - "$WORK/exit-wind.out"
[ ! -s "$WORK/exit-emergency.out" ]
# Declared C primitives are called directly, like table rows, and a symbol
# used as a value gets one wrapper.
grep -F 'call $npl_integer_string_p(' "$WORK/c-primitives.ssa" >/dev/null
[ "$(grep -c 'function l $_npl_prim_npl_integer_string_p(' \
      "$WORK/c-primitives.ssa")" -eq 1 ]
# A tail call from a top-level procedure to itself with its own arity is a
# jump (backend/tail.npl emit-self-jump), not a trampoline request; a call
# with another arity keeps the request and reports the arity error.
SELF="$ROOT/tests/r7rs/fixtures/self-tail-calls"
emit "$WORK/compiler" "$SELF/native.npl" "$WORK/self-tail-calls.ssa"
self_function() {
    awk -v f="function l \$_npl_fn_$1(" \
        'index($0, f) == 1 {p=1} p {print} p && /^}/ {exit}' \
        "$WORK/self-tail-calls.ssa"
}
for name in count_2ddown swap_2dloop rotate collect str_2dloop; do
    self_function "$name" | grep -q '^jmp '
    # set -e ignores a negated pipeline, so fail explicitly.
    if self_function "$name" | grep -qF "npl_tail_request(l \$_npl_fn_$name,"
    then
        echo "self tail call not a jump: $name" >&2
        exit 1
    fi
done
self_function bad | grep -qF 'npl_tail_request(l $_npl_fn_bad,'
# docs/ffi.md example: user C functions linked next to the runtime.
EXTENSION="$TESTS/c-extension"
emit "$WORK/compiler" "$EXTENSION/main.npl" "$WORK/c-extension.ssa"
"$QBE" -o "$WORK/c-extension.s" "$WORK/c-extension.ssa"
cc -std=c99 -Wall -Werror -I "$ROOT/runtime" -c -o "$WORK/clamp.o" \
    "$EXTENSION/clamp.c"
cc -o "$WORK/c-extension" "$WORK/c-extension.s" "$WORK/clamp.o" "$RUNTIME" -lm
"$WORK/c-extension" > "$WORK/c-extension.out"
cmp "$EXTENSION/expected.out" "$WORK/c-extension.out"
# (npl ffi) pointers and with-resource over a counted C resource.
emit "$WORK/compiler" "$EXTENSION/pointers.npl" "$WORK/pointers.ssa"
"$QBE" -o "$WORK/pointers.s" "$WORK/pointers.ssa"
cc -std=c99 -Wall -Wextra -Werror -I "$ROOT/runtime" -c -o "$WORK/pointers.o" \
    "$EXTENSION/pointers.c"
cc -o "$WORK/pointers" "$WORK/pointers.s" "$WORK/pointers.o" "$RUNTIME" -lm
"$WORK/pointers" > "$WORK/pointers.out"
cmp "$EXTENSION/pointers.out" "$WORK/pointers.out"
# A C function returning several values (result type values).
emit "$WORK/compiler" "$EXTENSION/values.npl" "$WORK/values.ssa"
"$QBE" -o "$WORK/values.s" "$WORK/values.ssa"
cc -std=c99 -Wall -Wextra -Werror -I "$ROOT/runtime" -c -o "$WORK/values.o" \
    "$EXTENSION/values.c"
cc -o "$WORK/values" "$WORK/values.s" "$WORK/values.o" "$RUNTIME" -lm
"$WORK/values" > "$WORK/values.out"
cmp "$EXTENSION/values.out" "$WORK/values.out"
# define-c-function: stubs from --emit=c-stubs, linked with plain C code.
"$WORK/compiler" --emit=c-stubs --stdlib-root "$ROOT/lib" "$EXTENSION/typed.npl" \
    > "$WORK/typed-stubs.c"
cc -std=c99 -Wall -Wextra -Wpedantic -Werror -I "$ROOT/runtime" -c \
    -o "$WORK/typed-stubs.o" "$WORK/typed-stubs.c"
cc -std=c99 -Wall -Wextra -Werror -c -o "$WORK/typed.o" "$EXTENSION/typed.c"
emit "$WORK/compiler" "$EXTENSION/typed.npl" "$WORK/typed.ssa"
"$QBE" -o "$WORK/typed.s" "$WORK/typed.ssa"
cc -o "$WORK/typed" "$WORK/typed.s" "$WORK/typed-stubs.o" "$WORK/typed.o" \
    "$RUNTIME" -lm
"$WORK/typed" > "$WORK/typed.out"
cmp "$EXTENSION/typed.out" "$WORK/typed.out"
# A program without define-c-function has empty stubs.
"$WORK/compiler" --emit=c-stubs --stdlib-root "$ROOT/lib" "$EXTENSION/main.npl" \
    > "$WORK/no-stubs.c"
[ ! -s "$WORK/no-stubs.c" ]
# 1 MiB threshold forces many collections through globals and closures.
GC_THRESHOLD=1048576 run_suite gc-roots "PASS gc-roots 5"
# Collections inside a nested trampoline at nursery sizes that failed when
# a trampoline narrowed the stack scan to its own frame (512 B-2 KiB). The
# verify run uses a 16 KiB nursery and every 100th collection: each check
# scans the whole heap, and at 1 KiB with NPL_GC_VERIFY=1 the run takes
# hours.
run_suite gc-nested-scan "PASS gc-nested-scan 1"
for nursery in 256 512 1024 2048 4096 16384; do
    NPL_GC_NURSERY_BYTES=$nursery "$WORK/gc-nested-scan" \
        > "$WORK/gc-nested-scan.out" 2>&1
    printf 'PASS gc-nested-scan 1\n' | cmp - "$WORK/gc-nested-scan.out"
done
NPL_GC_NURSERY_BYTES=16384 NPL_GC_VERIFY=100 "$WORK/gc-nested-scan" \
    > "$WORK/gc-nested-scan.out" 2>&1
printf 'PASS gc-nested-scan 1\n' | cmp - "$WORK/gc-nested-scan.out"
if grep -F 'call $npl_call_cc' "$WORK/continuations.ssa" >/dev/null; then
    echo "full compiler continuations used escape-only runtime path" >&2
    exit 1
fi

reject_literal_mutation literal-vector-mutation \
    "vector-set!: cannot mutate a literal"
reject_literal_mutation literal-bytevector-mutation \
    "bytevector-u8-set!: cannot mutate a literal"
reject_literal_mutation literal-string-mutation \
    "string-set!: cannot mutate a literal"
reject_runtime contracts/multiple-values-in-single-context.npl \
    multiple-values-in-single-context \
    "value context: expected 1 value: 2"
reject_runtime contracts/zero-values-in-single-context.npl \
    zero-values-in-single-context \
    "value context: expected 1 value: 0"
reject_runtime contracts/value-consumer-arity.npl \
    value-consumer-arity \
    "procedure: wrong number of arguments: 2"
reject_runtime contracts/dotted-value-consumer-arity.npl \
    dotted-value-consumer-arity \
    "procedure: wrong number of arguments: 0"
reject_runtime contracts/continuation-zero-values.npl \
    continuation-zero-values \
    "value context: expected 1 value: 0"
reject_runtime contracts/continuation-multiple-values.npl \
    continuation-multiple-values \
    "value context: expected 1 value: 2"
reject_runtime contracts/dynamic-wind-nonprocedure.npl \
    dynamic-wind-nonprocedure \
    "application: not a procedure"
reject_runtime contracts/handler-nonprocedure.npl \
    handler-nonprocedure \
    "with-exception-handler: handler is not a procedure"
reject_runtime contracts/uncaught-exception.npl \
    uncaught-exception "uncaught exception"
reject_runtime contracts/primitive-error-uncaught.npl \
    primitive-error-uncaught "car: expected a pair: 42"
reject_runtime contracts/handler-secondary-raise.npl \
    handler-secondary-raise "uncaught exception"
reject_runtime contracts/parameter-arity.npl \
    parameter-arity "parameter: expected zero or one argument"
reject_runtime contracts/parameterize-nonparameter.npl \
    parameterize-nonparameter "parameterize: not a parameter"
reject_runtime contracts/read-char-nonport.npl \
    read-char-nonport "read-char: expected a port"
reject_runtime contracts/write-closed-port.npl \
    write-closed-port "write-u8: port is closed"
reject_runtime contracts/write-u8-range.npl \
    write-u8-range "write-u8: expected a byte"
reject_runtime contracts/read-char-binary-port.npl \
    read-char-binary-port "read-char: wrong port type"
reject_runtime contracts/write-bytevector-range.npl \
    write-bytevector-range "write-bytevector: index out of range"
reject_runtime contracts/write-bytevector-closed.npl \
    write-bytevector-closed "write-u8: port is closed"
reject_runtime contracts/open-input-string-type.npl \
    open-input-string-type "open-input-string: expected a string"
reject_runtime contracts/get-output-string-port.npl \
    get-output-string-port "get-output-string: expected a string output port"
reject_runtime contracts/process-improper-arguments.npl \
    process-improper-arguments "run-process: expected a proper list"
reject_runtime contracts/process-nonstring-argument.npl \
    process-nonstring-argument "run-process: expected a string"
reject_compile contracts/library-cycle.npl \
    library-cycle "library cycle"
reject_compile contracts/library-unknown-import.npl \
    library-unknown-import "only import name not exported"
reject_compile contracts/library-duplicate.npl \
    library-duplicate "duplicate library"
reject_compile contracts/library-unimported-standard.npl \
    library-unimported-standard "unbound identifier: display"
reject_compile contracts/c-primitive-nested.npl \
    c-primitive-nested "define-c-primitive is valid at top level only"
reject_compile contracts/c-primitive-type.npl \
    c-primitive-type "unknown C primitive type: pear"
reject_compile contracts/c-primitive-conflict.npl \
    c-primitive-conflict "C primitive declaration conflicts with registered npl_car"
reject_compile contracts/c-primitive-values-conflict.npl \
    c-primitive-values-conflict \
    "C primitive declaration conflicts with registered npl_car"
reject_compile contracts/c-function-nested.npl \
    c-function-nested "define-c-function is valid at top level only"
reject_compile contracts/c-function-type.npl \
    c-function-type "unknown C function type: float"
reject_compile contracts/c-function-void-argument.npl \
    c-function-void-argument "void is valid only as a define-c-function result"
reject_compile contracts/c-function-pointer-out-result.npl \
    c-function-pointer-out-result \
    "pointer-out is valid only as a define-c-function argument"
reject_compile contracts/c-function-arrow.npl \
    c-function-arrow "malformed define-c-function"
reject_compile contracts/c-function-conflict.npl \
    c-function-conflict "conflicting define-c-function declarations of demo_f"
reject_compile contracts/c-primitive-symbol.npl \
    c-primitive-symbol "invalid C symbol: npl-f"

reject_reader bad-dotted "expected ) after dotted datum"
reject_reader bad-byte "bytevector literal byte out of range"
reject_reader bad-unquote "unquote outside quasiquote"
reject_reader bad-duplicate-label "duplicate datum label"
reject_reader bad-direct-label "datum label cannot directly reference itself"
reject_reader bad-undefined-label "undefined datum label"
reject_reader bad-string-escape "unknown string escape"
reject_reader bad-empty-hex-escape \
    "hexadecimal string escape needs a digit"
reject_reader bad-quasiquote-cycle "circular datum label in quasiquote"
reject_reader bad-delimiter \
    "unmatched ( at line 1 col 1; expected ) before end of input"

reject_compile macro-errors/duplicate-pattern-variable.npl \
    duplicate-pattern-variable "duplicate pattern variable"
reject_compile macro-errors/non-identifier-literal.npl \
    non-identifier-literal "syntax literal must be an identifier"
reject_compile macro-errors/duplicate-literal.npl \
    duplicate-literal "duplicate syntax literal"
reject_compile macro-errors/malformed-rule.npl \
    malformed-rule "syntax rule must contain a pattern and template"
reject_compile macro-errors/no-matching-rule.npl \
    no-matching-rule "macro use does not match any syntax rule"
reject_compile macro-errors/syntax-error.npl \
    syntax-error "rejected macro input"
reject_compile macro-errors/inconsistent-repetition.npl \
    inconsistent-repetition "inconsistent ellipsis repetition lengths"
reject_compile macro-errors/wrong-ellipsis-depth.npl \
    wrong-ellipsis-depth "pattern variable used at wrong ellipsis depth"
reject_compile macro-errors/ellipsis-without-variable.npl \
    ellipsis-without-variable \
    "ellipsis template has no repeated pattern variable"
reject_compile macro-errors/misplaced-pattern-ellipsis.npl \
    misplaced-pattern-ellipsis "misplaced ellipsis in pattern"
reject_compile macro-errors/misplaced-template-ellipsis.npl \
    misplaced-template-ellipsis "misplaced ellipsis in template"
reject_compile macro-errors/internal-use-before-definition.npl \
    internal-use-before-definition \
    "syntax keyword used before its definition"
reject_compile macro-errors/duplicate-internal-syntax.npl \
    duplicate-internal-syntax "duplicate internal syntax keyword"
reject_compile macro-errors/duplicate-local-syntax.npl \
    duplicate-local-syntax "duplicate local syntax keyword"
reject_compile macro-errors/let-syntax-sibling-reference.npl \
    let-syntax-sibling-reference "unknown builtin or unbound: right"
reject_compile macro-errors/malformed-syntax-binding.npl \
    malformed-syntax-binding "malformed syntax binding"
reject_compile macro-errors/duplicate-case-datum.npl \
    duplicate-case-datum "duplicate case datum"
reject_compile macro-errors/cond-else-not-last.npl \
    cond-else-not-last "cond else clause must be last"
reject_compile macro-errors/case-else-not-last.npl \
    case-else-not-last "malformed case clauses"
reject_compile macro-errors/malformed-do-binding.npl \
    malformed-do-binding "macro use does not match any syntax rule: do"
reject_compile macro-errors/malformed-parameterize-binding.npl \
    malformed-parameterize-binding \
    "macro use does not match any syntax rule: parameterize"
reject_compile macro-errors/force-arity.npl \
    force-arity "macro use does not match any syntax rule: force"
reject_compile macro-errors/delay-arity.npl \
    delay-arity "macro use does not match any syntax rule: delay"

echo "full compiler dynamic core: PASS"
echo "full compiler reader/datum: PASS"
echo "full compiler macros: PASS"

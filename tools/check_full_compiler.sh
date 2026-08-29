#!/bin/bash
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
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
    cc -o "$2" "$2.s" "$RUNTIME"
}

emit() {
    set +e
    "$1" --emit-qbe "$2" > "$3"
    rc=$?
    set -e
    [ "$rc" -eq 44 ]
    [ -s "$3" ]
}

build_ssa "$ROOT/bootstrap/current.ssa" "$WORK/compiler"

run_suite() {
    name=$1
    expected=$2
    emit "$WORK/compiler" "$TESTS/$name.npl" "$WORK/$name.ssa"
    build_ssa "$WORK/$name.ssa" "$WORK/$name"
    set +e
    "$WORK/$name" > "$WORK/$name.out" 2>&1
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
    "$WORK/compiler" --emit-qbe "$TESTS/$source" \
        > "$WORK/$name.out" 2>&1
    rc=$?
    set -e
    [ "$rc" -ne 44 ]
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
run_suite macro-ellipsis "PASS macro-ellipsis 9"
run_suite macro-scopes "PASS macro-scopes 10"
run_suite derived-forms "PASS derived-forms 17"
run_suite explicit-control "PASS explicit-control 2"
run_suite multiple-values "PASS multiple-values 13"
run_suite continuations "PASS continuations 7"
run_suite dynamic-wind "PASS dynamic-wind 5"
run_suite exceptions "PASS exceptions 6"
run_suite guard "PASS guard 5"
if grep -F 'call $npl_call_cc' "$WORK/continuations.ssa" >/dev/null; then
    echo "full compiler continuations used escape-only runtime path" >&2
    exit 1
fi

reject_literal_mutation literal-vector-mutation \
    "vector-set!: cannot mutate a literal"
reject_literal_mutation literal-bytevector-mutation \
    "bytevector-u8-set!: cannot mutate a literal"
reject_runtime contracts/multiple-values-in-single-context.npl \
    multiple-values-in-single-context \
    "value context: expected 1 value, got 2"
reject_runtime contracts/zero-values-in-single-context.npl \
    zero-values-in-single-context \
    "value context: expected 1 value, got 0"
reject_runtime contracts/value-consumer-arity.npl \
    value-consumer-arity \
    "npl_tail_request_closure: expected 2 arguments, got 3"
reject_runtime contracts/dotted-value-consumer-arity.npl \
    dotted-value-consumer-arity \
    "npl_tail_request_closure: expected at least 2 arguments, got 1"
reject_runtime contracts/continuation-zero-values.npl \
    continuation-zero-values \
    "value context: expected 1 value, got 0"
reject_runtime contracts/continuation-multiple-values.npl \
    continuation-multiple-values \
    "value context: expected 1 value, got 2"
reject_runtime contracts/dynamic-wind-nonprocedure.npl \
    dynamic-wind-nonprocedure \
    "npl_tail_request_closure: not a closure"
reject_runtime contracts/handler-nonprocedure.npl \
    handler-nonprocedure \
    "with-exception-handler: handler is not a procedure"
reject_runtime contracts/uncaught-exception.npl \
    uncaught-exception "uncaught exception"
reject_runtime contracts/handler-secondary-raise.npl \
    handler-secondary-raise "uncaught exception"

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

echo "full compiler dynamic core: PASS"
echo "full compiler reader/datum: PASS"
echo "full compiler macros: PASS"

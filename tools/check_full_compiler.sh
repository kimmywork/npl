#!/bin/bash
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
BUILD="$ROOT/build"
QBE="$BUILD/qbe/qbe"
RUNTIME="$BUILD/runtime/libnpl_runtime.a"
NATIVE="$BUILD/lib/npl/native"
export NPL_NATIVE_PATH="$NATIVE"
TESTS="$ROOT/src/compiler/tests"
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

cmake -S "$ROOT" -B "$BUILD" -G Ninja >/dev/null
ninja -C "$BUILD" qbe_build npl_runtime npl_runtime_shared npl_time npl_unix npl_demo_crc32 npl_demo_crc32_static npl_c_struct_layouts thread_test safepoint_test thread_object_test thread_lifecycle_test shared_lock_test blocking_io_test virtual_thread_test >/dev/null
# zlib is optional; the target exists only when CMake found the library.
ninja -C "$BUILD" npl_demo_zlib npl_demo_zlib_static >/dev/null 2>&1 || true

# T1 acceptance: native TLS current context, per-thread stack limits and
# dynamic state, explicit attach/detach, and GC visibility of every context.
"$BUILD/runtime/thread_test"
# T1.3 acceptance: concurrent mutators, world-stop park/resume, parked
# thread stack/register scanning, cancellation, blocking/exclusion.
"$BUILD/runtime/safepoint_test"
# T2 acceptance: SRFI 18 object layer and GC visibility.
"$BUILD/runtime/thread_object_test"
# T2 acceptance: thread lifecycle, join, termination, mutex and CV.
"$BUILD/runtime/thread_lifecycle_test"
# T1.3 acceptance: shared-object lock under concurrent mutation + GC.
"$BUILD/runtime/shared_lock_test"
# T1.3 acceptance: blocking libc call publishes context for GC.
"$BUILD/runtime/blocking_io_test"
# T2b acceptance: one-shot scheduler tokens, N virtual threads on one
# carrier, then cross-carrier migration with M carriers.
"$BUILD/runtime/virtual_thread_test" 1
"$BUILD/runtime/virtual_thread_test" 4

EXTRA_NATIVE=()
EXTRA_LIBS=()
if [ -f "$NATIVE/libnpl_demo_zlib.a" ]; then
    EXTRA_NATIVE=("$NATIVE/libnpl_demo_zlib.a")
    EXTRA_LIBS=(-lz)
fi

build_ssa() {
    "$QBE" -o "$2.s" "$1"
    cc -o "$2" "$2.s" "$NATIVE/libnpl_time.a" "$NATIVE/libnpl_unix.a" \
        "$NATIVE/libnpl_demo_crc32.a" \
        ${EXTRA_NATIVE[@]+"${EXTRA_NATIVE[@]}"} "$RUNTIME" -lm \
        ${EXTRA_LIBS[@]+"${EXTRA_LIBS[@]}"}
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

# T2 acceptance: SRFI 18 cross-thread continuation invocation. Native-only:
# the differential fixture runner compares nplc and npli output, but a
# cross-thread scenario has no evaluator surface. Each harness asserts
# internally and exits non-zero on failure.
for name in cross-thread-terminal cross-thread-continuation \
            cross-thread-wind-parameters virtual-threads virtual-thread-edge \
            thread-timeouts; do
    emit "$WORK/compiler" "$ROOT/tests/unit/$name.npl" "$WORK/$name.ssa"
    build_ssa "$WORK/$name.ssa" "$WORK/$name"
    "$WORK/$name" > "$WORK/$name.out" 2>&1
    printf 'PASS: %s (native-only thread increment)\n' "$name"
done

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

run_import() {
    name=$1
    emit "$WORK/compiler" "$TESTS/imports/$name.npl" "$WORK/import-$name.ssa"
}

# reject_import NAME [MESSAGE]: the import fails with the conflict
# diagnostic, and MESSAGE (the name and both defining libraries) when given.
reject_import() {
    name=$1
    message=${2:-conflicting imported binding}
    set +e
    "$WORK/compiler" --emit-qbe --stdlib-root "$ROOT/lib" \
        "$TESTS/imports/$name.npl" > "$WORK/import-$name.out" 2>&1
    rc=$?
    set -e
    [ "$rc" -ne 0 ]
    grep -F "$message" "$WORK/import-$name.out" >/dev/null
}

reject_literal_mutation() {
    name=$1
    message=$2
    reject_runtime "contracts/$name.npl" "$name" "$message"
}

run_suite dynamic-core "PASS dynamic-core 4"
run_suite reader-datum "PASS reader-datum 26"
run_suite macros-basic "PASS macros-basic 10"
run_suite macro-ellipsis "PASS macro-ellipsis 11"
run_suite macro-scopes "PASS macro-scopes 10"
run_suite macro-conformance "PASS macro-conformance 9"
run_suite macro-core-syntax "PASS macro-core-syntax 4"
run_suite srfi-46 "PASS srfi-46 5"
run_suite srfi-149 "PASS srfi-149 5"
run_suite syntax-case "PASS syntax-case 4"
run_suite syntax-case-procedural "PASS syntax-case-procedural 20"
run_suite ffi-memory "PASS ffi-memory 84"
# A retained owner and its views must survive minor collections: the FfiBlock
# store needs a write barrier.
NPL_GC_NURSERY_BYTES=4096 NPL_GC_VERIFY=100 \
    run_suite ffi-gc "PASS ffi-gc 2"
run_suite runtime-hardening "PASS runtime-hardening 10"
run_suite c-structs "PASS c-structs 17"
# R6: importing core native packages reads the build-time layout cache and
# never runs a C compiler.
printf '(import (scheme base) (npl time) (npl unix))\n(define now (current-time))\n' \
    > "$WORK/cache-import.npl"
NPL_C_STRUCT_LAYOUTS="$NATIVE/c-struct-layouts.meta" NPL_CC=/usr/bin/false \
    "$WORK/compiler" --emit-qbe --stdlib-root "$ROOT/lib" \
    "$WORK/cache-import.npl" > "$WORK/cache-import.ssa"
# R19: NPL_C_STRUCT_LAYOUTS is a colon-separated search path.  The temporary
# compiler has no installed cache beside it, so this names the core file
# after the package file; the fallback to the cache beside an installed nplc
# is checked by (tools link-modes) with build/bin/nplc.
printf '(c-struct-layouts\n  ("struct iovec" (size . 16) (iov_base . 0) (iov_len . 8))\n)\n' \
    > "$WORK/package-layouts.meta"
NPL_C_STRUCT_LAYOUTS="$WORK/package-layouts.meta:$NATIVE/c-struct-layouts.meta" \
    NPL_CC=/usr/bin/false \
    "$WORK/compiler" --emit-qbe --stdlib-root "$ROOT/lib" \
    "$WORK/cache-import.npl" > "$WORK/cache-import-package.ssa"
run_suite srfi-19 "PASS srfi-19 52"
run_suite thread "PASS thread 26"
run_suite srfi-170 "PASS srfi-170 132"
run_suite platform "PASS platform 5"
run_suite native-crc32 "PASS native-crc32 4"
if [ -f "$NATIVE/libnpl_demo_zlib.so" ] || \
   [ -f "$NATIVE/libnpl_demo_zlib.dylib" ]; then
    run_suite native-zlib "PASS native-zlib 5"
fi
run_suite npl-lambda "PASS npl-lambda 9"
run_suite derived-forms "PASS derived-forms 17"
run_suite explicit-control "PASS explicit-control 2"
run_suite multiple-values "PASS multiple-values 13"
run_suite continuations "PASS continuations 7"
run_suite dynamic-wind "PASS dynamic-wind 5"
run_suite exceptions "PASS exceptions 6"
run_suite guard "PASS guard 9"
run_suite parameters "PASS parameters 10"
run_suite promises "PASS promises 10"
run_suite libraries "PASS libraries 24"
run_suite forward-references "PASS forward-references 4"
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
run_suite process "PASS process 15"
run_suite error-objects "PASS error-objects 6"
run_suite primitive-errors "PASS primitive-errors 37"
run_suite runtime-errors "PASS runtime-errors 14"
run_suite c-primitives "PASS c-primitives 11"
run_suite records "PASS records 10"
run_suite boxes "PASS boxes 6"
run_suite srfi-1 "PASS srfi-1 44"
run_suite srfi-13 "PASS srfi-13 61"
run_suite srfi-130 "PASS srfi-130 29"
run_suite srfi-140 "PASS srfi-140 24"
run_suite srfi-133 "PASS srfi-133 25"
run_suite srfi-41 "PASS srfi-41 32"
run_suite srfi-47 "PASS srfi-47 34"
run_suite srfi-158 "PASS srfi-158 38"
run_suite srfi-161 "PASS srfi-161 10"
run_suite srfi-195 "PASS srfi-195 7"
run_suite srfi-128 "PASS srfi-128 38"
run_suite srfi-113 "PASS srfi-113 44"
run_suite srfi-132 "PASS srfi-132 36"
run_suite srfi-44 "PASS srfi-44 43"
run_suite srfi-44-surface "PASS srfi-44-surface 12"
run_suite srfi-146 "PASS srfi-146 38"
run_suite srfi-146-surface "PASS srfi-146-surface 17"
run_suite srfi-178 "PASS srfi-178 39"
run_suite srfi-178-surface "PASS srfi-178-surface 8"
run_suite srfi-review-foundation "PASS review-foundation 119"
run_suite srfi-review-strings "PASS review-strings 100"
run_suite srfi-review-vectors-arrays "PASS review-vectors-arrays 69"
run_suite srfi-review-lazy-data "PASS review-lazy-data 60"

run_import all-aliases-with-base
run_import unix-process
run_import test-expect-and-util
run_import base-and-npl-string
run_import base-and-npl-vector
run_import box-111-and-195
run_import srfi-140-readme-recipe
run_import srfi-140-spec-names
reject_import scheme-char-and-srfi-13-reject
reject_import srfi-13-and-125-reject \
    "conflicting imported binding: string-hash (from (npl string) and (npl hashtable))"
reject_import srfi-13-and-130-reject
run_suite srfi-196 "PASS srfi-196 29"
run_suite srfi-189 "PASS srfi-189 39"
run_suite hash-tables "PASS hash-tables 6"
NPL_GC_NURSERY_BYTES=65536 NPL_GC_VERIFY=10 "$WORK/hash-tables" \
    > "$WORK/hash-tables-verify.out"
cmp "$WORK/hash-tables.expected" "$WORK/hash-tables-verify.out"
run_suite match "PASS match 17"
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
SELF="$ROOT/tests/fixtures/self-tail-calls"
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
# A named loop whose name is assigned once jumps back to the start of its
# body (backend/bindings.npl self-loop-binding?); a reassigned name, a call
# from a nested lambda, and a call with another arity keep the trampoline.
# The loop is the first lambda its top-level function creates.
LOOPS="$ROOT/tests/fixtures/named-loops"
emit "$WORK/compiler" "$LOOPS/native.npl" "$WORK/named-loops.ssa"
loop_lambda() {
    awk -v f="function l \$_npl_fn_$1(" \
        'index($0, f) == 1 {p=1} p {print} p && /^}/ {exit}' \
        "$WORK/named-loops.ssa" |
        grep -o 'npl_make_flat_closure(l \$_lambda_[0-9]*' | head -1 |
        sed 's/.*\$//'
}
loops_back() {
    awk -v f="function l \$$(loop_lambda "$1")(" \
        'index($0, f) == 1 {p=1} p {print} p && /^}/ {exit}' \
        "$WORK/named-loops.ssa" | awk '
        /^@start/ {s = 1; next}
        s && /^@L/ && first == "" {first = $0}
        /^jmp / {target[$2] = 1}
        END {exit !(first != "" && (first in target))}'
}
for name in sum_2dto spin swap thunks fill emit_2dall; do
    loops_back "$name" || { echo "named loop not a jump: $name" >&2; exit 1; }
done
for name in reassigned via_2dlambda bad_2darity; do
    if loops_back "$name"; then
        echo "named loop jumped where it must not: $name" >&2
        exit 1
    fi
done
# Direct-call report (frontend/direct-calls.npl, C2a): the verdict for each
# program function, and the report must not change the emitted SSA.
DIRECT="$TESTS/direct-calls"
NPL_DIRECT_REPORT=1 "$WORK/compiler" --emit-qbe --stdlib-root "$ROOT/lib" \
    "$DIRECT/probe.npl" > "$WORK/direct-report.ssa" \
    2> "$WORK/direct-report.txt"
grep -v '_npl_lib_\|total' "$WORK/direct-report.txt" |
    cmp - "$DIRECT/expected.txt"
emit "$WORK/compiler" "$DIRECT/probe.npl" "$WORK/direct-plain.ssa"
cmp "$WORK/direct-report.ssa" "$WORK/direct-plain.ssa"
# Direct entries (design-calls C2b, C2c): a direct-safe function has a
# direct entry that calls itself directly and checks the stack limit; its
# CPS entry (the fallback) does not call it. A variadic function has one;
# raise in a direct body raises through npl_raise_direct. Deep recursion and
# a collection at a small nursery inside it still work.
DCALLS="$ROOT/tests/fixtures/direct-calls"
emit "$WORK/compiler" "$DCALLS/native.npl" "$WORK/direct-calls.ssa"
direct_function() {
    awk -v f="function l \$$1(" \
        'index($0, f) == 1 {p=1} p {print} p && /^}/ {exit}' \
        "$WORK/direct-calls.ssa"
}
direct_function _npl_dfn_fib | grep -qF 'call $_npl_dfn_fib(l '
# T1: the guard reads the executable-owned TLS slot directly (one IL line),
# not a global or a runtime accessor call.
direct_function _npl_dfn_fib | grep -qF 'loadl thread $npl_stack_limit'
grep -qF 'export thread data $npl_stack_limit = { l 0 }' "$WORK/direct-calls.ssa"
grep -qF 'export data $npl_opaque_zero = { l 0 }' "$WORK/direct-calls.ssa"
# T1.3: the world-stop slot and a poll at every direct entry.
grep -qF 'export data $npl_gc_stop_requested = { l 0 }' "$WORK/direct-calls.ssa"
direct_function _npl_dfn_fib | grep -qF 'call $npl_safepoint_poll()'
# The CPS entry is the stack-limit fallback: it must not re-enter fib's
# direct entry. Other direct entries (library procedures in numeric slow
# paths) check the stack themselves.
if direct_function _npl_fn_fib | grep -qF '$_npl_dfn_fib('; then
    echo "CPS entry of fib calls fib's direct entry" >&2
    exit 1
fi
direct_function _npl_dfn_sum_2dall | grep -qF 'function l $_npl_dfn_sum_2dall(l %npl_p0, l %npl_p1)'
direct_function _npl_dfn_checked_2dsqrt | grep -qF 'call $npl_raise_direct(l '
if direct_function _npl_dfn_my_2deven_3f | grep -q .; then
    echo "mutual tail recursion got a direct entry" >&2
    exit 1
fi
build_ssa "$WORK/direct-calls.ssa" "$WORK/direct-calls"
NPL_GC_NURSERY_BYTES=65536 "$WORK/direct-calls" > "$WORK/direct-calls.out"
cmp "$DCALLS/expected.out" "$WORK/direct-calls.out"
# Direct lambdas (design-calls C3): the mapped lambda has a direct entry
# stored in its closure, and map-one calls it through npl_closure_direct.
DLAMBDAS="$ROOT/tests/fixtures/direct-lambdas"
emit "$WORK/compiler" "$DLAMBDAS/native.npl" "$WORK/direct-lambdas.ssa"
grep -q '^function l \$_lambda_[0-9]*_d(l %npl_env, l %npl_p0)' \
    "$WORK/direct-lambdas.ssa"
awk -v f='function l $_npl_fn__5fnpl_5flib_5f1_5fmap_5f2done(' \
    'index($0, f) == 1 {p=1} p {print} p && /^}/ {exit}' \
    "$WORK/direct-lambdas.ssa" | grep -qF 'call $npl_closure_direct('
# Lambdas are flat closures: one npl_make_flat_closure per creation and
# inline capture loads, no separate environment object (design-calls C1b).
CAPTURES="$ROOT/tests/fixtures/closure-captures"
emit "$WORK/compiler" "$CAPTURES/native.npl" "$WORK/closure-captures.ssa"
grep -qF 'call $npl_make_flat_closure(' "$WORK/closure-captures.ssa"
if grep -qE 'npl_make_environment|npl_environment_ref' \
        "$WORK/closure-captures.ssa"; then
    echo "closure environment built outside the flat closure" >&2
    exit 1
fi
# Inline primitives (backend/inline.npl): car and cdr load at +7 and +15
# after a pair-tag test, vector-ref loads from vector+15 after a bounds test,
# vector-set! and record modifiers store and call the write barrier; the C
# functions remain only as the failing branch.
INLINE="$TESTS/inline-primitives"
emit "$WORK/compiler" "$INLINE/probe.npl" "$WORK/inline.ssa"
inline_function() {
    awk -v f="function l \$$1(" \
        'index($0, f) == 1 {p=1} p {print} p && /^}/ {exit}' \
        "$WORK/inline.ssa"
}
inline_function _npl_dfn_sum_2dlist | grep -qE '=l add %t[0-9]+, 7$'
inline_function _npl_dfn_sum_2dlist | grep -qE '=l add %t[0-9]+, 15$'
inline_function _npl_dfn_sum_2dlist | grep -qF 'call $npl_car(l '
inline_function _npl_dfn_sum_2dvec | grep -qE '=l add %t[0-9]+, 15$'
inline_function _npl_dfn_sum_2dvec | grep -qF 'cultl'
inline_function _npl_dfn_fill_21 | grep -qF 'call $npl_gc_write_barrier_value(l '
# A record accessor tests the box tag (22) and the type in slot 0 inline.
inline_function _npl_dfn_point_2dx | grep -qE '=w ceqw %t[0-9]+, 22$'
inline_function _npl_dfn_point_2dx | grep -qF 'loadl $gv_point'
inline_function _npl_dfn_set_2dpoint_2dx_21 | grep -qF 'call $npl_gc_write_barrier_value(l '
build_ssa "$WORK/inline.ssa" "$WORK/inline"
"$WORK/inline" > "$WORK/inline.out"
cmp "$INLINE/expected.out" "$WORK/inline.out"
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
awk 'BEGIN { feature = 0; include = 0 }
     /^#define _GNU_SOURCE$/ { feature = NR }
     /^#include/ && include == 0 { include = NR }
     END { exit !(feature != 0 && include != 0 && feature < include) }' \
    "$WORK/typed-stubs.c"
if grep -E '#include <(time|sys/stat|dirent|pwd|grp|sys/utsname|sys/resource)\.h>' \
        "$WORK/typed-stubs.c" >/dev/null; then
    echo "C stub includes a POSIX header" >&2
    exit 1
fi
cc -std=c99 -Wall -Wextra -Wpedantic -Werror -I "$ROOT/runtime" -c \
    -o "$WORK/typed-stubs.o" "$WORK/typed-stubs.c"
cc -std=c99 -Wall -Wextra -Werror -c -o "$WORK/typed.o" "$EXTENSION/typed.c"
emit "$WORK/compiler" "$EXTENSION/typed.npl" "$WORK/typed.ssa"
"$QBE" -o "$WORK/typed.s" "$WORK/typed.ssa"
cc -o "$WORK/typed" "$WORK/typed.s" "$WORK/typed-stubs.o" "$WORK/typed.o" \
    "$RUNTIME" -lm
"$WORK/typed" > "$WORK/typed.out"
cmp "$EXTENSION/typed.out" "$WORK/typed.out"
# define-c-struct layout probe and a struct: typed function argument.
"$WORK/compiler" --emit=c-stubs --stdlib-root "$ROOT/lib" "$EXTENSION/structs.npl" \
    > "$WORK/structs-stubs.c"
cc -std=c99 -Wall -Wextra -Wpedantic -Werror -I "$ROOT/runtime" -c \
    -o "$WORK/structs-stubs.o" "$WORK/structs-stubs.c"
cc -std=c99 -Wall -Wextra -Werror -c -o "$WORK/time.o" "$EXTENSION/time.c"
emit "$WORK/compiler" "$EXTENSION/structs.npl" "$WORK/structs.ssa"
"$QBE" -o "$WORK/structs.s" "$WORK/structs.ssa"
cc -o "$WORK/structs" "$WORK/structs.s" "$WORK/structs-stubs.o" \
    "$WORK/time.o" "$RUNTIME" -lm
"$WORK/structs" > "$WORK/structs.out"
cmp "$EXTENSION/structs.out" "$WORK/structs.out"
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
reject_compile contracts/c-struct-type.npl \
    c-struct-type "define-c-struct: unknown field type: nope"
reject_compile contracts/c-struct-probe.npl \
    c-struct-probe "C probe compilation failed"
reject_compile contracts/c-primitive-nested.npl \
    c-primitive-nested "define-c-primitive is valid at top level only"
reject_compile contracts/c-primitive-type.npl \
    c-primitive-type "unknown C primitive type: pear"
reject_compile contracts/c-primitive-conflict.npl \
    c-primitive-conflict "C primitive declaration conflicts with registered npl_car"
reject_compile contracts/c-primitive-values-conflict.npl \
    c-primitive-values-conflict \
    "C primitive declaration conflicts with registered npl_car"
reject_compile contracts/record-unknown-field.npl \
    record-unknown-field "define-record-type: unknown field z"
reject_compile contracts/record-field-spec.npl \
    record-field-spec "define-record-type field needs an accessor"
reject_compile contracts/match-repeated-variable.npl \
    match-repeated-variable "match: repeated pattern variable"
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
reject_reader bad-bracket-close "mismatched ]"
reject_reader bad-bracket-open "mismatched )"
reject_reader bad-bracket-extra "unmatched ]"
reject_reader bad-bracket-dotted "mismatched )"

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
reject_compile macro-errors/multiple-ellipsis-pattern.npl \
    multiple-ellipsis-pattern "misplaced ellipsis in pattern"
reject_compile macro-errors/template-ellipsis-depth.npl \
    template-ellipsis-depth "pattern variable used at wrong ellipsis depth"
reject_reader backslash-identifier "bad character byte 92"
reject_compile contracts/duplicate-definition.npl \
    duplicate-definition "duplicate top-level definition"


echo "full compiler dynamic core: PASS"
echo "full compiler reader/datum: PASS"
echo "full compiler macros: PASS"

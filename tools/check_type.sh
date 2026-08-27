#!/bin/bash
# check_type.sh — Phase 5 annotation, unboxed codegen, and guard acceptance.

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD="$ROOT/build"
NPLC0="$BUILD/stage0/nplc0"
QBE="$BUILD/qbe/qbe"
RUNTIME="$BUILD/runtime/libnpl_runtime.a"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

fail() { echo "FAIL: $1" >&2; exit 1; }
pass() { echo "PASS: $1"; }

ninja -C "$BUILD" nplc0 qbe_build >/dev/null
"$NPLC0" "$ROOT/src/compiler.npl" -o "$WORK/nplc1" >/dev/null

emit_stage1() {
    local source="$1" output="$2" rc=0
    "$WORK/nplc1" "$source" > "$output" 2> "$output.err" || rc=$?
    [ "$rc" -eq 44 ] || {
        cat "$output" "$output.err" >&2
        fail "nplc1 emission returned $rc"
    }
}

build_ssa() {
    local ssa="$1" output="$2"
    "$QBE" -o "$output.s" "$ssa"
    cc -o "$output" "$output.s" "$RUNTIME"
}

run_expect() {
    local binary="$1" expected="$2" rc=0
    "$binary" >/dev/null 2>&1 || rc=$?
    [ "$rc" -eq "$expected" ] || fail "$binary expected $expected, got $rc"
}

echo "=== NPL Type System Tests ==="

cat > "$WORK/shape.npl" << 'EOF'
(define x : Integer 5)
(define (add (a : Integer) (b : Integer)) : Integer (+ a b))
EOF
shape=$("$NPLC0" --print-ast "$WORK/shape.npl")
printf '%s' "$shape" | grep -Fq '(x : Integer)' || fail "variable annotation AST"
printf '%s' "$shape" | grep -Fq '((add (a : Integer) (b : Integer)) : Integer)' \
    || fail "function annotation AST"
pass "reader preserves variable, parameter, and return annotations"

cat > "$WORK/bad-return.npl" << 'EOF'
(define (bad (x : Integer)) : Bool (+ x 1))
(define (main) 0)
EOF
rc=0
"$NPLC0" --emit-qbe "$WORK/bad-return.npl" >"$WORK/bad0.out" 2>"$WORK/bad0.err" || rc=$?
[ "$rc" -ne 0 ] || fail "nplc0 accepted an invalid return annotation"
[ ! -s "$WORK/bad0.out" ] || fail "nplc0 emitted IL before type failure"
grep -Fq 'expected return Bool, got Integer' "$WORK/bad0.err" \
    || fail "nplc0 return mismatch diagnostic"
rc=0
"$WORK/nplc1" "$WORK/bad-return.npl" >"$WORK/bad1.out" 2>"$WORK/bad1.err" || rc=$?
[ "$rc" -ne 44 ] || fail "nplc1 accepted an invalid return annotation"
grep -Fq 'typed return mismatch in bad' "$WORK/bad1.out" \
    || fail "nplc1 return mismatch diagnostic"
pass "nplc0 and nplc1 reject contradictory annotations"

cat > "$WORK/add.npl" << 'EOF'
(define (add (a : Integer) (b : Integer)) : Integer (+ a b))
(define (main) (add 20 22))
EOF
"$NPLC0" --emit-qbe "$WORK/add.npl" > "$WORK/add0.ssa"
grep -Fq 'function w $add__typed(w %a, w %b)' "$WORK/add0.ssa" \
    || fail "nplc0 missing unboxed add body"
grep -Fq '=w add %a, %b' "$WORK/add0.ssa" || fail "nplc0 add is not unboxed"
grep -Fq 'call $npl_guard_integer' "$WORK/add0.ssa" || fail "nplc0 guard missing"
build_ssa "$WORK/add0.ssa" "$WORK/add0"
run_expect "$WORK/add0" 168

emit_stage1 "$WORK/add.npl" "$WORK/add1.ssa"
grep -Fq 'function w $add__typed(w %p0, w %p1)' "$WORK/add1.ssa" \
    || fail "nplc1 missing unboxed add body"
grep -Fq '=w add %p0, %p1' "$WORK/add1.ssa" || fail "nplc1 add is not unboxed"
grep -Fq 'call $npl_guard_integer' "$WORK/add1.ssa" || fail "nplc1 guard missing"
build_ssa "$WORK/add1.ssa" "$WORK/add1"
run_expect "$WORK/add1" 168
pass "nplc0 and nplc1 emit and run the same unboxed Integer function"

cat > "$WORK/direct.npl" << 'EOF'
(define (double (x : Integer)) : Integer (+ x x))
(define (plus-one-double (x : Integer)) : Integer (+ (double x) 1))
(define (main) (plus-one-double 20))
EOF
"$NPLC0" --emit-qbe "$WORK/direct.npl" > "$WORK/direct0.ssa"
grep -Fq 'call $double__typed(w ' "$WORK/direct0.ssa" \
    || fail "nplc0 typed-to-typed call was boxed"
build_ssa "$WORK/direct0.ssa" "$WORK/direct0"
run_expect "$WORK/direct0" 164
emit_stage1 "$WORK/direct.npl" "$WORK/direct1.ssa"
grep -Fq 'call $double__typed(w ' "$WORK/direct1.ssa" \
    || fail "nplc1 typed-to-typed call was boxed"
build_ssa "$WORK/direct1.ssa" "$WORK/direct1"
run_expect "$WORK/direct1" 164
pass "typed-to-typed direct calls remain unboxed in both compilers"

cat > "$WORK/bool.npl" << 'EOF'
(define (positive (x : Integer)) : Bool (> x 0))
(define (main) (if (positive 3) 7 9))
EOF
"$NPLC0" --emit-qbe "$WORK/bool.npl" > "$WORK/bool0.ssa"
build_ssa "$WORK/bool0.ssa" "$WORK/bool0"
run_expect "$WORK/bool0" 28
emit_stage1 "$WORK/bool.npl" "$WORK/bool1.ssa"
build_ssa "$WORK/bool1.ssa" "$WORK/bool1"
run_expect "$WORK/bool1" 28
pass "Bool results are unboxed internally and boxed at Dynamic boundaries"

cat > "$WORK/float.npl" << 'EOF'
(define (sum (a : Float) (b : Float)) : Float (+ a b))
(define (is-three (x : Float)) : Bool (= (sum x 1.5) 3.0))
(define (main) (if (is-three 1.5) 7 9))
EOF
"$NPLC0" --emit-qbe "$WORK/float.npl" > "$WORK/float0.ssa"
grep -Fq 'function d $sum__typed(d %a, d %b)' "$WORK/float0.ssa" \
    || fail "nplc0 missing unboxed Float body"
grep -Fq 'call $npl_guard_float' "$WORK/float0.ssa" \
    || fail "nplc0 Float guard missing"
build_ssa "$WORK/float0.ssa" "$WORK/float0"
run_expect "$WORK/float0" 28
emit_stage1 "$WORK/float.npl" "$WORK/float1.ssa"
grep -Fq 'function d $sum__typed(d %p0, d %p1)' "$WORK/float1.ssa" \
    || fail "nplc1 missing unboxed Float body"
grep -Fq 'call $npl_guard_float' "$WORK/float1.ssa" \
    || fail "nplc1 Float guard missing"
build_ssa "$WORK/float1.ssa" "$WORK/float1"
run_expect "$WORK/float1" 28
pass "Float arithmetic, comparison, guards, and boxing agree across both compilers"

cat > "$WORK/guard.npl" << 'EOF'
(define (identity x) x)
(define (inc (x : Integer)) : Integer (+ x 1))
(define (main) (inc (identity "bad")))
EOF
"$NPLC0" -o "$WORK/guard0" "$WORK/guard.npl" >/dev/null
rc=0
"$WORK/guard0" >"$WORK/guard0.out" 2>"$WORK/guard0.err" || rc=$?
[ "$rc" -eq 1 ] || fail "nplc0 Dynamic guard expected exit 1, got $rc"
grep -Fq 'type guard: expected Integer' "$WORK/guard0.err" \
    || fail "nplc0 Dynamic guard diagnostic"
emit_stage1 "$WORK/guard.npl" "$WORK/guard1.ssa"
build_ssa "$WORK/guard1.ssa" "$WORK/guard1"
rc=0
"$WORK/guard1" >"$WORK/guard1.out" 2>"$WORK/guard1.err" || rc=$?
[ "$rc" -eq 1 ] || fail "nplc1 Dynamic guard expected exit 1, got $rc"
grep -Fq 'type guard: expected Integer' "$WORK/guard1.err" \
    || fail "nplc1 Dynamic guard diagnostic"
pass "Dynamic-to-Integer boundaries fail fast in both compilers"

cat > "$WORK/globals-shadow.npl" << 'EOF'
(define answer : Integer 5)
(define (after-shadow (x : Integer)) : Integer
  (begin (let ((x #t)) x) (+ x 1)))
(define (main) (+ answer (after-shadow 4)))
EOF
"$NPLC0" --emit-qbe "$WORK/globals-shadow.npl" > "$WORK/globals0.ssa"
build_ssa "$WORK/globals0.ssa" "$WORK/globals0"
run_expect "$WORK/globals0" 40
emit_stage1 "$WORK/globals-shadow.npl" "$WORK/globals1.ssa"
build_ssa "$WORK/globals1.ssa" "$WORK/globals1"
run_expect "$WORK/globals1" 40
pass "typed globals and lexical type shadowing agree across both compilers"

cat > "$WORK/hm-poly.npl" << 'EOF'
(define (poly (x : Integer)) : Integer
  (let ((id (lambda (v) v)))
    (let ((a (id x)))
      (if (id #t) (+ a 1) 0))))
(define (main) (poly 41))
EOF
"$NPLC0" --emit-qbe "$WORK/hm-poly.npl" > "$WORK/hm-poly.ssa"
grep -Fq 'call $npl_apply' "$WORK/hm-poly.ssa" \
    || fail "HM polymorphic closure application missing"
build_ssa "$WORK/hm-poly.ssa" "$WORK/hm-poly"
run_expect "$WORK/hm-poly" 168
pass "local HM let-generalization instantiates identity at Integer and Bool"

cat > "$WORK/typed-lambda.npl" << 'EOF'
(define (use-capture (n : Integer)) : Integer
  (let ((f (lambda ((x : Integer)) : Integer (+ x n))))
    (f 5)))
(define (main) (use-capture 37))
EOF
"$NPLC0" --emit-qbe "$WORK/typed-lambda.npl" > "$WORK/typed-lambda.ssa"
grep -Fq 'function w $_typed_lambda_' "$WORK/typed-lambda.ssa" \
    || fail "unboxed typed lambda body missing"
grep -Fq 'call $npl_environment_ref' "$WORK/typed-lambda.ssa" \
    || fail "typed lambda capture environment missing"
build_ssa "$WORK/typed-lambda.ssa" "$WORK/typed-lambda"
run_expect "$WORK/typed-lambda" 168
pass "explicit typed lambda captures an Integer through a managed environment"

cat > "$WORK/self-lambda.npl" << 'EOF'
(define (apply-one (f : (-> Integer Integer)) (x : Integer)) : Integer
  (f x))
(define (local-lambda (x : Integer)) : Integer
  (let ((f (lambda ((y : Integer)) : Integer (+ y 1))))
    (f x)))
(define (main)
  (+ (apply-one (lambda ((z : Integer)) : Integer (+ z 1)) 20)
     (local-lambda 20)))
EOF
emit_stage1 "$WORK/self-lambda.npl" "$WORK/self-lambda.ssa"
grep -Fq 'function l $_typed_lambda_' "$WORK/self-lambda.ssa" \
    || fail "nplc1 generic typed lambda entry missing"
grep -Fq '__typed(l %typed_env, w %lp0)' "$WORK/self-lambda.ssa" \
    || fail "nplc1 unboxed typed lambda body missing"
grep -Fq 'call $npl_guard_closure' "$WORK/self-lambda.ssa" \
    || fail "nplc1 function boundary guard missing"
grep -Fq 'call $npl_apply' "$WORK/self-lambda.ssa" \
    || fail "nplc1 typed closure apply missing"
build_ssa "$WORK/self-lambda.ssa" "$WORK/self-lambda"
run_expect "$WORK/self-lambda" 168

cat > "$WORK/self-lambda-capture.npl" << 'EOF'
(define (bad-capture (n : Integer)) : Integer
  (let ((f (lambda ((x : Integer)) : Integer (+ x n))))
    (f 1)))
(define (main) 0)
EOF
rc=0
"$WORK/nplc1" "$WORK/self-lambda-capture.npl" \
    > "$WORK/self-lambda-capture.out" 2> "$WORK/self-lambda-capture.err" || rc=$?
[ "$rc" -ne 44 ] || fail "nplc1 silently accepted a typed lambda capture"
grep -Fq 'typed unbound n' "$WORK/self-lambda-capture.out" \
    || fail "nplc1 typed lambda capture diagnostic missing"
pass "self-hosted unary typed lambdas and function parameters use managed closures"

cat > "$WORK/compound.npl" << 'EOF'
(define (first-plus (p : (Pair Integer Integer)) (n : Integer)) : Integer
  (+ (car p) n))
(define (apply-one (f : (-> Integer Integer)) (x : Integer)) : Integer
  (f x))
(define (main)
  (+ (first-plus (cons 1 2) 40)
     (apply-one (lambda ((y : Integer)) : Integer (+ y 1)) 0)))
EOF
"$NPLC0" --emit-qbe "$WORK/compound.npl" > "$WORK/compound.ssa"
grep -Fq 'call $npl_guard_pair' "$WORK/compound.ssa" \
    || fail "typed Pair boundary guard missing"
grep -Fq 'call $npl_guard_closure' "$WORK/compound.ssa" \
    || fail "typed function boundary guard missing"
grep -Eq 'call \$npl_(apply|tail_request_closure)' "$WORK/compound.ssa" \
    || fail "typed function parameter application missing"
build_ssa "$WORK/compound.ssa" "$WORK/compound"
run_expect "$WORK/compound" 168
pass "Pair and function annotations lower through typed pointer guards"

cat > "$WORK/pair-self-host.npl" << 'EOF'
(define (first-plus (p : (Pair Integer Integer)) (n : Integer)) : Integer
  (+ (car p) n))
(define (typed-pair (x : Integer)) : Integer
  (cdr (cons #t x)))
(define (main) (+ (first-plus (cons 1 2) 39) (typed-pair 1)))
EOF
"$NPLC0" --emit-qbe "$WORK/pair-self-host.npl" > "$WORK/pair0.ssa"
build_ssa "$WORK/pair0.ssa" "$WORK/pair0"
run_expect "$WORK/pair0" 164
emit_stage1 "$WORK/pair-self-host.npl" "$WORK/pair1.ssa"
grep -Fq 'function w $first_plus__typed(l %p0, w %p1)' "$WORK/pair1.ssa" \
    || fail "nplc1 Pair parameter is not a typed pointer"
grep -Fq 'call $npl_guard_pair' "$WORK/pair1.ssa" \
    || fail "nplc1 Pair boundary guard missing"
grep -Fq 'call $npl_box_bool' "$WORK/pair1.ssa" \
    || fail "nplc1 typed cons did not box its Bool element"
grep -Fq 'call $npl_cdr' "$WORK/pair1.ssa" \
    || fail "nplc1 typed cdr lowering missing"
build_ssa "$WORK/pair1.ssa" "$WORK/pair1"
run_expect "$WORK/pair1" 164

cat > "$WORK/pair-mismatch.npl" << 'EOF'
(define (first (p : (Pair Integer Integer))) : Integer (car p))
(define (bad (x : Integer)) : Integer (first (cons #t x)))
(define (main) 0)
EOF
rc=0
"$WORK/nplc1" "$WORK/pair-mismatch.npl" > "$WORK/pair-bad.out" \
    2> "$WORK/pair-bad.err" || rc=$?
[ "$rc" -ne 44 ] || fail "nplc1 accepted mismatched Pair element types"
grep -Fq 'typed call argument mismatch' "$WORK/pair-bad.out" \
    || fail "nplc1 Pair mismatch diagnostic missing"
pass "self-hosted Pair parsing, guards, construction, and access are typed"

cat > "$WORK/tail.npl" << 'EOF'
(define (count (n : Integer) (acc : Integer)) : Integer
  (if (= n 0) acc (count (- n 1) (+ acc 1))))
(define (main) (count 10000000 7))
EOF
"$NPLC0" --emit-qbe "$WORK/tail.npl" > "$WORK/tail.ssa"
sed -n '/function w $count__typed/,/^}/p' "$WORK/tail.ssa" > "$WORK/count.ssa"
grep -Fq 'jmp @typed_tail' "$WORK/count.ssa" || fail "typed self-tail loop missing"
if grep -Fq 'call $count__typed' "$WORK/count.ssa"; then
    fail "typed self-tail body still calls itself"
fi
build_ssa "$WORK/tail.ssa" "$WORK/tail"
run_expect "$WORK/tail" 28
emit_stage1 "$WORK/tail.npl" "$WORK/tail1.ssa"
sed -n '/function w $count__typed/,/^}/p' "$WORK/tail1.ssa" > "$WORK/count1.ssa"
grep -Fq 'jmp @typed_tail' "$WORK/count1.ssa" \
    || fail "nplc1 typed self-tail loop missing"
if grep -Fq 'call $count__typed' "$WORK/count1.ssa"; then
    fail "nplc1 typed self-tail body still calls itself"
fi
build_ssa "$WORK/tail1.ssa" "$WORK/tail1"
run_expect "$WORK/tail1" 28
pass "both compilers run 10000000 unboxed self-tail iterations"

cat > "$WORK/float-tail.npl" << 'EOF'
(define (float-count (n : Float) (acc : Float)) : Float
  (if (= n 0.0) acc (float-count (- n 1.0) (+ acc 1.0))))
(define (float-ok (x : Float)) : Bool (= x 100007.0))
(define (main) (if (float-ok (float-count 100000.0 7.0)) 7 9))
EOF
"$NPLC0" --emit-qbe "$WORK/float-tail.npl" > "$WORK/float-tail0.ssa"
sed -n '/function d $float_count__typed/,/^}/p' "$WORK/float-tail0.ssa" \
    > "$WORK/float-count0.ssa"
grep -Fq 'jmp @typed_tail' "$WORK/float-count0.ssa" \
    || fail "nplc0 Float self-tail loop missing"
build_ssa "$WORK/float-tail0.ssa" "$WORK/float-tail0"
run_expect "$WORK/float-tail0" 28
emit_stage1 "$WORK/float-tail.npl" "$WORK/float-tail1.ssa"
sed -n '/function d $float_count__typed/,/^}/p' "$WORK/float-tail1.ssa" \
    > "$WORK/float-count1.ssa"
grep -Fq 'stored ' "$WORK/float-count1.ssa" \
    || fail "nplc1 Float tail merge does not store double"
grep -Fq 'loadd ' "$WORK/float-count1.ssa" \
    || fail "nplc1 Float tail merge does not load double"
build_ssa "$WORK/float-tail1.ssa" "$WORK/float-tail1"
run_expect "$WORK/float-tail1" 28
pass "both compilers preserve Float values through unboxed self-tail loops"

echo "=== Type-system acceptance passed ==="

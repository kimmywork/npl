#!/bin/bash
# check_control.sh — Phase 4 trampoline, managed closure, and escape checks.

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD="$ROOT/build"
NPLC0="$BUILD/stage0/nplc0"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

fail() { echo "FAIL: $1" >&2; exit 1; }
pass() { echo "PASS: $1"; }

ninja -C "$BUILD" nplc0 >/dev/null

cat > "$WORK/mutual.npl" << 'EOF'
(define (odd-step n) (if (= n 0) 9 (even-step (- n 1))))
(define (even-step n) (if (= n 0) 7 (odd-step (- n 1))))
(define (main) (even-step 10000000))
EOF
"$NPLC0" --emit-qbe "$WORK/mutual.npl" > "$WORK/mutual.ssa"
sed -n '/function l $odd_step__body/,/^}/p' "$WORK/mutual.ssa" > "$WORK/odd-body.ssa"
sed -n '/function l $even_step__body/,/^}/p' "$WORK/mutual.ssa" > "$WORK/even-body.ssa"
grep -Fq 'call $npl_tail_request(l $even_step__body' "$WORK/odd-body.ssa" \
    || fail "odd-step did not emit a tail request"
grep -Fq 'call $npl_tail_request(l $odd_step__body' "$WORK/even-body.ssa" \
    || fail "even-step did not emit a tail request"
if grep -Eq 'call \$(odd_step|even_step)\(' "$WORK/odd-body.ssa" "$WORK/even-body.ssa"; then
    fail "mutual tail bodies still contain recursive fixed-ABI calls"
fi
"$NPLC0" -o "$WORK/mutual-bin" "$WORK/mutual.npl" >/dev/null
rc=0
"$WORK/mutual-bin" >/dev/null 2>&1 || rc=$?
[ "$rc" -eq 28 ] || fail "mutual tail recursion expected 28, got $rc"
pass "10000000 mutual tail calls run through the dispatcher"

cat > "$WORK/callcc.npl" << 'EOF'
(define (main) (call/cc (lambda (k) (+ 1 (k 42)))))
EOF
"$NPLC0" -o "$WORK/callcc" "$WORK/callcc.npl" >/dev/null
rc=0
"$WORK/callcc" >/dev/null 2>&1 || rc=$?
[ "$rc" -eq 168 ] || fail "call/cc escape expected tagged 42 (168), got $rc"
pass "call/cc escapes a non-tail context with 42"

cat > "$WORK/callcc-nested.npl" << 'EOF'
(define (main)
  (call-with-current-continuation
    (lambda (outer)
      (+ 1 (call/cc (lambda (inner) (outer 42)))))))
EOF
"$NPLC0" -o "$WORK/callcc-nested" "$WORK/callcc-nested.npl" >/dev/null
rc=0
"$WORK/callcc-nested" >/dev/null 2>&1 || rc=$?
[ "$rc" -eq 168 ] || fail "nested escape expected tagged 42 (168), got $rc"
pass "nested escape restores the outer continuation boundary"

cat > "$WORK/callcc-expired.npl" << 'EOF'
(define (main) ((call/cc (lambda (k) k)) 1))
EOF
"$NPLC0" -o "$WORK/callcc-expired" "$WORK/callcc-expired.npl" >/dev/null
rc=0
"$WORK/callcc-expired" >"$WORK/expired.out" 2>"$WORK/expired.err" || rc=$?
[ "$rc" -eq 1 ] || fail "expired continuation expected exit 1, got $rc"
grep -Fq 'outside its dynamic extent' "$WORK/expired.err" \
    || fail "expired continuation diagnostic missing"
pass "escape continuation is rejected outside its dynamic extent"

cat > "$WORK/closure-gc.npl" << 'EOF'
(define (make-adder n) (lambda (x) (+ x n)))
(define (churn n)
  (if (= n 0) 0 (begin (cons n ()) (churn (- n 1)))))
(define (main)
  (let ((add5 (make-adder 5)))
    (begin (churn 200000) (add5 10))))
EOF
"$NPLC0" -o "$WORK/closure-gc" "$WORK/closure-gc.npl" >/dev/null
rc=0
"$WORK/closure-gc" >/dev/null 2>&1 || rc=$?
[ "$rc" -eq 60 ] || fail "managed closure after GC expected 60, got $rc"
pass "managed closure environment survives automatic collection"

cat > "$WORK/map-100k.npl" << 'EOF'
(define (build-list n acc)
  (if (= n 0) acc (build-list (- n 1) (cons n acc))))
(define (map-acc f xs acc)
  (if (null? xs)
      acc
      (map-acc f (cdr xs) (cons (f (car xs)) acc))))
(define (list-length xs n)
  (if (null? xs) n (list-length (cdr xs) (+ n 1))))
(define (main)
  (let ((mapped (map-acc (lambda (x) (+ x 1))
                         (build-list 100000 ()) ())))
    (if (= (list-length mapped 0) 100000) 7 9)))
EOF
"$NPLC0" -o "$WORK/map-100k" "$WORK/map-100k.npl" >/dev/null
rc=0
"$WORK/map-100k" >/dev/null 2>&1 || rc=$?
[ "$rc" -eq 28 ] || fail "100000-element map expected 28, got $rc"
pass "tail-recursive map over 100000 elements preserves length"

echo "=== Control-flow acceptance passed ==="

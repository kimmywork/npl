#!/bin/bash
# check_r7rs.sh — R7RS conformance tests for NPL
# Usage: ./tools/check_r7rs.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_DIR/build"
NPLC0="$BUILD_DIR/stage0/nplc0"

RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m'

pass() { echo -e "${GREEN}PASS${NC}: $1"; }
fail() { echo -e "${RED}FAIL${NC}: $1"; exit 1; }

echo "=== R7RS Conformance Tests ==="
echo ""

# Test 1: List operations
echo "Test 1: List length"
cat > /tmp/r7rs_01.npl << 'EOF'
(define (list-length lst)
  (if (null? lst) 0 (+ 1 (list-length (cdr lst)))))
(define (main) (list-length (cons 1 (cons 2 (cons 3 ())))))
EOF
$NPLC0 -o /tmp/r7rs_01 /tmp/r7rs_01.npl 2>&1
result=$(/tmp/r7rs_01; echo $?)
[ "$result" -eq 12 ] && pass "list-length(3) = 12 (3<<2)" || fail "expected 12, got $result"

# Test 2: List append
echo "Test 2: List append"
cat > /tmp/r7rs_02.npl << 'EOF'
(define (list-append a b)
  (if (null? a) b (cons (car a) (list-append (cdr a) b))))
(define (list-length lst)
  (if (null? lst) 0 (+ 1 (list-length (cdr lst)))))
(define (main)
  (let ((a (cons 1 (cons 2 ())))
        (b (cons 3 (cons 4 ()))))
    (list-length (list-append a b))))
EOF
$NPLC0 -o /tmp/r7rs_02 /tmp/r7rs_02.npl 2>&1
result=$(/tmp/r7rs_02; echo $?)
[ "$result" -eq 16 ] && pass "append length = 16 (4<<2)" || fail "expected 16, got $result"

# Test 3: List reverse
echo "Test 3: List reverse"
cat > /tmp/r7rs_03.npl << 'EOF'
(define (list-rev-acc lst acc)
  (if (null? lst) acc (list-rev-acc (cdr lst) (cons (car lst) acc))))
(define (list-reverse lst) (list-rev-acc lst ()))
(define (list-length lst)
  (if (null? lst) 0 (+ 1 (list-length (cdr lst)))))
(define (main)
  (let ((lst (cons 1 (cons 2 (cons 3 ())))))
    (list-length (list-reverse lst))))
EOF
$NPLC0 -o /tmp/r7rs_03 /tmp/r7rs_03.npl 2>&1
result=$(/tmp/r7rs_03; echo $?)
[ "$result" -eq 12 ] && pass "reverse length = 12 (3<<2)" || fail "expected 12, got $result"

# Test 4: Map
echo "Test 4: List map"
cat > /tmp/r7rs_04.npl << 'EOF'
(define (list-map f lst)
  (if (null? lst) () (cons (f (car lst)) (list-map f (cdr lst)))))
(define (list-length lst)
  (if (null? lst) 0 (+ 1 (list-length (cdr lst)))))
(define (double x) (+ x x))
(define (main)
  (let ((lst (cons 1 (cons 2 (cons 3 ())))))
    (list-length (list-map double lst))))
EOF
$NPLC0 -o /tmp/r7rs_04 /tmp/r7rs_04.npl 2>&1
result=$(/tmp/r7rs_04; echo $?)
[ "$result" -eq 12 ] && pass "map length = 12 (3<<2)" || fail "expected 12, got $result"

# Test 5: Fold
echo "Test 5: List fold"
cat > /tmp/r7rs_05.npl << 'EOF'
(define (list-fold f init lst)
  (if (null? lst) init (list-fold f (f init (car lst)) (cdr lst))))
(define (main)
  (let ((lst (cons 1 (cons 2 (cons 3 ())))))
    (list-fold (lambda (acc x) (+ acc x)) 0 lst)))
EOF
$NPLC0 -o /tmp/r7rs_05 /tmp/r7rs_05.npl 2>&1
result=$(/tmp/r7rs_05; echo $?)
[ "$result" -eq 24 ] && pass "fold sum = 24 (6<<2)" || fail "expected 24, got $result"

# Test 6: Higher-order functions
echo "Test 6: Higher-order function (apply)"
cat > /tmp/r7rs_06.npl << 'EOF'
(define (apply-fn f x) (f x))
(define (main) (apply-fn (lambda (x) (+ x 1)) 21))
EOF
$NPLC0 -o /tmp/r7rs_06 /tmp/r7rs_06.npl 2>&1
result=$(/tmp/r7rs_06; echo $?)
[ "$result" -eq 88 ] && pass "apply-fn = 88 (22<<2)" || fail "expected 88, got $result"

# Test 7: Closure
echo "Test 7: Closure (make-adder)"
cat > /tmp/r7rs_07.npl << 'EOF'
(define (make-adder n) (lambda (x) (+ x n)))
(define (main) ((make-adder 5) 10))
EOF
$NPLC0 -o /tmp/r7rs_07 /tmp/r7rs_07.npl 2>&1
result=$(/tmp/r7rs_07; echo $?)
[ "$result" -eq 60 ] && pass "closure = 60 (15<<2)" || fail "expected 60, got $result"

# Test 8: Recursive factorial
echo "Test 8: Recursive factorial"
cat > /tmp/r7rs_08.npl << 'EOF'
(define (fact n) (if (<= n 1) 1 (* n (fact (- n 1)))))
(define (main) (fact 5))
EOF
$NPLC0 -o /tmp/r7rs_08 /tmp/r7rs_08.npl 2>&1
result=$(/tmp/r7rs_08; echo $?)
# 5! = 120, (120<<2) % 256 = 480 % 256 = 224
[ "$result" -eq 224 ] && pass "5! = 224 ((120<<2)%256)" || fail "expected 224, got $result"

# Test 9: Tail recursion (fibonacci)
echo "Test 9: Fibonacci"
cat > /tmp/r7rs_09.npl << 'EOF'
(define (fib n) (if (< n 2) n (+ (fib (- n 1)) (fib (- n 2)))))
(define (main) (fib 10))
EOF
$NPLC0 -o /tmp/r7rs_09 /tmp/r7rs_09.npl 2>&1
result=$(/tmp/r7rs_09; echo $?)
[ "$result" -eq 220 ] && pass "fib(10) = 220 (55<<2)" || fail "expected 220, got $result"

rm -f /tmp/r7rs_*

echo ""
echo "=== All R7RS conformance tests passed ==="

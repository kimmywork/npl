#!/bin/bash
# check_stage0_qbe.sh — Verify Phase 2 (QBE IL codegen) works correctly.
# Usage: ./tools/check_stage0_qbe.sh
# Exit 0 on success, non-zero on failure.

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_DIR/build"
NPLC0="$BUILD_DIR/stage0/nplc0"

RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m' # No Color

pass() { echo -e "${GREEN}PASS${NC}: $1"; }
fail() { echo -e "${RED}FAIL${NC}: $1"; exit 1; }

echo "=== NPL Stage0 QBE Codegen Tests ==="
echo ""

# Build nplc0 if needed
if [ ! -x "$NPLC0" ]; then
    echo "Building nplc0..."
    ninja -C "$BUILD_DIR" nplc0
fi

# Test 1: Simple arithmetic
echo "Test 1: Simple arithmetic (42 * 42)"
cat > /tmp/npl_test_arith.npl << 'EOF'
(define (square x) (* x x))
(define (main) (square 42))
EOF
$NPLC0 -o /tmp/npl_test_arith /tmp/npl_test_arith.npl 2>&1
result=$(/tmp/npl_test_arith; echo $?)
# With tagged pointers: 42 << 2 = 168, extract 42, 42*42=1764, 1764<<2=7056, 7056%256=144
[ "$result" -eq 144 ] && pass "42*42 with tagged pointers, exit code 144" || fail "expected 144, got $result"

# Test 2: Multiple arithmetic operations
echo "Test 2: Multiple arithmetic operations (add/sub only)"
cat > /tmp/npl_test_multi.npl << 'EOF'
(define (add a b) (+ a b))
(define (sub a b) (- a b))
(define (main) (add (add 3 4) (sub 10 5)))
EOF
$NPLC0 -o /tmp/npl_test_multi /tmp/npl_test_multi.npl 2>&1
result=$(/tmp/npl_test_multi; echo $?)
# With tagged pointers: 3<<2=12, 4<<2=16, 12+16=28, 10<<2=40, 5<<2=20, 40-20=20, 28+20=48, 48%256=48
[ "$result" -eq 48 ] && pass "add/sub with tagged pointers, exit code 48" || fail "expected 48, got $result"

# Test 3: If expression (true branch)
echo "Test 3: If expression (true branch)"
cat > /tmp/npl_test_if_true.npl << 'EOF'
(define (abs x)
  (if (< x 0) (- 0 x) x))
(define (main) (abs -5))
EOF
$NPLC0 -o /tmp/npl_test_if_true /tmp/npl_test_if_true.npl 2>&1
result=$(/tmp/npl_test_if_true; echo $?)
# With tagged pointers: -5<<2=-20, 0<<2=0, 0-(-20)=20, 20%256=20
[ "$result" -eq 20 ] && pass "abs(-5) with tagged pointers, exit code 20" || fail "expected 20, got $result"

# Test 4: If expression (false branch)
echo "Test 4: If expression (false branch)"
cat > /tmp/npl_test_if_false.npl << 'EOF'
(define (abs x)
  (if (< x 0) (- 0 x) x))
(define (main) (abs 5))
EOF
$NPLC0 -o /tmp/npl_test_if_false /tmp/npl_test_if_false.npl 2>&1
result=$(/tmp/npl_test_if_false; echo $?)
# With tagged pointers: 5<<2=20, 20%256=20
[ "$result" -eq 20 ] && pass "abs(5) with tagged pointers, exit code 20" || fail "expected 20, got $result"

# Test 5: Recursive addition (simple recursion test)
echo "Test 5: Recursive addition"
cat > /tmp/npl_test_fact.npl << 'EOF'
(define (sum-to n)
  (if (<= n 0) 0 (+ n (sum-to (- n 1)))))
(define (main) (sum-to 5))
EOF
$NPLC0 -o /tmp/npl_test_fact /tmp/npl_test_fact.npl 2>&1
result=$(/tmp/npl_test_fact; echo $?)
# sum-to(5) = 5+4+3+2+1+0 = 15, with tagged pointers: 15<<2=60, 60%256=60
[ "$result" -eq 60 ] && pass "sum-to(5) with tagged pointers, exit code 60" || fail "expected 60, got $result"

# Test 6: Fibonacci (recursive, addition only)
echo "Test 6: Fibonacci (recursive)"
cat > /tmp/npl_test_fib.npl << 'EOF'
(define (fib n)
  (if (< n 2) n (+ (fib (- n 1)) (fib (- n 2)))))
(define (main) (fib 10))
EOF
$NPLC0 -o /tmp/npl_test_fib /tmp/npl_test_fib.npl 2>&1
result=$(/tmp/npl_test_fib; echo $?)
# fib(10) = 55, with tagged pointers: 55<<2=220, 220%256=220
[ "$result" -eq 220 ] && pass "fib(10) with tagged pointers, exit code 220" || fail "expected 220, got $result"

# Test 7: QBE IL emit mode
echo "Test 7: QBE IL emit mode"
output=$($NPLC0 --emit-qbe stage0/tests/hello.npl 2>&1)
echo "$output" | grep -q "function l \$square" && pass "QBE IL contains function" || fail "QBE IL missing function"

# Test 8: Phase 1 regression check
echo "Test 8: Phase 1 regression check (reader tests)"
output=$($NPLC0 --print-ast stage0/tests/test_special.npl 2>&1)
expected='((define x 5) (if (> x 3) "yes" "no") (lambda (a b) (+ a b)) (let ((x 1) (y 2)) (+ x y)) (begin (display "hello") (display "world")) (set! x 10))'
[ "$output" = "$expected" ] && pass "Phase 1 reader regression" || fail "Phase 1 regression"

# Test 9: Phase 0 smoke test
echo "Test 9: Phase 0 smoke test"
ninja -C "$BUILD_DIR" smoke >/dev/null
output=$("$BUILD_DIR/smoke/hello")
[ "$output" = "hello world" ] && pass "Phase 0 smoke test" || fail "Phase 0 smoke test failed"

# Cleanup
rm -f /tmp/npl_test_*.npl /tmp/npl_test_* /tmp/npl_test_*.npl

echo ""
echo "=== All Phase 2 tests passed ==="

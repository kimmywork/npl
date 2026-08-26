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
[ "$result" -eq 228 ] && pass "42*42=1764, exit code 228 (1764%256)" || fail "expected 228, got $result"

# Test 2: Multiple arithmetic operations
echo "Test 2: Multiple arithmetic operations"
cat > /tmp/npl_test_multi.npl << 'EOF'
(define (add a b) (+ a b))
(define (sub a b) (- a b))
(define (mul a b) (* a b))
(define (main) (add (mul 3 4) (sub 10 5)))
EOF
$NPLC0 -o /tmp/npl_test_multi /tmp/npl_test_multi.npl 2>&1
result=$(/tmp/npl_test_multi; echo $?)
[ "$result" -eq 17 ] && pass "3*4 + (10-5) = 17" || fail "expected 17, got $result"

# Test 3: If expression (true branch)
echo "Test 3: If expression (true branch)"
cat > /tmp/npl_test_if_true.npl << 'EOF'
(define (abs x)
  (if (< x 0) (- 0 x) x))
(define (main) (abs -5))
EOF
$NPLC0 -o /tmp/npl_test_if_true /tmp/npl_test_if_true.npl 2>&1
result=$(/tmp/npl_test_if_true; echo $?)
[ "$result" -eq 5 ] && pass "abs(-5) = 5" || fail "expected 5, got $result"

# Test 4: If expression (false branch)
echo "Test 4: If expression (false branch)"
cat > /tmp/npl_test_if_false.npl << 'EOF'
(define (abs x)
  (if (< x 0) (- 0 x) x))
(define (main) (abs 5))
EOF
$NPLC0 -o /tmp/npl_test_if_false /tmp/npl_test_if_false.npl 2>&1
result=$(/tmp/npl_test_if_false; echo $?)
[ "$result" -eq 5 ] && pass "abs(5) = 5" || fail "expected 5, got $result"

# Test 5: Factorial (recursive)
echo "Test 5: Factorial (recursive)"
cat > /tmp/npl_test_fact.npl << 'EOF'
(define (fact n)
  (if (<= n 1) 1 (* n (fact (- n 1)))))
(define (main) (fact 5))
EOF
$NPLC0 -o /tmp/npl_test_fact /tmp/npl_test_fact.npl 2>&1
result=$(/tmp/npl_test_fact; echo $?)
[ "$result" -eq 120 ] && pass "5! = 120" || fail "expected 120, got $result"

# Test 6: Fibonacci (recursive)
echo "Test 6: Fibonacci (recursive)"
cat > /tmp/npl_test_fib.npl << 'EOF'
(define (fib n)
  (if (< n 2) n (+ (fib (- n 1)) (fib (- n 2)))))
(define (main) (fib 10))
EOF
$NPLC0 -o /tmp/npl_test_fib /tmp/npl_test_fib.npl 2>&1
result=$(/tmp/npl_test_fib; echo $?)
[ "$result" -eq 55 ] && pass "fib(10) = 55" || fail "expected 55, got $result"

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
output=$(./build/smoke/hello)
[ "$output" = "hello world" ] && pass "Phase 0 smoke test" || fail "Phase 0 smoke test failed"

# Cleanup
rm -f /tmp/npl_test_*.npl /tmp/npl_test_* /tmp/npl_test_*.npl

echo ""
echo "=== All Phase 2 tests passed ==="

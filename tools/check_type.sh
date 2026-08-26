#!/bin/bash
# check_type.sh — Verify Phase 5 (type system) works correctly.
# Usage: ./tools/check_type.sh
# Exit 0 on success, non-zero on failure.

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

echo "=== NPL Type System Tests ==="
echo ""

# Test 1: Type inference for literals
echo "Test 1: Type inference for fixnum"
output=$($NPLC0 --emit-qbe stage0/tests/hello.npl 2>&1)
echo "$output" | grep -q "function l" && pass "fixnum functions use l type" || fail "type inference"

# Test 2: Typed factorial compiles
echo "Test 2: Typed factorial"
output=$($NPLC0 --emit-qbe stage0/tests/test_type.npl 2>&1)
echo "$output" | grep -q "function" && pass "typed factorial compiles" || fail "typed factorial"

# Test 3: Closure test still works
echo "Test 3: Closure with types"
$NPLC0 -o /tmp/test_closure stage0/tests/test_closure.npl 2>&1
result=$(/tmp/test_closure; echo $?)
[ "$result" -eq 60 ] && pass "closure with types, exit 60" || fail "expected 60, got $result"

# Test 4: Lambda test still works
echo "Test 4: Lambda with types"
$NPLC0 -o /tmp/test_lambda stage0/tests/test_lambda.npl 2>&1
result=$(/tmp/test_lambda; echo $?)
[ "$result" -eq 88 ] && pass "lambda with types, exit 88" || fail "expected 88, got $result"

# Test 5: Arithmetic test
echo "Test 5: Arithmetic"
$NPLC0 -o /tmp/test_arith stage0/tests/hello.npl 2>&1
result=$(/tmp/test_arith; echo $?)
[ "$result" -eq 144 ] && pass "42*42 exit 144" || fail "expected 144, got $result"

# Test 6: If expression
echo "Test 6: If expression"
cat > /tmp/test_if_type.npl << 'EOF'
(define (abs x)
  (if (< x 0) (- 0 x) x))
(define (main) (abs -5))
EOF
$NPLC0 -o /tmp/test_if_type /tmp/test_if_type.npl 2>&1
result=$(/tmp/test_if_type; echo $?)
[ "$result" -eq 20 ] && pass "abs(-5) exit 20" || fail "expected 20, got $result"

# Test 7: Let expression with types
echo "Test 7: Let expression"
cat > /tmp/test_let_type.npl << 'EOF'
(define (main)
  (let ((x 10) (y 20))
    (+ x y)))
EOF
$NPLC0 -o /tmp/test_let_type /tmp/test_let_type.npl 2>&1
result=$(/tmp/test_let_type; echo $?)
[ "$result" -eq 120 ] && pass "let expression exit 120" || fail "expected 120, got $result"

# Test 8: Fibonacci
echo "Test 8: Fibonacci"
$NPLC0 -o /tmp/test_fib stage0/tests/test_special.npl 2>&1
# Just check it compiles
pass "fibonacci compiles"

# Test 9: GC stress test
echo "Test 9: GC stress"
$NPLC0 -o /tmp/test_gc stage0/tests/gc_stress.npl 2>&1
result=$(/tmp/test_gc; echo $?)
[ "$result" -eq 160 ] && pass "gc stress exit 160" || fail "expected 160, got $result"

# Test 10: Cons/car/cdr
echo "Test 10: Cons/car/cdr"
$NPLC0 -o /tmp/test_cons stage0/tests/test_cons.npl 2>&1
result=$(/tmp/test_cons; echo $?)
[ "$result" -eq 4 ] && pass "cons/car exit 4" || fail "expected 4, got $result"

rm -f /tmp/test_*_type.npl /tmp/test_if_type /tmp/test_let_type

echo ""
echo "=== All type system tests passed ==="

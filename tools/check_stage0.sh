#!/bin/bash
# check_stage0.sh — Verify Phase 1 (stage0 reader) works correctly.
# Usage: ./tools/check_stage0.sh
# Exit 0 on success, non-zero on failure.

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_DIR/build"
NPLC0="$BUILD_DIR/stage0/nplc0"
TEST_DIR="$PROJECT_DIR/stage0/tests"

RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m' # No Color

pass() { echo -e "${GREEN}PASS${NC}: $1"; }
fail() { echo -e "${RED}FAIL${NC}: $1"; exit 1; }

echo "=== NPL Stage0 Reader Tests ==="
echo ""

# Build nplc0 if needed
if [ ! -x "$NPLC0" ]; then
    echo "Building nplc0..."
    ninja -C "$BUILD_DIR" nplc0
fi

# Test 1: Number
echo "Test 1: Number literal"
output=$($NPLC0 --print-ast "$TEST_DIR/test_number.npl")
[ "$output" = "(42)" ] && pass "number literal" || fail "expected '(42)', got '$output'"

# Test 2: Symbol
echo "Test 2: Symbol"
output=$($NPLC0 --print-ast "$TEST_DIR/test_symbol.npl")
[ "$output" = "(hello)" ] && pass "symbol" || fail "expected '(hello)', got '$output'"

# Test 3: Nested list
echo "Test 3: Nested list"
output=$($NPLC0 --print-ast "$TEST_DIR/test_nested.npl")
[ "$output" = "((+ 1 2))" ] && pass "nested list" || fail "expected '((+ 1 2))', got '$output'"

# Test 4: Special forms
echo "Test 4: Special forms"
output=$($NPLC0 --print-ast "$TEST_DIR/test_special.npl")
expected='((define x 5) (if (> x 3) "yes" "no") (lambda (a b) (+ a b)) (let ((x 1) (y 2)) (+ x y)) (begin (display "hello") (display "world")) (set! x 10))'
[ "$output" = "$expected" ] && pass "special forms" || fail "special forms mismatch"

# Test 5: Quote
echo "Test 5: Quote"
output=$($NPLC0 --print-ast "$TEST_DIR/test_quote.npl")
expected='((quote (1 2 3)) (quote hello))'
[ "$output" = "$expected" ] && pass "quote" || fail "expected '$expected', got '$output'"

# Test 6: String
echo "Test 6: String"
output=$($NPLC0 --print-ast "$TEST_DIR/test_string.npl")
# Use diff to compare multiline output (note: wrapped in list parens)
expected='("hello world" "line1
line2")'
echo "$expected" > /tmp/npl_expected.txt
echo "$output" > /tmp/npl_actual.txt
diff -q /tmp/npl_expected.txt /tmp/npl_actual.txt > /dev/null 2>&1 && pass "string" || fail "string mismatch"

# Test 7: Edge cases (comments, booleans, float, function define)
echo "Test 7: Edge cases"
cat > /tmp/npl_edge_test.npl << 'EOF'
; This is a comment
42
3.14
#t
#f
hello-world
'(1 2 3)
(define (square x) (* x x))
EOF
output=$($NPLC0 --print-ast /tmp/npl_edge_test.npl)
expected='(42 3.14 #t #f hello-world (quote (1 2 3)) (define (square x) (* x x)))'
[ "$output" = "$expected" ] && pass "edge cases" || fail "edge cases mismatch: $output"

# Test 8: Phase 0 smoke test still works
echo "Test 8: Phase 0 smoke test"
output=$(./build/smoke/hello)
[ "$output" = "hello world" ] && pass "Phase 0 smoke test" || fail "Phase 0 smoke test failed"

# Test 9: include, cond, and closure compile to an executable
echo "Test 9: Multi-file executable"
work=$(mktemp -d)
mkdir -p "$work/lib"
cat > "$work/lib/functions.npl" << 'EOF'
(define (make-adder x) (lambda (y) (+ x y)))
(define (choose x)
  (cond ((< x 0) 1)
        ((= x 0) 2)
        (else ((make-adder 40) x))))
EOF
cat > "$work/main.npl" << 'EOF'
(include "lib/functions.npl")
(define (main)
  (case 2
    ((1) 0)
    ((2 3) (choose 2))
    (else 9)))
EOF
"$NPLC0" -o "$work/program" "$work/main.npl"
set +e
"$work/program"
rc=$?
set -e
rm -rf "$work"
[ "$rc" -eq 168 ] && pass "include/cond/closure executable" \
                   || fail "multi-file executable returned $rc"

rm -f /tmp/npl_edge_test.npl

echo ""
echo "=== All tests passed ==="

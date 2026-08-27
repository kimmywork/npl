#!/bin/bash
# check_core.sh — Phase 8 GAP-fix tests (increment-1 core features).
# Usage: ./tools/check_core.sh
# Exit 0 on success, non-zero on failure.

set -u

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_DIR/build"
NPLC0="$BUILD_DIR/stage0/nplc0"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m'

pass() { echo -e "${GREEN}PASS${NC}: $1"; }
fail() { echo -e "${RED}FAIL${NC}: $1"; exit 1; }

echo "=== Core feature tests (cond / inner define / quote / globals) ==="

nplc0_ok() { $NPLC0 -o "$2" "$1" >/dev/null 2>"$WORK/cc.err" \
    || { echo "--- compiler stderr ---"; cat "$WORK/cc.err"; fail "compile $1"; }; }

run_expect_stdout() { # prog expected_file label
    "$1" > "$WORK/run.out" 2>/dev/null
    if diff -q "$WORK/run.out" "$2" >/dev/null; then
        pass "$3"
    else
        echo "--- expected ---"; cat "$2"
        echo "--- actual ---";   cat "$WORK/run.out"
        fail "$3"
    fi
}

# ---------- positive suite ----------
SRC="$PROJECT_DIR/stage0/tests/test_core.npl"
BIN="$WORK/core_bin"

nplc0_ok "$SRC" "$BIN"

cat > "$WORK/expected.txt" << 'EOF'
#t
0
1
111
333
16
side 144
32
#t
#f
#t
#t
#t
#f
#t
#f
(1 2 3)
tail-sym
(nested (a b) 9)
100
alpha
zero-true
nil-true
EOF

run_expect_stdout "$BIN" "$WORK/expected.txt" "core feature suite (stdout contract)"

"$BIN" >/dev/null 2>&1
rc=$?
# Project convention (Phase 2+): main returns the raw tagged NplVal;
# the process exit code is its low byte. 42 -> 42<<2 = 168.
[ "$rc" -eq 168 ] && pass "main returns tagged 42 (168) as exit code" || fail "exit code 168 expected, got $rc"

# ---------- contract tests: misuse must fail loudly, never silently ----------

# set! unsupported -> compile must fail
cat > "$WORK/t_set.npl" << 'EOF'
(define (f) (set! x 5))
(define (main) 1)
EOF
$NPLC0 -o "$WORK/setbin" "$WORK/t_set.npl" >/dev/null 2>&1 \
    && fail "contract: set! must be a compile error" \
    || pass "contract: set! rejected at compile time"

# unbound symbol -> compile must fail
cat > "$WORK/t_unbound.npl" << 'EOF'
(define (main) (+ nope 1))
EOF
$NPLC0 -o "$WORK/unb_bin" "$WORK/t_unbound.npl" >/dev/null 2>&1 \
    && fail "contract: unbound symbol must be a compile error" \
    || pass "contract: unbound symbol rejected at compile time"

# computed global initializer -> legal since increment-4 (runtime inits);
# contract now checks the VALUE at runtime instead of rejection
cat > "$WORK/t_gvar.npl" << 'EOF'
(define bad (+ 1 2))
(define (main) (display bad))
EOF
nplc0_ok "$WORK/t_gvar.npl" "$WORK/gv_bin"
out=$("$WORK/gv_bin" | tail -1)
[ "$out" = "3" ] && pass "contract: computed global evaluates via init fn" \
    || fail "contract: computed global expected 3, got '$out'"

# misplaced else in cond -> compile must fail
cat > "$WORK/t_else.npl" << 'EOF'
(define (f x) (cond (else 1) ((> x 0) 2)))
(define (main) 1)
EOF
$NPLC0 -o "$WORK/el_bin" "$WORK/t_else.npl" >/dev/null 2>&1 \
    && fail "contract: misplaced else in cond must be an error" \
    || pass "contract: misplaced else rejected"

echo ""
echo "=== All core feature tests passed ==="

echo ""
echo "=== String / symbol builtin tests ==="

SRC2="$PROJECT_DIR/stage0/tests/test_strings.npl"
BIN2="$WORK/str_bin"
nplc0_ok "$SRC2" "$BIN2"

cat > "$WORK/expected_str.txt" << 'EOF'
foobar
6
1
65
12345
1
hello
#t
%42
EOF

run_expect_stdout "$BIN2" "$WORK/expected_str.txt" "string/symbol suite (stdout contract)"

"$BIN2" >/dev/null 2>&1
rc=$?
[ "$rc" -eq 44 ] && pass "strings main returns tagged 11 (44)" || fail "exit 44 expected, got $rc"

# contract: string-ref out of range must fail loudly at runtime
cat > "$WORK/t_oob.npl" << 'EOF'
(define (main) (string-ref "abc" 99))
EOF
nplc0_ok "$WORK/t_oob.npl" "$WORK/oob_bin"
"$WORK/oob_bin" >/dev/null 2>&1
rc=$?
[ "$rc" -ne 0 ] && pass "contract: out-of-range string-ref aborts" || fail "contract: OOB string-ref must exit non-zero"

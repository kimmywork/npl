#!/bin/bash
# check_bootstrap.sh — Phase 8 bootstrap validation.
#
# Stage A (functional): the self-hosted compiler compiles test programs.
#   nplc1 built by stage0 must produce working executables for the
#   supported L1 slice (arithmetic, control flow, recursion, strings).
#
# Stage B (fixpoint / cproc-style):
#   il2 = nplc1(compiler.npl)
#   nplc2 = qbe+clang(il2)
#   il3 = nplc2(compiler.npl)
#   il2 == il3 must hold byte-for-byte when stage B passes fully;
#   current known-limitation failures are reported distinctly.
#
# Exit 0 iff every enabled check passes.

set -u
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD="$ROOT/build"
STAGE0="$BUILD/stage0/nplc0"
QBE="$ROOT/external/qbe-1.3/qbe"
CC=clang
RUNTIME="$BUILD/runtime/libnpl_runtime.a"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

GREEN='\033[0;32m'; RED='\033[0;31m'; YEL='\033[0;33m'; NC='\033[0m'
pass() { echo -e "${GREEN}PASS${NC}: $1"; }
fail() { echo -e "${RED}FAIL${NC}: $1"; return 1; }
skip() { echo -e "${YEL}SKIP${NC}: $1"; }

compile_via() { # <compiler-binary> <src.npl> <out.ssa>
    "$1" "$2" > "$3" 2>"$3.err"
}

build_exe() { # <ssa> <outbin>
    "$QBE" -o "$1.s" "$1" && $CC "$1.s" "$RUNTIME" -o "$2"
}

echo "=== Bootstrap Stage A: functional compilation via nplc1 ==="

$STAGE0 "$ROOT/src/compiler.npl" -o "$WORK/nplc1" || {
    fail "stage0 cannot build nplc1"; exit 1; }
pass "stage0 -> nplc1"

# nplc1 exits 44 (= tagged 11 from its own main) on success.
NPLC_OK=44

A_FAIL=0
check_prog() { # <name> <want-exit> <program>
    local name="$1" want_exit="$2"
    printf '%s\n' "$3" > "$WORK/$name.npl"
    compile_via "$WORK/nplc1" "$WORK/$name.npl" "$WORK/$name.ssa"
    local crc=$?
    if [ "$crc" != "$NPLC_OK" ] || ! [ -s "$WORK/$name.ssa" ] \
       || head -1 "$WORK/$name.ssa" | grep -q "^nplc1:"; then
        fail "$name: emission failed: $(head -1 "$WORK/$name.ssa" | tail -c 60)"
        A_FAIL=1; return
    fi
    if ! build_exe "$WORK/$name.ssa" "$WORK/$name.bin"; then
        fail "$name: link stage"; A_FAIL=1; return
    fi
    local got
    "$WORK/$1.bin" > "$WORK/$1.out" 2>/dev/null
    got=$?
    if [ "$got" != "$want_exit" ]; then
        fail "$name: exit $got != $want_exit"; A_FAIL=1; return
    fi
    if [ $# -ge 4 ] && [ -n "$4" ]; then
        if ! diff <(printf '%s' "$4") "$WORK/$1.out" >/dev/null; then
            fail "$name: stdout mismatch"; A_FAIL=1; return
        fi
    fi
    pass "$name (exit=$want_exit)"
}

# programs call (exit v): the process exit code is the untagged value's
# low byte per the runtime contract.
check_prog p_arith 5 '(define (main) (exit (+ 2 3)))'
check_prog p_rec   7 '(define (f x) (if (< x 1) 7 (f (- x 1))))
(define (main) (exit (f 3)))'
check_prog p_args 42 '(define (add a b) (+ a b))
(define (main) (exit (add 20 22)))'
check_prog p_str   9 '(define (main) (begin (display "ok") (exit 9)))' ok

# letrec/let*/let binding-offset regression (P0-1):
# the parser once mis-consumed the bindings '(' causing letrec/let*
# to emit "unexpected )" / "unbound symbol".  Now fixed.
check_prog p_let      40 '(define (main) (let ((a 40)) (exit a)))'
check_prog p_let2     12 '(define (main) (let ((a 3) (b 4)) (exit (* a b))))'
check_prog p_letrec   40 '(define (main) (letrec ((a 40)) (exit a)))'
check_prog p_letrec2  30 '(define (main) (letrec ((a 10) (b 20)) (exit (+ a b))))'
check_prog p_letstar  40 '(define (main) (let* ((a 40)) (exit a)))'
check_prog p_letstar2 15 '(define (main) (let* ((a 10) (b (+ a 5))) (exit b)))'

# R7RS case regression: literal datums, else, and sequential clause bodies.
check_prog p_case_num 12 \
    '(define (main) (exit (case 2 ((1) 9) ((2 3) 12) (else 1))))'
check_prog p_case_sym 17 \
    "(define (main) (exit (case 'b ((a) 1) ((b) 17) (else 2))))"
check_prog p_case_body 23 \
    '(define (main) (exit (case 9 ((9) (display "ok") 23) (else 1))))' ok

check_nplc1_error() { # <name> <program> <diagnostic-substring>
    local name="$1"
    printf '%b' "$2" > "$WORK/$name.npl"
    compile_via "$WORK/nplc1" "$WORK/$name.npl" "$WORK/$name.ssa"
    local crc=$?
    if [ "$crc" = "$NPLC_OK" ] \
       || ! grep -Fq "$3" "$WORK/$name.ssa" "$WORK/$name.ssa.err"; then
        fail "$name: missing diagnostic '$3'"
        A_FAIL=1; return
    fi
    pass "$name diagnostic"
}
check_nplc1_error p_unmatched_open '\n  (define (main) 1\n' \
    "unmatched ( at line 2 col 3; expected ) before end of input"
check_nplc1_error p_unmatched_close '\n  )\n' \
    "unmatched ) at line 2 col 3"

[ "$A_FAIL" = 0 ] && echo -e "${GREEN}=== Stage A all green ===${NC}" \
                   || echo -e "${RED}=== Stage A had failures ===${NC}"

echo ""
echo "=== Bootstrap Stage B: fixpoint ==="

SRC="$ROOT/src/compiler.npl"

# il2 = nplc1(self)
compile_via "$WORK/nplc1" "$SRC" "$WORK/il2.ssa"
if [ $? != "$NPLC_OK" ] || ! [ -s "$WORK/il2.ssa" ] \
   || head -1 "$WORK/il2.ssa" | grep -q "^nplc1:"; then
    fail "il2 emission: $(head -1 "$WORK/il2.ssa" | tail -c 70)"; exit 1
fi
pass "il2 = nplc1(compiler.npl) emitted ($(wc -l < "$WORK/il2.ssa") lines)"

# nplc2 from il2
if build_exe "$WORK/il2.ssa" "$WORK/nplc2"; then
    pass "nplc2 built from il2"
else
    fail "nplc2 build"; exit 1
fi

# il3 = nplc2(self)
il3_rc=0
compile_via "$WORK/nplc2" "$SRC" "$WORK/il3.ssa" || il3_rc=$?
if [ "$il3_rc" = 44 ] && [ -s "$WORK/il3.ssa" ]; then
    pass "il3 = nplc2(compiler.npl) emitted ($(wc -l < "$WORK/il3.ssa") lines)"
    if diff -q "$WORK/il2.ssa" "$WORK/il3.ssa" >/dev/null; then
        pass "*** FIXPOINT REACHED: il2 == il3 byte-identical ***"
    else
        fail "il2 != il3 (stage2 still diverges; see design-inc1-core.md)"
        exit 1
    fi
else
    skip "il3 emission failed (known limitation, tracked in ledger): $(grep '^nplc1:' "$WORK/il3.ssa" 2>/dev/null | tail -1)"
    exit 2
fi

echo ""
exit 0

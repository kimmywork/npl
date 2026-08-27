#!/bin/bash
# check_gc.sh — Phase 3 mmap allocator and mark-sweep acceptance.

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD="$ROOT/build"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

ninja -C "$BUILD" gc_stress nplc0 >/dev/null
output=$("$BUILD/runtime/gc_stress")
printf '%s\n' "$output"
printf '%s\n' "$output" | grep -Fq 'PASS: 1000000 allocations'

cat > "$WORK/churn.npl" << 'EOF'
(define (churn n)
  (if (= n 0)
      7
      (begin
        (cons n ())
        (churn (- n 1)))))
(define (main) (churn 1000000))
EOF
"$BUILD/stage0/nplc0" -o "$WORK/churn" "$WORK/churn.npl" >/dev/null
rc=0
"$WORK/churn" >/dev/null 2>&1 || rc=$?
[ "$rc" -eq 28 ]
echo "PASS: generated NPL program survives 1000000 collected cons cells"

cat > "$WORK/live-list.npl" << 'EOF'
(define (build n acc)
  (if (= n 0) acc (build (- n 1) (cons n acc))))
(define (list-length xs n)
  (if (null? xs) n (list-length (cdr xs) (+ n 1))))
(define (main)
  (if (= (list-length (build 100000 ()) 0) 100000) 7 9))
EOF
"$BUILD/stage0/nplc0" -o "$WORK/live-list" "$WORK/live-list.npl" >/dev/null
rc=0
"$WORK/live-list" >/dev/null 2>&1 || rc=$?
[ "$rc" -eq 28 ]
echo "PASS: conservative roots preserve a 100000-cell live list"

echo "=== GC acceptance passed ==="

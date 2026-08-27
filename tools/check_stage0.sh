#!/bin/bash
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUILD="$ROOT/build"
NPLC0="$BUILD/stage0/nplc0"
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

cmake -S "$ROOT" -B "$BUILD" -G Ninja >/dev/null
ninja -C "$BUILD" nplc0 >/dev/null

mkdir -p "$WORK/lib" "$WORK/cycle"
cat > "$WORK/main.npl" <<'EOF'
(include "lib/forms.npl" "tail.npl")
(define main-value (choose 1))
EOF
cat > "$WORK/lib/forms.npl" <<'EOF'
(include "values.npl")
(define (choose x)
  (cond ((= x 0) zero)
        (else one)))
EOF
cat > "$WORK/lib/values.npl" <<'EOF'
(define zero 'zero)
EOF
cat > "$WORK/tail.npl" <<'EOF'
(define one "one")
EOF

actual=$($NPLC0 --print-ast "$WORK/main.npl")
expected='((define zero (quote zero)) (define (choose x) (cond ((= x 0) zero) (else one))) (define one "one") (define main-value (choose 1)))'
[ "$actual" = "$expected" ] || {
    printf 'expected: %s\nactual:   %s\n' "$expected" "$actual" >&2
    exit 1
}

expect_error() {
    file=$1
    message=$2
    if "$NPLC0" --print-ast "$file" >"$WORK/out" 2>"$WORK/err"; then
        echo "expected failure: $file" >&2
        exit 1
    fi
    grep -Fq "$message" "$WORK/err" || {
        cat "$WORK/err" >&2
        exit 1
    }
}

printf '(define x 1\n' > "$WORK/malformed.npl"
expect_error "$WORK/malformed.npl" "unmatched '('"

printf '(include 42)\n' > "$WORK/invalid.npl"
expect_error "$WORK/invalid.npl" "include path"

printf '(include "absent.npl")\n' > "$WORK/missing.npl"
expect_error "$WORK/missing.npl" "cannot read"

printf '(include "b.npl")\n' > "$WORK/cycle/a.npl"
printf '(include "a.npl")\n' > "$WORK/cycle/b.npl"
expect_error "$WORK/cycle/a.npl" "include cycle"

echo "stage0 frontend: PASS"

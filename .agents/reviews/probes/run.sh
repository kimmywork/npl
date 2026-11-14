#!/bin/bash
# Run review probes on npli, nplc, and Guile (or Racket for .rkt files) and
# print one line per engine.
#
#   .agents/reviews/probes/run.sh FILE...        run the given probes
#   .agents/reviews/probes/run.sh --scale [N...] time M8 macro vs plain lists
#
# A .scm probe without an (import ...) line gets
# "(import (scheme base) (scheme write))" prepended for npli and nplc, unless
# it contains "; probe: no-import". Guile always gets the file without its
# import lines. nplc is built from bootstrap/current.ssa into $PROBE_WORK
# (default: a per-user temp directory) and rebuilt when the seed changes.
set -u

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
WORK=${PROBE_WORK:-${TMPDIR:-/tmp}/npl-review-probes}
QBE="$ROOT/build/qbe/qbe"
RUNTIME="$ROOT/build/runtime/libnpl_runtime.a"
NPLI="$ROOT/build/bin/npli"
NPLC="$WORK/nplc"
mkdir -p "$WORK"

if [ ! -x "$NPLC" ] || [ "$ROOT/bootstrap/current.ssa" -nt "$NPLC" ]; then
    "$QBE" -o "$WORK/nplc.s" "$ROOT/bootstrap/current.ssa" &&
        cc -o "$NPLC" "$WORK/nplc.s" "$RUNTIME" -lm || exit 1
fi

# Hide machine-specific paths (Guile and Racket print absolute paths).
one_line() {
    tr '\n' ' ' | sed -e "s|$(cd "$WORK" && pwd -P)|<work>|g" -e "s|$WORK|<work>|g" \
        -e "s|$ROOT/||g" | cut -c1-200
}

run_nplc() {
    src=$1; out=$WORK/$2
    if timeout 300 "$NPLC" --emit-qbe --stdlib-root "$ROOT/lib" "$src" \
           > "$out.ssa" 2> "$out.err"; then
        "$QBE" -o "$out.s" "$out.ssa" && cc -o "$out.bin" "$out.s" "$RUNTIME" -lm &&
            timeout 60 "$out.bin" 2>&1 | one_line
    else
        printf 'COMPILE: '; one_line < "$out.err"
    fi
}

run_scm() {
    f=$1; name=$(echo "$f" | tr '/' '_')
    src=$WORK/$name.npl.scm; guile_src=$WORK/$name.guile.scm
    if grep -q '^(import' "$f" || grep -q '; probe: no-import' "$f"; then
        cp "$f" "$src"
    else
        { echo '(import (scheme base) (scheme write))'; cat "$f"; } > "$src"
    fi
    grep -v '^(import' "$f" > "$guile_src"
    printf '%s\n' "$f"
    printf '  npli : %s\n' "$(timeout 60 "$NPLI" "$src" 2>&1 | one_line)"
    printf '  nplc : %s\n' "$(run_nplc "$src" "$name")"
    if command -v guile >/dev/null; then
        # Run from WORK with a short name so error locations stay readable.
        cp "$guile_src" "$WORK/g.scm"
        printf '  guile: %s\n' "$(cd "$WORK" && timeout 60 guile --no-auto-compile g.scm 2>&1 |
            grep -v -e '^;;;' -e '^Syntax error:$' | tail -2 | one_line)"
    fi
}

run_rkt() {
    printf '%s\n' "$1"
    if command -v racket >/dev/null; then
        printf '  racket: %s\n' "$(timeout 120 racket "$1" 2>&1 | head -3 | one_line)"
    else
        printf '  racket: (not installed)\n'
    fi
}

seconds() {
    start=$(date +%s.%N); "$@" > /dev/null 2>&1; end=$(date +%s.%N)
    python3 -c "print('%.2f' % ($end - $start))"
}

if [ "${1:-}" = "--scale" ]; then
    shift
    python3 "$ROOT/.agents/reviews/probes/2026-11-14-macro-system/scale/gen.py" \
        "$WORK/scale" "$@"
    for f in "$WORK"/scale/macro-*.scm; do
        n=${f##*/macro-}; n=${n%.scm}
        m=$(seconds "$NPLC" --emit-qbe --stdlib-root "$ROOT/lib" "$f")
        p=$(seconds "$NPLC" --emit-qbe --stdlib-root "$ROOT/lib" "$WORK/scale/plain-$n.scm")
        printf 'n=%-6s nplc macro %ss  plain %ss\n' "$n" "$m" "$p"
    done
    exit 0
fi

for f in "$@"; do
    case "$f" in
        *.rkt) run_rkt "$f" ;;
        *.scm) run_scm "$f" ;;
    esac
done

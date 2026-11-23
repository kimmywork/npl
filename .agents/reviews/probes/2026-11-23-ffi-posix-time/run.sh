#!/bin/bash
# Probes for the 2026-11-23 FFI / POSIX / time review (R1-R10).
#
#   run.sh               run every NN-*.scm probe on npli and nplc (and Guile
#                        when NN-*.guile.scm exists); print one line per engine
#   run.sh FILE...       run the given probes
#   run.sh --leak [N]    R5: peak RSS of 06-struct-view-leak.scm at 0 and N
#   run.sh --cc          R6: import time, and import with NPL_CC=/usr/bin/false
#   run.sh --stubs       R7: feature-macro position in nplc --emit=c-stubs
#
# Uses build/bin/npli and build/bin/nplc (the driver links the core native
# packages).  Scratch files go to $PROBE_WORK (default: a temp directory).
set -u

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../../.." && pwd)
NPLI="$ROOT/build/bin/npli"
NPLC="$ROOT/build/bin/nplc"
WORK=${PROBE_WORK:-${TMPDIR:-/tmp}/npl-review-probes-2026-11-23}
mkdir -p "$WORK"

one_line() {
    tr '\n' ' ' | sed -e "s|$WORK|<work>|g" -e "s|$ROOT/||g" | cut -c1-200
}

status_suffix() {
    [ "$1" -eq 0 ] || printf ' [exit %s]' "$1"
}

run_npli() {
    out=$(cd "$WORK" && timeout 60 "$NPLI" "$1" 2>&1); code=$?
    printf '%s%s' "$(printf '%s' "$out" | one_line)" "$(status_suffix $code)"
}

run_nplc() {
    name=$(basename "$1" .scm)
    if ! (cd "$WORK" && timeout 300 "$NPLC" -o "$WORK/$name.bin" "$1" \
              > "$WORK/$name.err" 2>&1); then
        printf 'COMPILE: '; one_line < "$WORK/$name.err"; return
    fi
    out=$(cd "$WORK" && timeout 60 "$WORK/$name.bin" 2>&1); code=$?
    printf '%s%s' "$(printf '%s' "$out" | one_line)" "$(status_suffix $code)"
}

run_probe() {
    f=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
    printf '%s\n' "${f#$HERE/}"
    printf '  npli : %s\n' "$(run_npli "$f")"
    printf '  nplc : %s\n' "$(run_nplc "$f")"
    guile_file=${f%.scm}.guile.scm
    if [ -f "$guile_file" ] && command -v guile > /dev/null; then
        printf '  guile: %s\n' "$(timeout 60 guile --no-auto-compile \
            "$guile_file" 2>&1 | grep -v '^;;;' | one_line)"
    fi
}

peak_rss() {
    # Peak resident set size in bytes (macOS and GNU time formats).
    if /usr/bin/time -l true > /dev/null 2>&1; then
        /usr/bin/time -l "$@" 2>&1 > /dev/null |
            awk '/maximum resident set size/ { print $1 }'
    else
        /usr/bin/time -f '%M' "$@" 2>&1 > /dev/null | tail -1 |
            awk '{ print $1 * 1024 }'
    fi
}

seconds() {
    python3 - "$@" <<'PY'
import subprocess, sys, time
start = time.perf_counter()
subprocess.run(sys.argv[1:], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
print('%.2f' % (time.perf_counter() - start))
PY
}

case "${1:-}" in
    --leak)
        n=${2:-2000000}
        for mode in time leak free; do
            for count in 0 "$n"; do
                printf 'npli %-4s x%-9s peak RSS %s bytes\n' "$mode" "$count" \
                    "$(peak_rss "$NPLI" "$HERE/06-struct-view-leak.scm" \
                                "$count" "$mode")"
            done
        done
        exit 0 ;;
    --cc)
        for f in 07-import-base 07-import-time 07-import-unix; do
            printf '%-15s npli %ss   NPL_CC=false: %s\n' "$f" \
                "$(seconds "$NPLI" "$HERE/$f.scm")" \
                "$(NPL_CC=/usr/bin/false "$NPLI" "$HERE/$f.scm" 2>&1 | one_line)"
        done
        exit 0 ;;
    --stubs)
        "$NPLC" --emit=c-stubs -o "$WORK/stubs.c" \
            "$HERE/09-undefined-time-helpers.scm" || exit 1
        grep -n -m1 '#include' "$WORK/stubs.c"
        grep -n -e '_GNU_SOURCE' -e '_DARWIN_C_SOURCE' "$WORK/stubs.c"
        exit 0 ;;
esac

if [ $# -eq 0 ]; then
    set -- $(ls "$HERE"/[0-9][0-9]-*.scm | grep -v '\.guile\.scm$' |
             grep -v -e '06-struct-view-leak' -e '07-import-')
fi
for f in "$@"; do run_probe "$f"; done

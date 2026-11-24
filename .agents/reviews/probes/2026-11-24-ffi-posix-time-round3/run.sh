#!/bin/sh
# Probes for the 2026-11-24 third-round FFI / POSIX / time review (R23-R30).
#
#   run.sh            run every probe in this directory on npli and nplc
#   run.sh FILE...    run the given probes
#
# Uses build/bin/npli and build/bin/nplc.  Scratch files go to $PROBE_WORK.
# 24b truncates a scratch file to a garbage length (about 1 GiB, sparse).
set -u

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../../.." && pwd)
NPLI="$ROOT/build/bin/npli"
NPLC="$ROOT/build/bin/nplc"
WORK=${PROBE_WORK:-${TMPDIR:-/tmp}/npl-review-probes-2026-11-24-r3}
export NPL_NATIVE_PATH="$ROOT/build/lib/npl/native"
mkdir -p "$WORK"

one_line() {
    tr '\n' ' ' | sed -e "s|$WORK|<work>|g" -e "s|$ROOT/||g" | cut -c1-200
}

run_probe() {
    f=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
    name=$(basename "$f" .scm)
    printf '%s\n' "${f#$HERE/}"
    out=$(cd "$WORK" && timeout 120 "$NPLI" "$f" 2>&1); code=$?
    printf '  npli : %s [exit %s]\n' "$(printf '%s' "$out" | one_line)" "$code"
    if (cd "$WORK" && timeout 300 "$NPLC" -o "$WORK/$name.bin" "$f" \
            > "$WORK/$name.err" 2>&1); then
        out=$(cd "$WORK" && timeout 120 "$WORK/$name.bin" 2>&1); code=$?
        printf '  nplc : %s [exit %s]\n' "$(printf '%s' "$out" | one_line)" "$code"
    else
        printf '  nplc : COMPILE: %s\n' "$(grep -v '^ ' "$WORK/$name.err" | one_line)"
    fi
}

if [ $# -eq 0 ]; then set -- "$HERE"/*.scm; fi
for f in "$@"; do run_probe "$f"; done

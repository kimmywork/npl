#!/bin/bash
# Probes for the 2026-11-24 second-round FFI / POSIX / time review (R13-R22).
#
#   run.sh               run every NN-*.scm probe on npli and nplc
#   run.sh FILE...       run the given probes
#   run.sh --gc          R13: nplc build of 13 under small nurseries, with and
#                        without NPL_GC_VERIFY=1
#   run.sh --link        R16: artifact form for NPL_NATIVE_LINK vs --native
#   run.sh --cache       R19: import (npl time) with NPL_CC=/usr/bin/false, without and
#                        with a package NPL_C_STRUCT_LAYOUTS
#
# Uses build/bin/npli and build/bin/nplc.  Scratch files go to $PROBE_WORK
# (default: a temp directory).
set -u

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../../.." && pwd)
NPLI="$ROOT/build/bin/npli"
NPLC="$ROOT/build/bin/nplc"
WORK=${PROBE_WORK:-${TMPDIR:-/tmp}/npl-review-probes-2026-11-24}
mkdir -p "$WORK"

one_line() {
    tr '\n' ' ' | sed -e "s|$WORK|<work>|g" -e "s|$ROOT/||g" \
                      -e 's|/var/folders/[^ ]*/npl-driver\.[A-Za-z0-9]*|<tmp>|g' |
        cut -c1-200
}

status_suffix() {
    [ "$1" -eq 0 ] || printf ' [exit %s]' "$1"
}

compile() {
    # compile SOURCE OUTPUT [nplc options...]; prints the diagnostic on failure
    src=$1; out=$2; shift 2
    if ! (cd "$WORK" && timeout 300 "$NPLC" "$@" -o "$out" "$src" \
              > "$out.err" 2>&1); then
        printf 'COMPILE: '; grep -v '^ ' "$out.err" | one_line; return 1
    fi
}

run_npli() {
    out=$(cd "$WORK" && timeout 120 "$NPLI" "$1" 2>&1); code=$?
    printf '%s%s' "$(printf '%s' "$out" | one_line)" "$(status_suffix $code)"
}

run_nplc() {
    name=$(basename "$1" .scm)
    compile "$1" "$WORK/$name.bin" || return
    out=$(cd "$WORK" && timeout 120 "$WORK/$name.bin" 2>&1); code=$?
    printf '%s%s' "$(printf '%s' "$out" | one_line)" "$(status_suffix $code)"
}

run_probe() {
    f=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
    printf '%s\n' "${f#$HERE/}"
    printf '  npli : %s\n' "$(run_npli "$f")"
    printf '  nplc : %s\n' "$(run_nplc "$f")"
}

link_form() {
    # "shared" when the executable depends on the shared runtime
    if command -v otool > /dev/null; then deps=$(otool -L "$1")
    else deps=$(ldd "$1" 2>&1); fi
    case "$deps" in *npl_runtime*) printf 'shared' ;; *) printf 'static' ;; esac
}

case "${1:-}" in
    --gc)
        bin="$WORK/13-gc.bin"
        compile "$HERE/13-alloc-write-barrier.scm" "$bin" || { echo; exit 1; }
        for nursery in 2048 4096 8192; do
            printf 'nursery %-5s          : %s\n' "$nursery" \
                "$(NPL_GC_NURSERY_BYTES=$nursery "$bin" 20000 1 2>&1 | one_line)"
            printf 'nursery %-5s verify=1 : %s\n' "$nursery" \
                "$(NPL_GC_NURSERY_BYTES=$nursery NPL_GC_VERIFY=1 \
                   "$bin" 20000 1 2>&1 | one_line)"
        done
        exit 0 ;;
    --link)
        src="$HERE/16-native-link-env.scm"
        for setting in "env NPL_NATIVE_LINK=shared" "flag --native=shared" \
                       "env NPL_NATIVE_LINK=dynamic"; do
            kind=${setting%% *}; value=${setting#* }
            bin="$WORK/16-$kind-${value##*=}.bin"
            rm -f "$bin"
            if [ "$kind" = env ]; then
                env "$value" "$NPLC" -o "$bin" "$src" > "$bin.err" 2>&1
            else
                "$NPLC" "$value" -o "$bin" "$src" > "$bin.err" 2>&1
            fi
            code=$?
            if [ -f "$bin" ]; then
                printf '%-28s: exit %s, %s, %s bytes\n' "$value" "$code" \
                    "$(link_form "$bin")" "$(wc -c < "$bin" | tr -d ' ')"
            else
                printf '%-28s: exit %s, %s\n' "$value" "$code" \
                    "$(one_line < "$bin.err")"
            fi
        done
        exit 0 ;;
    --cache)
        printf 'core cache only        : %s\n' \
            "$(NPL_CC=/usr/bin/false "$NPLI" "$HERE/19-import-time.scm" 2>&1 | one_line)"
        printf 'NPL_C_STRUCT_LAYOUTS set: %s\n' \
            "$(NPL_C_STRUCT_LAYOUTS="$HERE/19-package.meta" NPL_CC=/usr/bin/false \
               "$NPLI" "$HERE/19-import-time.scm" 2>&1 | one_line)"
        exit 0 ;;
esac

if [ $# -eq 0 ]; then
    set -- $(ls "$HERE"/[0-9][0-9]-*.scm |
             grep -v -e '16-native-link-env' -e '19-import-time')
fi
for f in "$@"; do run_probe "$f"; done

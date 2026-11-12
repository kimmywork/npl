#!/bin/bash
# Run one shared R7RS fixture through NPL native, persistent REPL, and Guile.
# Usage: tools/run_r7rs_fixture.sh tests/r7rs/fixtures/smoke

set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
. "$ROOT/tools/utf8_locale.sh"
FIXTURE=${1:?fixture directory required}
FIXTURE=$(cd "$FIXTURE" && pwd)
META="$FIXTURE/fixture.meta"
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

[ -f "$META" ]

get_meta() {
    key=$1
    value=$(awk -F= -v key="$key" '$1 == key { print substr($0, index($0, "=") + 1); exit }' "$META")
    [ -n "$value" ]
    printf '%s' "$value"
}

name=$(get_meta name)
native_source="$FIXTURE/$(get_meta native)"
repl_input="$FIXTURE/$(get_meta repl)"
# oracle=none: behavior Guile 3.0 lacks (e.g. datum labels in read). The
# expected output is then established from R7RS and cross-checked by hand
# (recorded in the fixture's behavior file); native and npli must match it.
oracle=$(get_meta oracle || true)
# guile-defect=VERSION ...: those Guile releases mishandle this fixture
# (reason in guile-defect-reason). Only there is the Guile surface skipped,
# as for oracle=none; any other release must still match.
guile_defect=$(get_meta guile-defect || true)
oracle_skip=""
if [ "$oracle" = none ]; then
    oracle_skip=oracle-unavailable
elif [ -n "$guile_defect" ]; then
    guile_version=$(guile -c '(display (version))' 2>/dev/null || true)
    for version in $guile_defect; do
        if [ "$version" = "$guile_version" ]; then
            oracle_skip=oracle-known-defect
            get_meta guile-defect-reason >/dev/null
        fi
    done
fi
if [ "$oracle" = none ]; then
    guile_source="$FIXTURE/$(get_meta native)"
else
    guile_source="$FIXTURE/$(get_meta guile)"
fi
expected_out="$FIXTURE/$(get_meta expected-out)"
expected_err="$FIXTURE/$(get_meta expected-err)"
expected_status=$(get_meta expected-status)

for path in "$native_source" "$repl_input" "$guile_source" \
            "$expected_out" "$expected_err"; do
    [ -f "$path" ]
done

# Optional common body keeps native, evaluator, and Guile inputs identical.
repl_body=$(get_meta repl-body || true)
if [ -n "$repl_body" ]; then
    [ -f "$FIXTURE/$repl_body" ]
    cat "$repl_input" "$FIXTURE/$repl_body" > "$WORK/repl.in"
    printf '\n(exit)\n' >> "$WORK/repl.in"
    repl_input="$WORK/repl.in"
fi

NPLC=${NPLC:-"$ROOT/build/bin/nplc"}
REPL=${NPLI:-"$ROOT/build/bin/npli"}

if [ ! -x "$NPLC" ] || [ ! -x "$REPL" ]; then
    "$ROOT/tools/build_toolchain.sh" >/dev/null
fi

run_capture() {
    output=$1
    error=$2
    status_file=$3
    shift 3
    set +e
    "$@" >"$output" 2>"$error"
    status=$?
    set -e
    printf '%s\n' "$status" >"$status_file"
}

# The native source is compiled and then run as the first surface.
run_capture "$WORK/native-compile.out" "$WORK/native-compile.err" \
    "$WORK/native-compile.status" \
    "$NPLC" "$native_source" -o "$WORK/native"
if [ "$(cat "$WORK/native-compile.status")" -ne 0 ]; then
    cp "$WORK/native-compile.out" "$WORK/native.out"
    cp "$WORK/native-compile.err" "$WORK/native.err"
    printf '%s\n' "fixture=$name surface=native phase=compile status=FAIL classification=unclassified" >&2
    exit 1
fi
run_capture "$WORK/native.out" "$WORK/native.err" "$WORK/native.status" "$WORK/native"

# Run the same behavior as a no-prompt persistent interaction transcript.
(
    cd "$WORK"
    run_capture "$WORK/repl.out" "$WORK/repl.err" "$WORK/repl.status" \
        "$REPL" --no-prompt < "$repl_input"
)

# Guile is used only in its R7RS language mode and with auto-compilation off,
# so compiler notices do not contaminate the compared stderr.
if [ -n "$oracle_skip" ]; then
    cp "$expected_out" "$WORK/guile.out"
    cp "$expected_err" "$WORK/guile.err"
    printf '%s\n' "$expected_status" > "$WORK/guile.status"
else
    run_capture "$WORK/guile.out" "$WORK/guile.err" "$WORK/guile.status" \
        guile --r7rs -q --no-auto-compile -s "$guile_source"
fi

compare_surface() {
    surface=$1
    output=$2
    error=$3
    status_file=$4
    status=$(cat "$status_file")
    if [ "$status" -ne "$expected_status" ]; then
        printf '%s\n' "fixture=$name surface=$surface field=status expected=$expected_status actual=$status classification=unclassified" >&2
        return 1
    fi
    if ! cmp -s "$expected_out" "$output"; then
        printf '%s\n' "fixture=$name surface=$surface field=stdout classification=unclassified" >&2
        return 1
    fi
    if ! cmp -s "$expected_err" "$error"; then
        printf '%s\n' "fixture=$name surface=$surface field=stderr classification=unclassified" >&2
        return 1
    fi
}

compare_surface native "$WORK/native.out" "$WORK/native.err" "$WORK/native.status"
compare_surface repl "$WORK/repl.out" "$WORK/repl.err" "$WORK/repl.status"
compare_surface guile "$WORK/guile.out" "$WORK/guile.err" "$WORK/guile.status"

if ! cmp -s "$WORK/native.out" "$WORK/repl.out" || \
   ! cmp -s "$WORK/native.out" "$WORK/guile.out"; then
    printf '%s\n' "fixture=$name surfaces=native,repl,guile field=observable-result classification=unclassified" >&2
    exit 1
fi

printf '%s\n' "fixture=$name surface=native status=PASS classification=matched-expected"
printf '%s\n' "fixture=$name surface=repl status=PASS classification=matched-expected"
if [ -n "$oracle_skip" ]; then
    printf '%s\n' "fixture=$name surface=guile status=SKIP classification=$oracle_skip"
else
    printf '%s\n' "fixture=$name surface=guile status=PASS classification=matched-expected"
fi
if [ -n "$oracle_skip" ]; then
    printf '%s\n' "fixture=$name surfaces=native,repl status=PASS classification=identical-observable-result"
else
    printf '%s\n' "fixture=$name surfaces=native,repl,guile status=PASS classification=identical-observable-result"
fi

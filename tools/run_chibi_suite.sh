#!/bin/bash
# Run chibi-scheme's tests/r7rs-tests.scm (an independent R7RS conformance
# suite) on npli and report pass/fail counts plus the top-level forms that
# raised outside any test.
#
# Usage: tools/run_chibi_suite.sh [r7rs-tests.scm]
#   Without an argument the suite is downloaded (pinned chibi-scheme commit).
#   (chibi test) is the minimal portable version in tests/chibi/.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
COMMIT=${CHIBI_COMMIT:-master}
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
if [ $# -ge 1 ]; then
    cp "$1" "$WORK/r7rs-tests.scm"
else
    curl -sSf -m 120 -o "$WORK/r7rs-tests.scm" \
        "https://raw.githubusercontent.com/ashinn/chibi-scheme/$COMMIT/tests/r7rs-tests.scm"
fi
mkdir -p "$WORK/chibi"
cp "$ROOT/tests/chibi/chibi-test.sld" "$WORK/chibi/test.sld"
{ cat "$WORK/r7rs-tests.scm"; printf '\n(test-exit)\n'; } > "$WORK/suite.scm"
# The REPL evaluates form by form, so one unsupported form does not stop the
# run; its error goes to stderr.
(cd "$WORK" && "$ROOT/build/bin/npli" --no-prompt < suite.scm \
    > "$WORK/out" 2> "$WORK/err") || true
grep '^FAIL' "$WORK/out" || true
echo "--- top-level errors (form aborted, its tests not counted):"
sort "$WORK/err" | uniq -c | sort -rn
tail -1 "$WORK/out"

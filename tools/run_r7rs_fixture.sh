#!/bin/bash
# Compatibility wrapper: the expect-fixture runner now lives in NPL.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
. "$ROOT/tools/utf8_locale.sh"
cd "$ROOT"
export NPL_NATIVE_PATH="$ROOT/build/lib/npl/native"
FIXTURE=${1:?fixture directory required}
if [ ! -x "$ROOT/build/bin/npli" ]; then
    "$ROOT/tools/build_toolchain.sh" >/dev/null
fi
exec "$ROOT/build/bin/npli" "$ROOT/tools/expect-runner.npl" "$FIXTURE"

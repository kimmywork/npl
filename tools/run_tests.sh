#!/bin/bash
# Compatibility wrapper: the suite orchestrator now lives in NPL.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
. "$ROOT/tools/utf8_locale.sh"
cd "$ROOT"
export NPL_NATIVE_PATH="$ROOT/build/lib/npl/native"
if [ "${NPL_NO_BUILD:-0}" != "1" ]; then
    "$ROOT/tools/build_toolchain.sh" >/dev/null
fi
exec "$ROOT/build/bin/npli" "$ROOT/tools/run-tests.npl" "$@"

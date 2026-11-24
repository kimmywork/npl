#!/bin/bash
# Compatibility wrapper: the suite orchestrator now lives in NPL.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
. "$ROOT/tools/utf8_locale.sh"
cd "$ROOT"
export NPL_NATIVE_PATH="$ROOT/build/lib/npl/native"
"$ROOT/tools/build_toolchain.sh" >/dev/null
exec "$ROOT/build/bin/npli" "$ROOT/tools/run-tests.npl" "$@"

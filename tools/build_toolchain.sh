#!/bin/bash
# Build nplc (from bootstrap/current.ssa) and npli through CMake. Ninja reuses
# both when the seed, runtime, and NPL sources are unchanged.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cmake -S "$ROOT" -B "$ROOT/build" -G Ninja >/dev/null
ninja -C "$ROOT/build" nplc npli >/dev/null
printf 'toolchain: %s %s\n' "$ROOT/build/bin/nplc" "$ROOT/build/bin/npli"

#!/bin/bash
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUILD="$ROOT/build"

cmake -S "$ROOT" -B "$BUILD" -G Ninja >/dev/null
ninja -C "$BUILD" gc_stress >/dev/null
output=$($BUILD/runtime/gc_stress)
printf '%s\n' "$output"
printf '%s\n' "$output" | grep -Fq 'PASS: 1000000 allocations'
echo "runtime GC: PASS"

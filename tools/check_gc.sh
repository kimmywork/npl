#!/bin/bash
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUILD="$ROOT/build"

cmake -S "$ROOT" -B "$BUILD" -G Ninja >/dev/null
ninja -C "$BUILD" gc_stress >/dev/null
output=$($BUILD/runtime/gc_stress)
printf '%s\n' "$output"
printf '%s\n' "$output" | grep -Fq 'PASS: 1000000 allocations'
# A 64 KiB nursery with barrier and heap verification at every collection:
# minor collections must run and keep the retained list intact.
output=$(NPL_GC_NURSERY_BYTES=65536 NPL_GC_VERIFY=1 $BUILD/runtime/gc_stress)
printf '%s\n' "$output"
printf '%s\n' "$output" | grep -Eq 'PASS: 1000000 allocations, [0-9]+ collections, [1-9][0-9]* minor'
echo "runtime GC: PASS"

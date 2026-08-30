#!/bin/bash
# Recompute and atomically install bootstrap/current.ssa.
# The file is changed only after a successful gen1/gen2/gen3 fixed-point check.

set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

"$ROOT/tools/check_current_fixed_point.sh" --work-dir "$WORK"

candidate="$WORK/current.ssa"
cp "$WORK/gen3.ssa" "$candidate"
mv "$candidate" "$ROOT/bootstrap/current.ssa"

echo "updated: bootstrap/current.ssa"
shasum -a 256 "$ROOT/bootstrap/current.ssa"

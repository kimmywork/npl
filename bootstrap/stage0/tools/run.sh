#!/bin/bash
# Run the frozen stage0 checks independently of the main gate.
set -eu
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
"$ROOT/bootstrap/stage0/tools/check_stage0.sh"
"$ROOT/bootstrap/stage0/tools/check_stage0_qbe.sh"
"$ROOT/bootstrap/stage0/tools/check_core.sh"
"$ROOT/bootstrap/stage0/tools/check_r7rs.sh"
"$ROOT/bootstrap/tools/check_bootstrap.sh"
python3 "$ROOT/bootstrap/stage0/tools/prop_tests.py"

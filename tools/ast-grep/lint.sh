#!/usr/bin/env bash
# Run ast-grep rule unit tests and scan the repository (C + Scheme/NPL).
# Extra arguments (e.g. --format github) are forwarded to `ast-grep scan`.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OS=$(uname -s)
ARCH=$(uname -m)
case "$OS:$ARCH" in
    Linux:x86_64)  LIB="$ROOT/.cache/ast-grep/scheme-linux-x86_64.so" ;;
    Darwin:arm64)  LIB="$ROOT/.cache/ast-grep/scheme-macos-arm64.dylib" ;;
    Darwin:x86_64) LIB="$ROOT/.cache/ast-grep/scheme-macos-x86_64.dylib" ;;
    *)             LIB="" ;;
esac

if [ -n "$LIB" ] && [ ! -f "$LIB" ]; then
    "$ROOT/tools/ast-grep/setup.sh"
fi

cd "$ROOT"
echo "==> Running ast-grep rule tests..."
ast-grep test --custom-languages allow --skip-snapshot-tests
echo "==> Scanning repository with ast-grep..."
ast-grep scan --custom-languages allow "$@" runtime src lib
echo "ast-grep lint: PASS"

#!/usr/bin/env bash
# Build the tree-sitter-scheme shared library for ast-grep on Linux / macOS.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
CACHE_DIR="$ROOT/.cache/ast-grep"
REPO_DIR="$CACHE_DIR/tree-sitter-scheme"
REV="${TREE_SITTER_SCHEME_REV:-45c72463ac4260ef6e07d1c0642ed43d4e9957b4}"
mkdir -p "$CACHE_DIR"

if [ ! -d "$REPO_DIR/.git" ]; then
    rm -rf "$REPO_DIR"
    git init -q "$REPO_DIR"
    git -C "$REPO_DIR" remote add origin https://github.com/6cdh/tree-sitter-scheme
    git -C "$REPO_DIR" fetch --depth 1 origin "$REV"
    git -C "$REPO_DIR" checkout -q FETCH_HEAD
fi

if [ -n "${CC:-}" ] && command -v "$CC" >/dev/null 2>&1; then
    CC_BIN="$CC"
elif command -v clang >/dev/null 2>&1; then
    CC_BIN=clang
else
    CC_BIN=cc
fi

OS=$(uname -s)
ARCH=$(uname -m)

case "$OS:$ARCH" in
    Linux:x86_64)
        OUT="$CACHE_DIR/scheme-linux-x86_64.so"
        "$CC_BIN" -shared -fPIC -O2 -I "$REPO_DIR/src" "$REPO_DIR/src/parser.c" -o "$OUT"
        ;;
    Darwin:arm64)
        OUT="$CACHE_DIR/scheme-macos-arm64.dylib"
        "$CC_BIN" -shared -fPIC -O2 -I "$REPO_DIR/src" "$REPO_DIR/src/parser.c" -o "$OUT"
        ;;
    Darwin:x86_64)
        OUT="$CACHE_DIR/scheme-macos-x86_64.dylib"
        "$CC_BIN" -shared -fPIC -O2 -I "$REPO_DIR/src" "$REPO_DIR/src/parser.c" -o "$OUT"
        ;;
    *)
        echo "Unsupported platform: $OS:$ARCH" >&2
        exit 1
        ;;
esac

echo "Built $OUT"

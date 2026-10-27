#!/bin/bash
# Install into a temporary prefix and use the toolchain from outside the
# repository with an empty environment: nplc and npli must find QBE, the
# runtime, and the libraries through the install layout alone, even with the
# checkout's lib/ temporarily unreachable by name.
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

[ -n "${NPL_TOOLCHAIN_BUILT:-}" ] || "$ROOT/tools/build_toolchain.sh" >/dev/null
PREFIX="$WORK/prefix with space"
cmake --install "$ROOT/build" --prefix "$PREFIX" >/dev/null

for path in bin/nplc bin/npli libexec/npl/qbe lib/npl/libnpl_runtime.a \
            share/npl/lib/scheme/base.sld share/npl/lib/npl/frontend.sld \
            share/npl/src/compiler/main.npl include/npl/npl.h; do
    [ -e "$PREFIX/$path" ]
done
[ ! -e "$PREFIX/share/npl/src/compiler/tests" ]

mkdir "$WORK/project"
cp "$ROOT/src/compiler/tests/driver-hello.npl" "$WORK/project/hello.npl"
cat > "$WORK/project/script.scm" <<'EOF'
(import (scheme base) (scheme write) (scheme char) (scheme process-context))
(write (list (char-upcase #\a) (cdr (command-line))))
(newline)
EOF
clean() { env -i PATH=/usr/bin:/bin HOME="$HOME" TMPDIR="$WORK" "$@"; }

cd "$WORK/project"
clean "$PREFIX/bin/nplc" hello.npl
[ "$(./hello)" = "hello from nplc" ]
clean "$PREFIX/bin/nplc" -c hello.npl
clean cc -o linked hello.o "$PREFIX/lib/npl/libnpl_runtime.a" -lm
[ "$(./linked)" = "hello from nplc" ]
# C functions for NPL build against the installed runtime header, and the
# installed nplc finds it for define-c-function stubs.
clean cc -std=c99 -Wall -Werror -I "$PREFIX/include/npl" -c \
    -o clamp.o "$ROOT/src/compiler/tests/c-extension/clamp.c"
cp "$ROOT/src/compiler/tests/c-extension/typed.npl" typed.npl
clean cc -std=c99 -c -o typed.o "$ROOT/src/compiler/tests/c-extension/typed.c"
clean ar rcs libdemotyped.a typed.o
clean "$PREFIX/bin/nplc" typed.npl -L . -l demotyped
./typed | cmp "$ROOT/src/compiler/tests/c-extension/typed.out" -
[ "$(clean "$PREFIX/bin/npli" script.scm x)" = '(#\A ("x"))' ]
[ "$(printf '(+ 40 2)\n' | clean "$PREFIX/bin/npli" --no-prompt)" = "42" ]

# The emitted SSA from the installed nplc equals the build-tree nplc's.
clean "$PREFIX/bin/nplc" --emit=ssa hello.npl > installed.ssa
"$ROOT/build/bin/nplc" --emit=ssa hello.npl > tree.ssa
cmp installed.ssa tree.ssa

echo "install layout: PASS"

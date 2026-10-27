#!/bin/bash
# Unified nplc: output kinds, path handling, failure cleanup, and the
# relationship between in-process and file-producing SSA emission.
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
NPLC="$ROOT/build/bin/nplc"
SOURCE="$ROOT/src/compiler/tests/driver-hello.npl"
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

[ -n "${NPL_TOOLCHAIN_BUILT:-}" ] || "$ROOT/tools/build_toolchain.sh" >/dev/null
# Resolution must come from the build-tree install layout alone.
unset NPL_QBE NPL_RUNTIME NPL_STDLIB_ROOT NPL_CC
mkdir -p "$WORK/tmp" "$WORK/path with spaces"
cp "$SOURCE" "$WORK/path with spaces/source program.npl"
printf 'hello from nplc\n' > "$WORK/native.expected"
empty_tmp() { [ -z "$(find "$WORK/tmp" -mindepth 1 -maxdepth 1 -print -quit)" ]; }

# Executable with spaces in both paths; default kind is exe.
OUTPUT="$WORK/path with spaces/native program"
TMPDIR="$WORK/tmp" "$NPLC" "$WORK/path with spaces/source program.npl" \
    -o "$OUTPUT"
"$OUTPUT" > "$WORK/native.out"
cmp "$WORK/native.expected" "$WORK/native.out"
empty_tmp

# Default output name is the source stem in the current directory.
(cd "$WORK/path with spaces" && TMPDIR="$WORK/tmp" "$NPLC" "source program.npl")
"$WORK/path with spaces/source program" > "$WORK/stem.out"
cmp "$WORK/native.expected" "$WORK/stem.out"

# In-process SSA to stdout equals the file-producing SSA path.
"$NPLC" --emit=ssa "$SOURCE" > "$WORK/stdout.ssa"
TMPDIR="$WORK/tmp" "$NPLC" --emit=ssa "$SOURCE" -o "$WORK/file.ssa"
cmp "$WORK/stdout.ssa" "$WORK/file.ssa"
"$NPLC" --emit-qbe "$SOURCE" > "$WORK/alias.ssa"
cmp "$WORK/stdout.ssa" "$WORK/alias.ssa"

# Assembly and object kinds; the object links into a working program.
TMPDIR="$WORK/tmp" "$NPLC" -S "$SOURCE" -o "$WORK/hello.s"
# C symbols carry a leading underscore on macOS only.
grep -E '^_?main:' "$WORK/hello.s" >/dev/null
TMPDIR="$WORK/tmp" "$NPLC" -c "$SOURCE" -o "$WORK/hello.o"
file "$WORK/hello.o" | grep -Ei 'object|relocatable' >/dev/null
cc -o "$WORK/linked" "$WORK/hello.o" "$ROOT/build/lib/npl/libnpl_runtime.a" -lm
"$WORK/linked" > "$WORK/linked.out"
cmp "$WORK/native.expected" "$WORK/linked.out"
empty_tmp

# -L/-l link a C library of declared primitives (docs/ffi.md), with the value
# attached or separate; -l without a value is a usage error.
EXTENSION="$ROOT/src/compiler/tests/c-extension"
mkdir -p "$WORK/c lib"
cc -std=c99 -Wall -Werror -I "$ROOT/runtime" -c -o "$WORK/clamp.o" \
    "$EXTENSION/clamp.c"
ar rcs "$WORK/c lib/libdemoclamp.a" "$WORK/clamp.o"
TMPDIR="$WORK/tmp" "$NPLC" "$EXTENSION/main.npl" -L "$WORK/c lib" \
    -l democlamp -o "$WORK/extension"
"$WORK/extension" > "$WORK/extension.out"
cmp "$EXTENSION/expected.out" "$WORK/extension.out"
TMPDIR="$WORK/tmp" "$NPLC" "-L$WORK/c lib" -ldemoclamp "$EXTENSION/main.npl" \
    -o "$WORK/extension-attached"
"$WORK/extension-attached" > "$WORK/extension.out"
cmp "$EXTENSION/expected.out" "$WORK/extension.out"
empty_tmp

# define-c-function: an executable build generates, compiles, and links the
# C stubs itself; --emit=c-stubs writes them for npli.
cc -std=c99 -Wall -Werror -c -o "$WORK/typed.o" "$EXTENSION/typed.c"
ar rcs "$WORK/c lib/libdemotyped.a" "$WORK/typed.o"
TMPDIR="$WORK/tmp" "$NPLC" "$EXTENSION/typed.npl" -L "$WORK/c lib" \
    -l demotyped -o "$WORK/typed"
"$WORK/typed" > "$WORK/typed.out"
cmp "$EXTENSION/typed.out" "$WORK/typed.out"
empty_tmp
TMPDIR="$WORK/tmp" "$NPLC" --emit=c-stubs "$EXTENSION/typed.npl" \
    -o "$WORK/typed-stubs.c"
"$NPLC" --emit=c-stubs "$EXTENSION/typed.npl" | cmp - "$WORK/typed-stubs.c"
grep -F 'NplVal npl_cfn_demo_add(NplVal a0, NplVal a1)' "$WORK/typed-stubs.c" \
    >/dev/null
# A failed stub compilation names its stage and leaves nothing behind.
set +e
TMPDIR="$WORK/tmp" "$NPLC" --cc "$WORK/missing-cc" "$EXTENSION/typed.npl" \
    -o "$WORK/typed-failed" > /dev/null 2> "$WORK/stubs.err"
stubs_status=$?
set -e
[ "$stubs_status" -ne 0 ]
grep -F 'nplc: cc (c stubs) failed' "$WORK/stubs.err" >/dev/null
[ ! -e "$WORK/typed-failed" ]
empty_tmp

# Runs from outside the repository.
(cd /tmp && TMPDIR="$WORK/tmp" "$NPLC" "$SOURCE" -o "$WORK/outside")
"$WORK/outside" > "$WORK/outside.out"
cmp "$WORK/native.expected" "$WORK/outside.out"

# Compile errors: nonzero status, diagnostic on stderr, no output, no temps.
set +e
TMPDIR="$WORK/tmp" "$NPLC" \
    "$ROOT/src/compiler/tests/contracts/library-unknown-import.npl" \
    -o "$WORK/rejected" > "$WORK/rejected.out" 2> "$WORK/rejected.err"
compile_status=$?
set -e
[ "$compile_status" -ne 0 ]
grep -F 'only import name not exported' "$WORK/rejected.err" >/dev/null
[ ! -s "$WORK/rejected.out" ]
[ ! -e "$WORK/rejected" ]
empty_tmp

# Missing tool: the stage and status are reported, nothing is left behind.
set +e
TMPDIR="$WORK/tmp" "$NPLC" --qbe "$WORK/missing-qbe" \
    "$SOURCE" -o "$WORK/qbe-failure" > /dev/null 2> "$WORK/qbe.err"
qbe_status=$?
set -e
[ "$qbe_status" -eq 127 ]
grep -F 'nplc: qbe failed' "$WORK/qbe.err" >/dev/null
[ ! -e "$WORK/qbe-failure" ]
empty_tmp

# Reserved and unknown kinds/options are rejected with status 2.
for arguments in "--emit=dylib" "--emit=staticlib" "--emit=bogus" "--bogus"; do
    set +e
    "$NPLC" $arguments "$SOURCE" > /dev/null 2> "$WORK/option.err"
    option_status=$?
    set -e
    [ "$option_status" -eq 2 ]
    [ -s "$WORK/option.err" ]
done
set +e
"$NPLC" "$SOURCE" -l > /dev/null 2> "$WORK/option.err"
option_status=$?
set -e
[ "$option_status" -eq 2 ]
grep -F 'nplc: -l needs a value' "$WORK/option.err" >/dev/null
"$NPLC" --help | grep -F 'usage: nplc' >/dev/null

# --keep-temps reports and keeps the intermediate directory.
TMPDIR="$WORK/tmp" "$NPLC" --keep-temps "$SOURCE" -o "$WORK/kept" \
    2> "$WORK/keep.err"
kept=$(sed -n 's/^nplc: kept temporaries in //p' "$WORK/keep.err")
[ -s "$kept/source.ssa" ] && [ -s "$kept/source.s" ]
rm -rf "$kept"

echo "unified nplc driver: PASS"

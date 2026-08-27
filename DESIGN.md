# NPL Design

## Goal

NPL is a Scheme dialect compiled through QBE to native code. The finished compiler provides an R7RS-small compatible core, gradual typing, and tensor/NN constructs.

## Bootstrap

NPL uses a historical relay. Each compiler generation emits canonical QBE SSA for the next generation.

```text
stage0 C sources -> nplc0
nplc0(current compiler sources) -> bootstrap/stage1.ssa -> nplc1
nplc1(next compiler sources)    -> bootstrap/stage2.ssa -> nplc2
nplc2(next compiler sources)    -> bootstrap/stage3.ssa -> nplc3
```

Compiler sources evolve in Git history. The repository stores SSA relay artifacts, QBE source, and runtime source. Compiler binaries are rebuilt from SSA:

```bash
qbe -o compiler.s bootstrap/stageN.ssa
cc -o nplcN compiler.s build/runtime/libnpl_runtime.a
```

Every compiler exposes:

```bash
nplcN --emit-qbe source.npl
```

A generation is accepted when its predecessor reproduces its checked-in SSA. A stable generation is accepted when two consecutive compilers emit byte-identical SSA from the same sources.

## Stage0 Language

Stage0 is an untyped C99 compiler for compiler construction:

- fixnum, boolean, string, symbol, pair, and closure values;
- `quote`, `if`, `cond`, `begin`, `define`, `lambda`, `set!`, `let`, and `let*`;
- integer, pair, string, and file primitives;
- lexical closures and proper tail calls;
- top-level `(include "path" ...)`, resolved relative to the including file.

Included files share one top-level namespace and preserve source order.

## Compiler Evolution

The current NPL compiler source is modular under `src/compiler/`. A generation uses the language implemented by its predecessor. New compiler features are used by later compiler generations and by programs compiled with the current generation.

Bootstrap source composition uses `include`. The full compiler later implements R7RS `define-library`, `import`, `export`, `include`, `include-ci`, and `cond-expand`.

## Runtime

The C runtime provides tagged values, allocation, garbage collection, process entry, the generic call ABI, and file primitives. Language libraries are implemented in NPL.

## Layout

```text
stage0/                 C99 bootstrap compiler
src/compiler/           current NPL compiler generation
src/lib/                NPL libraries
bootstrap/              canonical QBE SSA relay artifacts
runtime/                C runtime
external/qbe-1.3/       pinned QBE backend
tests/                  acceptance tests
tools/                  build and bootstrap checks
```

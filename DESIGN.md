# NPL Design

## 1. Goal

NPL is a native Scheme dialect compiled through QBE. The target language contains:

- an R7RS-small compatible core and library system;
- gradual typing with local HM inference;
- records, row polymorphism, and restricted refinements;
- first-class tensors, autograd, and static neural-network modules;
- deterministic native compilation on arm64 and amd64.

## 2. Three Codebases

The repository permanently contains three compiler implementations.

### Stage0

`stage0/` is a C99 compiler for a small untyped NPL language. It reads source files, emits QBE SSA, and can invoke QBE and the C linker to produce an executable.

Its language contains the dynamic values and forms needed by the bootstrap compiler: fixnums, booleans, strings, symbols, pairs, lexical functions, definitions, mutation, conditionals, `cond`, local bindings, quotation, file input, and top-level source inclusion.

### Bootstrap Compiler

`bootstrap/compiler/` is a permanent NPL compiler written in the stage0 language. Stage0 builds its seed binary. The seed compiler compiles the same bootstrap sources to canonical SSA. A compiler rebuilt from that SSA must reproduce it byte for byte.

The bootstrap compiler only supports the implementation language required to build the full compiler.

### Full Compiler

`src/compiler/` is the current complete compiler. It evolves with the project. The bootstrap compiler builds its initial version. Once the full compiler uses language features outside the bootstrap subset, its checked-in canonical SSA is the recovery path; prior full-compiler sources remain available through Git history.

## 3. Bootstrap Artifacts

The repository stores:

```text
bootstrap/compiler/          permanent bootstrap compiler sources
bootstrap/bootstrap.ssa      bootstrap compiler fixed-point SSA
bootstrap/current.ssa        current full compiler SSA
```

No compiler binary is required in version control. A binary is restored with:

```bash
qbe -o compiler.s bootstrap/current.ssa
cc -o nplc compiler.s build/runtime/libnpl_runtime.a
```

The bootstrap verification is:

```text
C compiler -> stage0
stage0(bootstrap sources) -> seed bootstrap compiler
seed(bootstrap sources) -> candidate bootstrap.ssa
candidate bootstrap.ssa == checked-in bootstrap.ssa
QBE(bootstrap.ssa) -> bootstrap compiler
bootstrap(bootstrap sources) == bootstrap.ssa
bootstrap(full sources) == checked-in current.ssa
QBE(current.ssa) -> full compiler
```

Both stage0 and NPL compilers emit QBE SSA. Stage0 also provides direct executable output.

## 4. Source Composition and Libraries

`(include "path" ...)` is the bootstrap source-composition form. Paths are relative to the including file. Included definitions share the including program's top-level namespace and retain source order.

The full compiler implements the R7RS library system:

- `define-library`, `import`, and `export`;
- `include` and `include-ci`;
- `cond-expand` and feature identifiers;
- deterministic library-name to source-path resolution;
- import-set modifiers `only`, `except`, `prefix`, and `rename`.

R7RS `(scheme load)` is a separate runtime library operation.

## 5. Language

### Dynamic Core

NPL implements R7RS lexical scope, proper tail calls, multiple values, first-class procedures, full continuations, `dynamic-wind`, exceptions, parameters, promises, records, bytevectors, ports, and textual and binary I/O.

`syntax-rules` provides hygienic macros and ellipsis. Derived forms are expanded before type checking.

### Numeric Tower

The numeric tower contains exact integers with automatic bignum promotion, rationals, complex numbers, and IEEE floating-point values. QBE `w`, `l`, `s`, and `d` are used where static types permit unboxed lowering.

### Gradual Types

Missing annotations produce `Dynamic`. Typed code uses:

- `Integer`, `Float`, `Bool`, `String`, `Symbol`, and `Dynamic`;
- function, pair, vector, tensor, and record types;
- local let-polymorphism using HM inference;
- row-polymorphic records;
- restricted predicate refinements.

Typed and dynamic code interoperate through runtime guards. Proven scalar values and typed calls are emitted unboxed.

## 6. Compiler Pipeline

```text
source loading
S-expression reader
library resolution
macro expansion
core AST
HM inference and annotation checking
guard insertion
closure conversion and tail lowering
QBE SSA emission
```

Each pass has one input representation and one output representation. Diagnostics carry source path, line, and column.

## 7. Runtime

The C runtime is the platform substrate:

- process entry and QBE ABI;
- mmap-backed allocation;
- stop-the-world mark-sweep collection;
- root registration and stack scanning;
- tagged dynamic values;
- closure invocation and tail-call trampoline;
- OS file and port primitives.

Language-level collections, algorithms, numeric policy, and standard libraries are written in NPL.

## 8. Tensors and Neural Networks

A tensor is a first-class value with element type, shape, strides, storage, and gradient state. The initial backend is CPU dense row-major storage.

The compiler provides tensor arithmetic, broadcasting, matrix multiplication, reductions, reverse-mode autograd, and static `nn-module` expansion. Static shapes are checked at compile time where known; dynamic shapes are guarded at runtime.

## 9. Layout

```text
stage0/                     C99 compiler
bootstrap/compiler/         permanent NPL bootstrap compiler
bootstrap/bootstrap.ssa     bootstrap fixed point
bootstrap/current.ssa       current full compiler artifact
src/compiler/               evolving full compiler
src/lib/                    NPL libraries
runtime/                    C runtime substrate
external/qbe-1.3/           pinned QBE backend
tests/                      language tests
tools/                      acceptance scripts
```

## 10. Acceptance

A change is complete only when its focused tests pass, bootstrap SSA artifacts are reproducible, and a compiler restored from `bootstrap/current.ssa` can compile and run the relevant programs.

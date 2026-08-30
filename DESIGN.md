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

Its language contains the dynamic values and forms needed by the bootstrap compiler: fixnums, booleans, strings, symbols, pairs, lexical functions, definitions, mutation, conditionals, `cond`, `case`, local bindings, quotation, file input, and top-level source inclusion. Derived bootstrap forms are directly desugared; stage0 has no general macro expander.

### Bootstrap Compiler

`bootstrap/compiler/` is a permanent NPL compiler written in the stage0 language. Stage0 builds its seed binary. The seed compiler compiles the same bootstrap sources to canonical SSA. A compiler rebuilt from that SSA must reproduce it byte for byte.

The bootstrap compiler only supports the implementation language required to build the full compiler.

### Full Compiler

`src/compiler/` is the evolving full compiler. It is not yet the complete target language. The permanent bootstrap compiler builds a full-compiler seed; that seed emits the checked-in current canonical SSA. Later full-compiler sources remain available through Git history, while `bootstrap/current.ssa` preserves the binary recovery path.

The current implementation contains the Dynamic Core, Reader/Datum, hygienic macros, explicit control and the minimal R7RS library system described in `PLAN.md`. Standard libraries, gradual types, and tensor features remain planned.

The full compiler source is organized by responsibility and loaded in deterministic source order by `src/compiler/main.npl`:

- `common/`: shared state and collection helpers;
- `reader/`: tokenization, delimiter validation, datum labels, and quasiquote;
- `frontend/`: form parsing, hygienic expansion, library resolution, and source inclusion;
- `backend/`: QBE state, static data, closures, expressions, tail lowering, bindings, and top-level emission;
- `tests/`: the NPL test framework, positive language suites, runtime contracts, and malformed-reader fixtures.

This is source composition rather than a public module system: includes share one compiler namespace, while directory ownership keeps later passes from accumulating in monolithic frontend/backend files.

## 3. Bootstrap Artifacts

The repository stores:

```text
bootstrap/compiler/          permanent bootstrap compiler sources
bootstrap/bootstrap.ssa      bootstrap compiler fixed-point SSA
bootstrap/current.ssa        current full compiler SSA
```

No compiler binary is required in version control. The supported build process is:

```bash
./tools/build_nplc.sh                 # writes build/bin/nplc
./build/bin/nplc --emit-qbe program.npl > program.ssa
```

The direct equivalent is:

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
bootstrap(full sources) -> full compiler seed SSA
QBE(full compiler seed SSA) -> full compiler seed
full compiler seed(full sources) == checked-in current.ssa
QBE(current.ssa) -> full compiler
full compiler(full sources) == current.ssa
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

The implemented dynamic core provides lexical closures, first-class fixed-arity procedures, mutation, `let*`, `letrec`, `letrec*`, internal definitions, R7RS truth semantics, and proper tail calls through the runtime trampoline.

The reader supports dotted data, R7RS escaped strings over the required ASCII repertoire, characters, vector and bytevector literals, nested quasiquotation, datum labels with shared and cyclic pair/vector structure, and line/column delimiter diagnostics.

The current implementation contains the Dynamic Core, Reader/Datum, hygienic `syntax-rules`, explicit-control features, and the minimal R7RS library system. It supports multiple values, full continuations, `dynamic-wind`, exceptions, parameters, promises, and deterministic `.sld` library loading. Standard libraries, records, ports, the numeric tower, gradual types, and tensor features remain planned.

### Numeric Tower

The target numeric tower will contain exact integers with automatic bignum promotion, rationals, complex numbers, and IEEE floating-point values. QBE `w`, `l`, `s`, and `d` will be used where static types permit unboxed lowering.

### Gradual Types

The planned gradual type system treats missing annotations as `Dynamic`. Typed code will use:

- `Integer`, `Float`, `Bool`, `String`, `Symbol`, and `Dynamic`;
- function, pair, vector, tensor, and record types;
- local let-polymorphism using HM inference;
- row-polymorphic records;
- restricted predicate refinements.

Typed and dynamic code will interoperate through runtime guards. Proven scalar values and typed calls will be emitted unboxed.

## 6. Compiler Pipeline

The target full compiler pipeline is:

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

Each pass will have one input representation and one output representation. Completed reader diagnostics report line and column for delimiter errors; path-aware diagnostics remain planned.

## 7. Runtime

The C runtime is the platform substrate:

- process entry and QBE ABI;
- mmap-backed allocation;
- stop-the-world mark-sweep collection;
- root registration and stack scanning;
- tagged dynamic values;
- closure invocation and tail-call trampoline;
- OS file and port primitives.

Language-level collections, algorithms, numeric policy, and standard libraries will be written in NPL.

## 8. Tensors and Neural Networks

The target tensor value has element type, shape, strides, storage, and gradient state. The initial backend will use CPU dense row-major storage.

The compiler will provide tensor arithmetic, broadcasting, matrix multiplication, reductions, reverse-mode autograd, and static `nn-module` expansion. Static shapes will be checked at compile time where known; dynamic shapes will be guarded at runtime.

## 9. Layout

```text
stage0/                     C99 compiler
bootstrap/compiler/         permanent NPL bootstrap compiler
bootstrap/bootstrap.ssa     bootstrap fixed point
bootstrap/current.ssa       current full compiler artifact
src/compiler/               evolving full compiler
  common/                    shared compiler helpers
  reader/                    lexer and datum reader
  frontend/                  form parsing and source loading
  backend/                   QBE lowering and emission
  tests/                     NPL test harness and focused suites
src/lib/                    NPL libraries
runtime/                    C runtime substrate
external/qbe-1.3/           pinned QBE backend
tools/                      acceptance scripts
```

## 10. Acceptance

A change is complete only when its focused tests pass, bootstrap SSA artifacts are reproducible, and a compiler restored from `bootstrap/current.ssa` can compile and run the relevant programs.

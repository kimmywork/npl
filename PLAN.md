# NPL Plan

## Bootstrap Baseline

Complete.

- `stage0/` builds executable programs and emits QBE SSA.
- `bootstrap/compiler/` is the permanent NPL bootstrap compiler.
- `bootstrap/bootstrap.ssa` is its byte-identical fixed point.
- `src/compiler/` is the evolving full compiler.
- `bootstrap/current.ssa` restores the current full compiler.

## 1. Dynamic Core

Status: complete.

Implemented in the full compiler:

- `lambda`, lexical captures, first-class procedure calls, and `set!`;
- `let*`, `letrec`, `letrec*`, and internal `define`;
- proper self and cross-function tail calls;
- R7RS truth semantics.

Acceptance: closure, mutation, recursive binding, internal definition, and ten-million-iteration cross-function tail-call tests pass through the restored full compiler.

Evidence: `src/compiler/tests/dynamic-core.npl` passes through `tools/check_full_compiler.sh`.

## 2. Reader and Datum

Status: complete for the R7RS ASCII repertoire.

Implemented in the full compiler:

- dotted lists and escaped strings;
- characters, vectors, and bytevectors;
- quasiquote, unquote, and unquote-splicing;
- datum labels and source-located reader errors.

Acceptance: read, quote, construct, compare, nested quasiquote, shared/cyclic datum, and malformed-input tests pass through the restored full compiler.

Evidence: `src/compiler/tests/reader-datum.npl` and the negative fixtures under `src/compiler/tests/reader-errors/` pass through `tools/check_full_compiler.sh`.

The supported character repertoire is ASCII, which is the R7RS-required minimum. Broader Unicode string semantics remain part of the standard-library work.

## 3. Macros and Derived Forms

Status: complete.

Implemented hygienic declarative `syntax-rules`, ellipsis, syntax scopes, and the remaining derived forms.

Evidence: the macro, ellipsis, scope, derived-form, and malformed-macro suites pass through `tools/check_full_compiler.sh`; the current compiler fixed point is verified by `tools/check_current_fixed_point.sh`.

## 4. Control and Exceptions

Status: complete.

Implemented explicit-control lowering, multiple values, unlimited-extent multi-shot continuations, `dynamic-wind`, exceptions, `guard`, parameters, and promises.

Evidence: the focused suites and runtime contracts pass through `tools/check_full_compiler.sh`; the compiler relay remains byte-identical across gen1/gen2/gen3.

## 5. R7RS Library System

Status: complete for the minimal scope.

Implemented `define-library`, top-level `import`, `export`, deterministic `.sld` resolution, `only`, `except`, `prefix`, `rename`, `begin`, `include`, `include-ci`, `include-library-declarations`, and `cond-expand`. Library loading has per-compilation caching and rejects circular dependencies. Package management, version resolution, dynamic loading, precompiled caches, and cyclic libraries are not implemented.

Evidence: `src/compiler/tests/libraries.npl`, its `.sld`/include fixtures, and library contract fixtures pass through `tools/check_full_compiler.sh`.

## 6. First-Wave Standard Libraries

Status: in progress.

Complete the practical R7RS dependency closure for native tooling and interactive evaluation: textual/string and binary/bytevector ports, explicit source-based libraries, base collections, `cxr`, `char`, `case-lambda`, file/process context, read/write, and promises. This wave is broad but is not claimed as full R7RS conformance; numeric tower completion, full public `eval`, `load`, and compatibility layers are deferred until a usable driver and REPL exist.

## 7. Unified Compilation Driver

Status: complete.

`build/bin/nplc source.npl -o output` is implemented in `src/driver/main.npl`. The NPL driver performs raw SSA emission, QBE assembly generation, and native linking while retaining `--emit-qbe`. It owns option parsing, configured toolchain paths, failure diagnostics, status propagation, and temporary-file cleanup. `tools/build_nplc.sh` only restores the raw compiler from canonical SSA, injects absolute build paths into a generated NPL entry file, and compiles that NPL driver.

## 8. REPL

Status: persistent core in progress.

Implemented the shared evaluator and interaction loop in NPL. `(npl frontend)` packages the existing compiler reader, hygienic macro expander, derived-form expansion, library/import resolver, and core lowering as a persistent interaction session. `(npl evaluator)` executes that shared core AST with persistent lexical environments, definitions, mutation, closures, recursion, core conditionals/sequencing, primitive procedures, multiple values, and `call/cc`; macro and import semantics are not reimplemented in the evaluator. `src/repl/main.npl` provides prompts, EOF/`(exit)` handling, result printing, and recovery from both evaluator and malformed-reader errors. `build/bin/npl-repl` is an NPL compiler output; C only supplies the existing datum reader plus a structured recoverable `read-result` boundary.

Persistent top-level `syntax-rules`, hygiene, derived syntax, and shared `only`/`rename` import resolution are implemented and covered by evaluator/native differential fixtures. Evaluator `dynamic-wind`, continuable/non-continuable exception dispatch, parameters, and continuation restoration across wind, handler, and parameter state are now implemented against the existing native control contract. Still required before this step is complete: promises and broader interactive library coverage. Full public `(scheme eval)` remains deferred.

Detailed increments, rewrite policy, and acceptance evidence: [`docs/plan-stdlib-driver-repl.md`](docs/plan-stdlib-driver-repl.md).

## Gate

Each increment regenerates `bootstrap/current.ssa`, restores the full compiler from that SSA, and runs focused tests. `tools/check_bootstrap.sh` owns compiler recovery checks; `tools/check_current_fixed_point.sh` owns the historical gen1/gen2/gen3 relay check; `tools/check_full_compiler.sh` owns full-language suites and contracts. `tools/update_current_ssa.sh` installs a new canonical current SSA only after that relay check succeeds. `tools/build_nplc.sh` builds a complete native `nplc` binary from `bootstrap/current.ssa`. `bootstrap/compiler/` and `bootstrap/bootstrap.ssa` remain unchanged.

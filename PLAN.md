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

Status: next after the first-wave library dependency closure.

Provide a one-command `nplc source.npl -o output` pipeline while retaining raw SSA emission, then implement the orchestration wrapper in NPL using a minimal non-standard argv-based process API. The process API is intentionally separate from R7RS standard libraries.

## 8. REPL

Status: next after the native driver core.

Implement a persistent `npl-repl` over the shared evaluator engine that will later back `(scheme eval)`. The evaluator core, persistent environments, macro/import behavior, and error recovery are hard REPL dependencies; only the complete public `(scheme eval)` environment API and remaining conformance are deferred. Repeated whole-program recompilation is not the implementation strategy.

Detailed increments, rewrite policy, and acceptance evidence: [`docs/plan-stdlib-driver-repl.md`](docs/plan-stdlib-driver-repl.md).

## Gate

Each increment regenerates `bootstrap/current.ssa`, restores the full compiler from that SSA, and runs focused tests. `tools/check_bootstrap.sh` owns compiler recovery checks; `tools/check_current_fixed_point.sh` owns the historical gen1/gen2/gen3 relay check; `tools/check_full_compiler.sh` owns full-language suites and contracts. `tools/update_current_ssa.sh` installs a new canonical current SSA only after that relay check succeeds. `tools/build_nplc.sh` builds a complete native `nplc` binary from `bootstrap/current.ssa`. `bootstrap/compiler/` and `bootstrap/bootstrap.ssa` remain unchanged.

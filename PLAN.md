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

Status: next.

Implement hygienic `syntax-rules`, ellipsis, `and`, `or`, `when`, `unless`, `do`, and complete `cond` and `case`.

## 4. Control and Exceptions

Implement multiple values, full `call/cc`, `dynamic-wind`, exceptions, `guard`, parameters, and promises.

## 5. R7RS Library System

Implement `define-library`, `import`, `export`, import-set modifiers, `include-ci`, and `cond-expand`.

## 6. Standard Libraries

Implement the R7RS-small libraries, ports, file I/O, strings, vectors, bytevectors, and the numeric tower.

## Gate

Each increment regenerates `bootstrap/current.ssa`, restores the full compiler from that SSA, and runs focused tests. `tools/check_bootstrap.sh` owns compiler recovery checks; `tools/check_full_compiler.sh` owns full-language suites and contracts. `bootstrap/compiler/` and `bootstrap/bootstrap.ssa` remain unchanged.

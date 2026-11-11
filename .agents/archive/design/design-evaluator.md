# Design: Closure-Compiling Evaluator for npli

Status: accepted 2026-10-22 (increment E1) with the recommended option for
D1, D2, and D3 (see "Decisions"). Builds on
[design-primitives](design-primitives.md) sections 3 and 4 and the P0
measurements (worklog 2026-10-20 11:10).

## Goal

npli evaluates programs with the same semantics and the same error objects
as nplc, and fast enough that every bench workload finishes in under 30 s
(today all exceed 30 s). The evaluator stays NPL source compiled by nplc and
reuses the shared frontend for reading, macros, libraries, and lowering.

## Current State

Checked 2026-10-22.

- `lib/npl/evaluator-*.scm` (about 2,000 lines) interpret the frontend's
  core AST in continuation-passing style. Every step allocates evaluator
  continuations (927 of 1,125 objects per loop iteration in P0), and every
  variable reference is a string search through frame lists
  (`eval-find-binding-owner`).
- Control is reimplemented: the evaluator's own continuation, handler, wind,
  parameter, and promise objects (`evaluator-control.scm`, 335 lines).
- Primitives are a hand-written name list (`EVAL-PRIMITIVE-NAMES`) and a
  string `cond` dispatch with NPL reimplementations
  (`evaluator-primitives.scm`, 526 lines). Some errors differ from native,
  for example `+: expected numbers` (`src/repl/tests/primitive-errors.err`).
- Standard libraries are interpreted from `lib/scheme/` in every session,
  so `(scheme base)` `+` runs interpreted `integer.scm`: the imported loop
  is 8x the import-free one.
- Evaluator procedures are tagged vectors, not procedures. Native higher-
  order procedures cannot call them, and `EVAL-UNSPECIFIED` leaks into data:
  `(write (list (vector-set! (vector 1) 0 2)))` prints `(#(906))` under
  npli and `(())` native.

## Design

### 1. Compile core AST to native closures

`eval-in-environment` keeps its frontend step: `frontend-expand-datum`
returns lowered core forms (tags 100-222, `src/compiler/frontend/forms.npl`).
A new compiler pass (`evaluator-compile.scm`) turns each core form, once,
into a native NPL closure `(lambda (frame) ...)`, and then calls it.

- **Variables.** The frontend renames every local to a unique emitted name,
  so the compile-time scope is a list of frames of names, and each reference
  becomes a lexical address (depth, index) or a global cell. At run time a
  frame is a vector `#(parent value ...)`. `set!` stores into the frame slot
  or the cell, so no boxes are needed. There are no name lookups at run
  time.
- **Globals.** Each global is a cell (`(value . name)`) in the session's
  global table, created when it is first referenced or defined. A reference
  to a cell that was never defined raises `unbound identifier: name` when it
  is read, so a later REPL definition fills the same cell.
- **Procedures.** A `lambda` evaluates to a native closure. Fixed arities 0-4
  and rest forms compile to native lambdas of the same shape, for example
  `(lambda (a b) (body (vector frame a b)))`. Native arity checking then
  raises the native error (`procedure: wrong number of arguments`, irritant
  the count) at no cost. Larger arities take `(lambda arguments ...)` and
  raise the same error object.
- **Applications.** These are specialized by argument count (0-4, then
  general) and by operator form (global cell, local, other expression).
  Argument nodes run in order, and the call is in tail position of the node
  closure. Native CPS therefore makes every user tail call a native tail
  call, and deep non-tail recursion uses heap continuations, not the C
  stack.
- **Other forms.** `if`, `begin`, `define`, `set!`, `quote`, and the literal
  tags each compile to one closure. `let` compiles to a frame push.
  `letrec`/`letrec*` and internal definitions arrive already lowered by the
  frontend (`core-body-expression`), and `let*` keeps using the frontend's
  `core-expand-letstar`.

### 2. Native control, values, and errors

Evaluator procedures are native closures, so the native operators apply to
them unchanged: `call/cc`, `dynamic-wind`, `with-exception-handler`,
`raise`, `raise-continuable`, `values`, `call-with-values`, `apply`,
parameters, and promises. P0 prototype B checked re-entry, escapes from
native `map`, handlers, and `dynamic-wind` with `raise`.

- These operators bind to the native procedures (section 3). The evaluator's
  own control objects and `evaluator-control.scm` are deleted.
- Errors are raised objects, not returned values. Evaluator-detected errors
  (unbound identifier, immutable binding, frontend errors) raise R7RS error
  objects through `error`, so `guard` catches them like any other error.
  `eval-error?`/`eval-error-message` go away. `(npl eval-runtime)` no longer
  re-raises.
- The REPL, script runner, and `load` evaluate each top-level form under a
  handler that prints `error: ` and the report that `npl_report_error_object`
  prints for native programs: the message, then `: irritant` for each.
  Script mode keeps exit status 70. Multiple values print as today.

### 3. Primitives and standard libraries bound to native procedures

A compiler form provides native values that npli can bind by name:

```scheme
(%library-definitions (npl primitive) (scheme base) (scheme char) ...)
; => (("npl/primitive" 0 ("car" . <procedure>) ...)
;     ("scheme/base" checksum ("list" . <procedure>) ("record-constructor" . <procedure>) ...)
;     ...)
```

As implemented in E2b (2026-10-23, `src/compiler/frontend/library-definitions.npl`):

- The form expands to an association list of every value definition in each
  named library's body, internal ones included, keyed by source name. For
  `(npl primitive)` it lists the value exports, which are generated from the
  shared primitive table (P2a). Whole-program compilation already contains
  all of these procedures. The form only makes them reachable from npli's
  own source.
- When a session imports a library, the shared frontend loads and expands
  its source exactly as before (syntax, hygiene, `cond-expand`, `include`).
  It then asks the evaluator's hook (`frontend-set-native-hook!`) whether the
  native table has this library with the same definition names and the same
  checksum. The checksum is a hash of the parsed declarations, including
  included files, so it covers every source file. On a match the expanded
  body is replaced by one form, `(%eval-bind-native key pairs)`, which binds
  each definition's cell to its native value. Exported macros keep working:
  their templates refer to internal names such as `record-constructor`,
  which are now native values.
- Bound natively: `(scheme base)`, `case-lambda`, `char`, `complex`, `cxr`,
  `file`, `inexact`, `lazy`, `process-context`, `read`, `time`, `write`.
  `(scheme eval)`, `load`, `repl`, and `r5rs` stay evaluated from source,
  because their procedures act on the running evaluator's environments.
- If either check fails (a `lib/` edited after npli was built), or the
  library is the user's own, the body is closure-compiled and run like any
  other code. That path is slower but still correct.
- The base session environment binds the primitive names as today, so
  import-free sessions and `(npl primitive)` imports behave as in native
  code. `EVAL-PRIMITIVE-NAMES`, the string dispatch, and the NPL
  reimplementations are deleted.
- `define-c-primitive` stays rejected in npli until P5.

Cost: the npli executable contains the compiled standard library, so it is
larger. Library expansion still runs once per session (a script that only imports
`(scheme base)` takes about 0.35 s user today). Caching it is out of scope.

### 4. The unspecified value (decision D1)

Native runtime procedures with unspecified results (`display`, `newline`,
`vector-set!`, `set-car!`, ...) return `()`, and the compiler emits `#f` for
`(if #f #f)`. The REPL must not print anything after `(display "x")`, since
every REPL fixture relies on that. Once results are native values, the REPL
can no longer tell `()` from "unspecified".

Recommended: a runtime unspecified immediate (`IMM_UNSPECIFIED`).
- Returned by those runtime procedures and emitted by the compiler for
  one-armed `if`, `set!`, and definitions.
- `write` prints it as `#<unspecified>` (as Guile does), and the REPL skips
  printing it.
- R7RS leaves these results unspecified, so programs that rely on `()` are
  already non-portable. Native output changes only where such a value is
  printed. This is a runtime and compiler change with one seed relay, done
  as its own increment before the evaluator switch.

Alternative: keep `()` and have the REPL print nothing for `()` results of
top-level expressions. This is simpler, but `'()` typed at the REPL would
print nothing, and `(if #f #f)` would still differ between npli and native.

### 5. Environments and `eval`

An environment is a global table plus a frontend session and a mutability
flag. `(environment '(scheme base))` builds a frozen one, as today.
Assignment to an immutable binding is detected when the `set!` is compiled
and compiles to raising `eval: immutable binding`, so it costs nothing at
run time. `eval`, `load`, and the REPL share one path:
`frontend-expand-datum`, then compile, then run.

### 6. Known behavior change

A continuation captured during one REPL form and invoked during a later one
re-enters the earlier form's native continuation, which includes the rest
of the REPL loop. Input continues from the port's current position. This
matches native semantics for a program that reads and evaluates forms. A
test records it. Delimiting each REPL form is not available without
delimited continuations.

## Expected Size

Deleted: `evaluator-control.scm`, most of `evaluator-primitives.scm` and
`evaluator-records.scm`, and the string dispatch in `evaluator-core.scm`
(about 1,300 lines). Added: `evaluator-compile.scm` (~450), library binding
(~150), and session and REPL changes (~100). Each file stays under 800 lines.

## Increments

1. **U0 (done 2026-10-22):** runtime unspecified value, compiler emission,
   writer, REPL suppression; fixtures and the native suites updated where
   they print such values; seed relay.
2. **E2a (done 2026-10-23):** closure compiler, native control and errors, primitives bound
   through `%library-definitions` for `(npl primitive)`. Standard libraries
   are still compiled from source in each session. Compiler change for the
   form, seed relay. Acceptance:
   - All 31 R7RS fixtures pass under native, npli, and Guile.
   - `src/compiler/tests/runtime-errors.npl` and `primitive-errors.npl`
     pass under npli with native messages; the expected `.err` files change
     only toward native text, and each change is listed.
   - chibi `r7rs-tests.scm`: 1207 passed under npli.
   - New tests: REPL continuation re-entry, an evaluator procedure passed to
     native `map`/`sort`, and an error raised from a C primitive through an
     evaluator frame.
3. **E2b (done 2026-10-23):** standard libraries bound natively. Acceptance:
   - The imported loop is within 1.5x of the import-free loop.
   - The fallback path is tested by importing a copied, edited library.
4. **E3 (done 2026-10-23; slowest workload 18.4 s):** evaluator-side tuning (operand kinds read directly, constant
   folding of operator cells where safe) until all 9 bench workloads finish
   under npli within 30 s. If native non-tail call cost still blocks that,
   a compiler change is decided separately.

## Decisions

Accepted by the user 2026-10-22: D1 runtime immediate, D2
`%library-definitions` form, D3 native re-entry.

- **D1** unspecified value: runtime immediate (recommended) or REPL-only
  suppression of `()`.
- **D2** native tables: the `%library-definitions` compiler form
  (recommended; generated from the frontend, so it cannot drift) or a
  checked-in generated source file with a gate check.
- **D3** REPL continuation re-entry as described in section 6 (recommended)
  or a restriction that raises an error on cross-form re-entry.

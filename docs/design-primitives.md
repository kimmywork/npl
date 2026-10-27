# Design: Shared Primitives and C Foreign Calls

Status: accepted direction 2026-10-20; sections 3 and 4 revised after the
P0 measurements. The increment order, with the GC nursery work, is the
"Agreed Plan" in [workstate](../.agents/workstate.md).

## Goal

One declaration of each runtime primitive, used by the compiler, the
`(npl primitive)` library, and the evaluator alike. C symbol names follow
from a rule, not a table. Wrapping a C library means writing library code,
not editing the compiler and regenerating the seed.

## Current State

Checked 2026-10-19 against the code.

- **Three independent name lists.**
  - `src/compiler/backend/primitives.npl` `PRIMITIVE-TABLE`: 165 rows of
    (name kind symbol required optional). Kinds: 124 `fixed`, 21 `special`,
    8 `out-port`, 6 `in-port`, 6 `count`.
  - `src/compiler/frontend/library-primitives.npl` `PRIMITIVE-VALUE-SOURCE`:
    the names `(npl primitive)` exports, as one string literal.
  - `lib/npl/evaluator-session.scm` `EVAL-PRIMITIVE-NAMES`: 158 names. They
    dispatch through 92 `string=?` branches in `evaluator-primitives.scm`,
    plus more in `evaluator-records.scm`. Arithmetic and comparison are
    reimplemented in NPL there (`eval-fold-plus`, `eval-number-comparison`).
- **Names mostly follow a rule already.** Of the 150 rows with a C symbol,
  126 match the rule in "Naming Rule" below. The other 24 differ, for
  example `symbol?` -> `npl_symbol_predicate`, `string-ref` ->
  `npl_string_char_ref`, `display` -> `npl_display_port`, `read-u8` ->
  `npl_read_byte`, `run-process` -> `npl_process_run`.
- **Adding a primitive costs:** C plus an `npl.h` declaration, a table row,
  a `PRIMITIVE-VALUE-SOURCE` entry, and, for npli, a name plus a dispatch
  branch. The table is compiler source, so every addition also needs a seed
  relay (`tools/update_current_ssa.sh`).
- **The evaluator already has a bridge.** `eval-host-primitive`
  (`lib/npl/evaluator-control.scm`) runs a native procedure under `guard` and
  re-raises a native error object through the evaluator's handlers.
  `eval-apply` already applies any `procedure?` value that way. npli is
  compiled by nplc, so every native primitive is reachable as a value in
  evaluator source.
- **Divergence the lists cause:** for example, evaluator built-in numeric
  primitives report `+: expected numbers` instead of the native error object
  (open item in the workstate).
- **No foreign-call facility exists.** There is no `dlopen` use, and no
  nplc link option for extra libraries.

## Design

### 1. Primitive declarations in library source

As implemented in P2b (2026-10-22, `src/compiler/frontend/c-primitives.npl`):
NPL has no identifier macros, so `define-c-primitive` is a top-level
declaration handled by the frontend rather than a macro over `%c-call`. It
registers a `fixed` row (with its types) for the current compilation and
binds the name to that row, so calls are direct and value uses share the P1
wrapper; import, rename, and prefix apply as to any binding. A symbol that
is already registered keeps its row and must agree on operand count. The
`fixed` rows stay in the table because import-free programs, including the
compiler, resolve through it (P2a). npli rejects these forms until P5 can
resolve symbols at run time. User documentation: [ffi](ffi.md).

A compiler core form calls a C function directly:

```scheme
(%c-call "npl_car" (pair) x)        ; NplVal npl_car(NplVal), synchronous
```

The middle list gives one type per argument (see "Types" below).

- Control conversion treats it like a `fixed` row: it is synchronous and
  receives no continuation.
- The backend emits `call $npl_car(l x)`, as `checked-call` does today.
- A `%c-procedure` form produces its first-class value. It reuses the
  wrapper emission in `primitive-values.npl`, keyed by symbol instead of
  table index.

Libraries then declare primitives with a macro over these forms:

```scheme
(define-c-primitive (car (p pair)) any)              ; symbol by rule: npl_car
(define-c-primitive (string-ref (s string) (k index)) char)
(define-c-primitive (symbol? (x any)) boolean "npl_symbol_predicate")
```

- `(npl primitive)` becomes an ordinary library made of these declarations.
  `PRIMITIVE-VALUE-SOURCE` and the 124 `fixed` rows go away.
- `PRIMITIVE-TABLE` keeps only what needs bespoke emission: inline
  arithmetic and comparison, `null?`/`pair?`/`eq?`/`not`, port-defaulting
  kinds, `count` varargs, `apply`, and `exit`.
- A new runtime primitive needs C plus one declaration line. No compiler
  change and no seed relay.
- Calls stay direct. Once `define-c-primitive` expands, a call site sees
  `%c-call`, so code quality matches today's `fixed` rows.

### Types

Declarations carry argument and result types from the start. They cost one
word per argument, and a later typed NPL needs them anyway.

- **Vocabulary**: a small fixed set that maps to existing runtime tag tests:
  `any`, `pair`, `fixnum`, `index` (non-negative fixnum), `integer`,
  `number`, `real`, `char`, `string`, `symbol`, `vector`, `bytevector`,
  `procedure`, `port`, `boolean`. Level B adds C types (see section 5).
- **What the compiler does with them now**: for `pair`, `fixnum`, `char`,
  and `index` (a tag test of a few instructions) it emits the check inline,
  and on success calls an unchecked entry or inlines the operation; on
  failure it calls the checked C function, so the error object and its text
  stay defined once, in C. Other types are documentation only for now; the C
  function checks them.
- **Result types** are recorded but not yet used. They are the hook for
  typed NPL and for removing redundant checks later.
- The C function keeps all its own checks. An inline check is an
  optimization, never the only check, so a wrong declaration cannot make a
  call unsafe; at worst it makes the fast path miss.
- The evaluator ignores types (it calls the checked native procedure).

### 2. Naming Rule

The rule turns a Scheme name into a C symbol:

1. A name already starting with `npl_` is used as is.
2. Otherwise, prefix `npl_`, then replace `->` with `_to_`, a trailing `?`
   with `_p`, drop `!`, and replace `-` with `_`.

The rule is written in NPL (frontend), not in C. Of the 24 current
exceptions, most can be renamed in `runtime/` to fit the rule, because the
current runtime is not frozen; only `bootstrap/runtime/` is. Renames that
read worse keep an explicit override, for example `display` ->
`npl_display_port`, whose port-defaulting row stays in the table anyway.

### 3. Evaluator compiles to native closures

P0 (worklog 2026-10-20 11:10) changed this section. A closure-compiling
evaluator built by nplc produces procedures that are ordinary native
closures, so native control works on them unchanged.

- The evaluator compiles the shared frontend's core AST to native closures,
  with variables resolved to lexical addresses at compile time and globals
  to cells. No string lookup at run time.
- Control operators are the native ones: `call/cc`, `dynamic-wind`,
  `with-exception-handler`, `raise`, `raise-continuable`, `apply`, `values`,
  parameters, and promises. A P0 prototype checked re-entering a `call/cc`
  continuation, escaping from native `map`, `raise-continuable`,
  `dynamic-wind` with `raise`, and catching a native primitive error.
  The evaluator's own continuation, handler, and wind frames go away.
- Primitive names bind to native procedure values, generated from the same
  declarations as the compiler, so names and error objects cannot diverge.
  The NPL reimplementations and the string `cond` dispatch are deleted.
- Prerequisite: runtime argument errors must be catchable. Today
  `numeric_error` (runtime/numeric.c) prints and exits, which native code
  avoids only because `(scheme base)` validates arguments first.

### 4. Compiled standard library in npli

npli currently interprets `lib/scheme/*.scm`, so `(scheme base)` `+` runs
`integer.scm` inside the evaluator (8x the import-free loop in P0).

- npli binds the value exports of the standard libraries to their compiled
  procedures and registers them as runtime libraries, as
  `frontend-register-runtime-library!` already does for
  `(npl eval-runtime)`. Higher-order library procedures (`map`,
  `for-each`, `sort`) are included: evaluator procedures are native
  closures.
- Syntax exports keep coming from the shared frontend.
- Remaining cost after the rewrite: native non-tail calls allocate a
  continuation closure per call. The P0 prototype ran a 20M-iteration loop
  in ~32 s user, about 40x native. Evaluator-side optimizations come first
  (variable and constant operands read directly instead of called); cheaper
  native non-tail calls would be a compiler architecture change, decided
  separately.

### 5. Foreign calls to C libraries

There are two levels, by the C signature of the target function.

**Level A: functions written against `npl.h`** (`NplVal` arguments and
result, checks inside).

- Compiler: works as soon as step 1 lands. nplc needs `-l`/`-L` link
  options passed through to the C compiler.
- npli: `dlopen`/`dlsym` lookup, then a fixed set of C trampolines for 0..8
  `NplVal` arguments. All such functions share one ABI, so no libffi is
  needed.
- An extension library calls back into the runtime (`npl_cons`,
  `npl_primitive_error`, ...) through the host executable's symbols.
  Verified 2026-10-19 with a host linked against `libnpl_runtime.a` and an
  extension that calls `npl_car` and `npl_number_add`:
  - macOS: works without extra flags (extension built with
    `-undefined dynamic_lookup`); `-Wl,-export_dynamic` changed nothing.
  - Linux (bookworm, clang): `dlopen` fails with `undefined symbol:
    npl_number_add` unless the host links with `-rdynamic`.
  - Both: only archive members the host already pulled in are visible. A
    host that did not use `npl_char_property` cannot provide it. npli
    already links all 260 runtime `npl_*` symbols today, but only by
    coincidence. So npli links its runtime with `-Wl,-force_load` (macOS) or
    `--whole-archive` (Linux), plus `-rdynamic` on Linux; both were checked
    to make every symbol resolvable.

**Level B: plain C signatures** (`int`, `double`, `char *`, pointers).

```scheme
(define-c-function sqlite-open "sqlite3_open" (string pointer-out) -> int)
```

- Compiler: QBE emits the native call directly, as in
  `call $sqlite3_open(l %p, l %q)`. The NPL side generates argument
  conversion and result boxing from the signature.
- npli: per-library generated stubs, no libffi (decided 2026-10-19). From
  a library's `define-c-function` declarations, the NPL toolchain generates
  a C stub file in which each C function becomes an `NplVal` function that
  converts its arguments, calls, and boxes its result. The stub is compiled
  into a shared library with the C library linked in. npli loads that stub
  library through the Level A path; nplc can either link the same stub or
  emit the calls directly.
- Following AGENTS.md, C holds only the call mechanism. Signature parsing,
  conversion policy, and error mapping are NPL.

## Verification

- After increment 0, every primitive moved out of the table keeps its
  emitted SSA: compiled
  test programs, and the compiler's own fixed point, are byte-identical
  before and after each move. That shows the move changed no behavior.
- Evaluator rebinding: a differential fixture runs each primitive's error
  and success cases under native and npli and requires identical output,
  including error-object messages.
- Foreign calls: a small C library in `tests/` covered by both levels,
  through nplc and (Level A) npli.

## Increments

0. Runtime argument errors raise catchable error objects (`numeric_error`
   and an audit of `exit(1)` sites).
1. Name primitive wrappers by C symbol (`_npl_prim_npl_car`) instead of
   their `PRIMITIVE-TABLE` index (`_npl_prim_N`,
   `backend/primitive-values.npl`). Moving rows out of the table would
   otherwise renumber every wrapper, and the SSA comparison could not show
   that a move changed nothing. Rows without a C symbol (`+`, `<`, `apply`,
   ...) stay in the table and need their own stable, collision-free name
   (sanitized names collide today, for example `+` and `-`). This step
   changes emitted SSA once; seed relay once.
2. Naming rule, typed `%c-call`/`%c-procedure` forms, and
   `define-c-primitive`. Move the `fixed` rows into typed `(npl primitive)`
   declarations and delete `PRIMITIVE-VALUE-SOURCE`. Types are recorded but
   unused, so emitted SSA stays unchanged; seed relay once.
3. Evaluator design (section 3), then the closure-compiling evaluator with
   native control, native primitives, and the compiled standard library
   (section 4); differential native/npli fixture.
4. Evaluator-side tuning until the bench workloads finish under npli.
5. Level A foreign calls: nplc link options, npli `dlopen` trampolines,
   npli linked with the whole runtime (`-force_load`, `--whole-archive`,
   `-rdynamic`). Done 2026-10-23: `nplc -l/-L`; `npli -l PATH` (chosen
   over a source-level library declaration) and `runtime/ffi.c`. User
   guide: [ffi](ffi.md).
6. Level B foreign calls: pointer type, typed `define-c-function`,
   generated per-library stubs (npli) and direct calls (nplc). Done
   2026-10-27 with the user's choices: a pointer box without finalizer
   (`with-resource` for release), `pointer-out` results as extra values,
   one stub file per program that nplc compiles and links itself and npli
   loads from `nplc --emit=c-stubs`; nplc calls the same stubs instead of
   emitting the C calls in SSA.
7. Optional: inline checks for `pair`, `fixnum`, `char`, and `index`
   arguments, gated on bench evidence. Renames of runtime exceptions to the
   naming rule fit any step after 2.

## Decisions

Recorded 2026-10-19.

- No new dependency: Level B in npli uses per-library generated stubs, not
  libffi.
- Declarations carry types now, with inline checks for the cheap tag tests
  only (see "Types"), so typed NPL can build on them later.
- Symbol export flags: verified as above; npli links its runtime whole and
  adds `-rdynamic` on Linux.

## Open Questions

Resolved 2026-10-24/25 with the user and done in increment 6:

- **Stub generation driver:** `nplc --emit=c-stubs`; an executable build
  generates and links the stubs itself. A package tool may wrap this later.
- **Pointer values:** `BOX_POINTER` (`runtime/ffi.c`), no finalizer;
  release is explicit, deterministic with `with-resource` (`(npl ffi)`).

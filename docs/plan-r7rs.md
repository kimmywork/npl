# R7RS-small Standard Library Plan

Status: in progress. Current progress and evidence live in
[workstate](../.agents/workstate.md); toolchain usage is in
[toolchain](toolchain.md).

## Goal

Implement the R7RS-small standard libraries ([reference](reference/r7rs.html))
so that native executables (`nplc`) and the evaluator (`npli`) show the same
observable behavior, checked against Guile 3 as the reference implementation.
Then measure performance against Guile.

`(scheme r5rs)` is built last, only from completed libraries.

## Current Gap

`python3 tools/r7rs_inventory.py --missing` reports export presence
(2026-10-10: 556/556, every R7RS-small library including `(scheme r5rs)`).
Presence is not conformance; behavior is established only by fixtures.

| Area | Gap |
|---|---|
| representation | strings and chars are bytes/ASCII; runtime `read` lacks datum labels and `write-shared` does not label cycles (L6) |
| semantics | `guard` clauses run before dynamic-wind after thunks unwind (R7RS 4.2.7); `(sqrt -1.0-0.0i)` branch needs a user decision |
| evaluator | does not reject mutation of vector literals; built-in numeric primitives used without imports keep their own error text |

## Rules

- One behavior, one contract: every increment adds or extends a shared fixture
  under `tests/r7rs/fixtures/` run by `tools/run_r7rs_fixture.sh` on native,
  `npli`, and Guile with identical stdout, stderr, and status.
- Fixture layout: `behavior.scm` holds the shared definitions and must be
  definitions only (native top level); `native.npl` includes it, `repl.in`
  holds the imports for the `repl-body` lane, and `guile.scm` wraps it in a
  private `define-library` to avoid Guile core-binding warnings. The fixture
  runner reuses built binaries; rebuild with `tools/build_toolchain.sh`.
- NPL first: library policy, argument checks, and errors in NPL; C only for
  runtime mechanisms. Reuse the shared frontend and evaluator; do not write a
  second semantics for the evaluator.
- Library-only changes need no relay. Compiler or runtime-ABI changes need the
  strict relay (`tools/update_current_ssa.sh`) and a test that fails on the
  previous seed.
- Differences with Guile are classified (`npl-bug`, `guile-extension`,
  `implementation-allowed`, `fixture-bug`, `unimplemented`,
  `oracle-unavailable`); the last two never count as passing.
- Permanent `bootstrap/bootstrap.ssa` and `bootstrap/compiler/` stay frozen.
- Each increment ends with `tools/run_tests.sh --skip-current-fixed-point`
  (plus the relay when applicable), a worklog entry, and a commit.

## Increments

Order follows dependencies; each is one commit. Runtime or compiler
prerequisites are delivered inside the increment that needs them (one relay
each). The three representation changes in L3-L5 were approved 2026-10-08.

0. **L0 values prerequisites** (done 2026-10-08): symbol interning and
   `string->symbol`; registered primitives as first-class values; native
   `apply`.
1. **L1 lists and equivalence** (done 2026-10-08). Pure NPL in `(scheme base)`: `map`,
   `for-each` (multi-list), `member`/`assoc` families (optional predicate),
   `list-tail`, `list-ref`, `list-copy`, `list-set!`, `make-list`, `equal?`
   (pairs, vectors, strings, bytevectors), `caar`...`cddr`, `boolean?`,
   `boolean=?`, `symbol=?`. Prerequisite: `procedure?` primitive.
2. **L2 vectors and bytevectors** (done 2026-10-08). Constructors, copies with optional ranges,
   fill, append, `vector-map`/`vector-for-each`, list/vector conversion
   (`vector->string`/`string->vector`/`utf8` conversions move to L3). Library
   only (constructors via `list->vector` and `apply bytevector`); allocation
   primitives only if measured necessary.
3. **L3 strings and chars (ASCII)** (done 2026-10-08). Prerequisites: `string-ref` returns a
   char (the compiler's own byte uses move to a private byte primitive; seed
   transition), `make-string`/`string-set!`/`string-fill!` primitives with the
   literal-immutability check. Then constructors, copies, ranges, comparisons,
   `string-map`/`string-for-each`, conversions (including `vector->string`,
   `string->vector`, `utf8->string`, `string->utf8`), `(scheme char)` string case and
   `string-ci` procedures. Unicode stays out of scope until L6.
4. **L4 records, errors, and primitive errors.** Prerequisite: a record box
   type disjoint from vectors (R7RS 3.2). Then `define-record-type`, `error`
   and error-object accessors built on it, `file-error?`/`read-error?` with
   runtime open/read failures reported to the library, and catchable primitive
   errors (section "Primitive Errors"). Done 2026-10-10.
5. **L5 I/O and remaining libraries.** Prerequisites: current ports as
   parameter objects (R7RS 6.13.1; needed by `with-output-to-file` and
   `parameterize`), a monotonic/wall clock primitive, and a readiness poll for
   `char-ready?`/`u8-ready?`. Then `read-line`, `read-string`, `write-string`,
   `with-input-from-file`, `with-output-to-file`, `features`, `(scheme time)`,
   `(scheme complex)`. Done 2026-10-10.
6. **L6 representation work** (each needs its own design review before code):
   Unicode strings and chars, datum labels in runtime `read` and
   `write-shared`. Design: [design-l6](design-l6.md) (accepted 2026-10-10).
7. **L7 `(scheme r5rs)`**, re-exported from the completed libraries plus
   `null-environment` and `scheme-report-environment`. Done 2026-10-10.
8. **P1 Guile performance baseline.** Fixed environment, warm-up, at least 10
   runs, median and spread, native and `npli` reported separately, compile time
   separated from run time; only workloads whose fixtures pass three ways.

## Primitive Errors

Decision (2026-10-08). Model: Racket, Guile, and Chez all raise a catchable
condition when a safe primitive receives a bad argument (Racket
`exn:fail:contract`, Guile a compound exception that is an R7RS error object
with message and irritants, Chez an `&assertion` condition with `&who`,
`&message`, `&irritants`). Uncaught, the condition reaches the default handler,
which prints and exits. Only explicitly unsafe code (Chez optimize-level 3,
Racket `unsafe-` operations) skips the check.

NPL target:

- A safe primitive's type or range error raises an R7RS error object (L4
  record type) with `who`, a message, and irritants; `guard` and
  `with-exception-handler` catch it on native and npli alike, and the message
  and irritants agree between the two.
- The C primitive keeps its own check. On failure it calls one runtime entry,
  `npl_raise_primitive_error(who, message, irritants)`, which builds the error
  object and raises it non-continuably through the current handler chain. The
  primitive's continuation is not needed: after a non-continuable raise,
  control never returns to the primitive's caller.
- Mechanism: the runtime unwinds the C stack to the innermost trampoline with
  a `longjmp` (the existing escape continuation and reader recovery use the
  same technique) and the trampoline then tail-calls the handler. The
  trampoline records the GC root count on entry and the unwind restores it,
  because primitives that rooted stack slots never reach their unroot. With no
  handler installed, the error is printed as today and the process exits.
- Inline-emitted checks (`car`/`cdr` on non-pairs, numeric fast paths) route
  their failure through the same entry.
- Diagnostics use one wording for native and npli: `who: message` plus
  irritants, e.g. `car: expected a pair: 1`.
- No unsafe mode is introduced now.

Verification: a shared fixture that catches each primitive family's errors
with `guard`, checks `error-object?`, `error-object-message`, and
`error-object-irritants`, and checks that an uncaught error still exits nonzero
with the diagnostic on stderr. Message text is compared only between native
and npli; Guile's wording differs (`implementation-allowed`).

Scope: delivered with L4, because it depends on the error-object type.

A stop is required, with evidence and a request for review, when an increment
needs a representation change, a new compiler primitive outside its scope, an
unclassifiable Guile difference, or a change to the permanent bootstrap.

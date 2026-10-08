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
(2026-10-08: 244/335 names excluding `r5rs`). Presence is not conformance;
behavior is established only by fixtures.

| Area | Gap |
|---|---|
| `(scheme base)` lists | `map for-each apply member memq memv assoc assq assv list-ref list-tail list-copy list-set! make-list equal? caar cadr cdar cddr` |
| `(scheme base)` strings, chars | `string make-string string-set! string-copy string-copy! string-fill! string->list list->string string->symbol string->vector vector->string string-map string-for-each string<? ...`, `char<? ...`, `symbol=? boolean? boolean=? procedure?` |
| `(scheme base)` vectors, bytevectors | `make-vector vector->list vector-copy vector-copy! vector-fill! vector-append vector-map vector-for-each make-bytevector bytevector-copy bytevector-copy! bytevector-append utf8->string string->utf8` |
| records, errors | `define-record-type`, `error error-object? error-object-message error-object-irritants read-error? file-error?`, `features` |
| I/O | `read-line read-string write-string char-ready? u8-ready?`; `(scheme file)` `with-input-from-file with-output-to-file` |
| other libraries | `(scheme char)` string case and `string-ci` procedures; `(scheme complex)` absent; `(scheme time)` absent |
| semantics | strings and chars are bytes/ASCII; runtime `read` lacks datum labels; native primitive type errors terminate instead of raising; evaluator lacks ~40 native primitives; `(sqrt -1.0-0.0i)` branch needs a user decision |

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

Order follows dependencies; each is one commit.

0. **L0 values prerequisites** (done 2026-10-08).
   L0a symbol interning and `string->symbol`; L0b registered primitives usable
   as first-class values in native code, via wrappers generated from
   `src/compiler/backend/primitives.npl`; L0c native `apply` as a tail-call
   primitive. Each with a test that fails on the previous seed.
1. **L1 lists and equivalence.** Pure NPL in `(scheme base)`: `map`,
   `for-each` (multi-list), `apply`, `member`/`assoc` families (with optional
   predicate), `list-tail`, `list-ref`, `list-copy`, `list-set!`, `make-list`,
   `equal?` (pairs, vectors, strings, bytevectors), `caar`...`cddr`,
   `boolean?`, `boolean=?`, `procedure?`, `symbol=?`.
2. **L2 vectors and bytevectors.** Constructors, copies with optional ranges,
   fill, append, `vector-map`/`vector-for-each`, list/vector conversion.
3. **L3 strings and chars (ASCII).** Constructors, mutation, copies, ranges,
   comparisons, `string-map`/`string-for-each`, conversions, `string->symbol`,
   `(scheme char)` string case and `string-ci` procedures. Unicode stays out of
   scope until L6.
4. **L4 errors and records.** `error` and error-object accessors, `read-error?`,
   `file-error?`, catchable errors from library procedures, then
   `define-record-type`.
5. **L5 I/O and remaining libraries.** `read-line`, `read-string`,
   `write-string`, `char-ready?`, `u8-ready?`, `with-input-from-file`,
   `with-output-to-file`, `features`, `(scheme time)`, `(scheme complex)`.
6. **L6 representation work** (each needs its own design review before code):
   Unicode strings and chars, datum labels in runtime `read` and
   `write-shared`, catchable native primitive errors.
7. **L7 `(scheme r5rs)`**, built from the completed libraries.
8. **P1 Guile performance baseline.** Fixed environment, warm-up, at least 10
   runs, median and spread, native and `npli` reported separately, compile time
   separated from run time; only workloads whose fixtures pass three ways.

A stop is required, with evidence and a request for review, when an increment
needs a representation change, a new compiler primitive outside its scope, an
unclassifiable Guile difference, or a change to the permanent bootstrap.

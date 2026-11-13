# SRFI I2/I3 review test design (2026-11-12)

Status: findings 1-2 and the priority list are fixed. The reserved-tag
limitation for rank-n literals is accepted and documented: `#nA` literals
are still vectors with a reserved tag, and a user vector that matches the
tag/shape/data layout is treated as an array. `make-array` returns opaque
records, so constructed arrays are not `vector?`. The suites live in
`src/compiler/tests/srfi-review-*.npl`, the import cases in
`src/compiler/tests/imports/`, and the differential fixture in
`tests/r7rs/fixtures/srfi-i2-i3/`.

Scope: `feature/srfi-i2-i3` against `master` (SRFI 13, 130, 140, 133, 41,
47, 158, 161, 195, the SRFI 111 box change, the `(scheme base)`
`vector-copy` change, and `#nA` literals in both readers), plus the libraries
merged by PR #2 (`release/0.1.2`): SRFI 1, 111, 189, 196, and 125.

Expected values come from the SRFI texts in `docs/reference/`, not from
either NPL surface. Where an SRFI leaves behavior to the implementation, the
case either checks only the guaranteed part or is left out.

## Layout and running

| Path | Purpose |
| --- | --- |
| `tools/check_full_compiler.sh` | Runs the four review suites plus the compile/reject import cases. |
| `src/compiler/tests/review-harness.scm` | `check`, `check-true`, `check-error`. Each case runs under an exception handler and reports every failure, not just the first. |
| `src/compiler/tests/srfi-review-strings.npl` | `S13-*`, `SC-*` (SRFI 130), `S140-*` |
| `src/compiler/tests/srfi-review-vectors-arrays.npl` | `VB-*` (base `vector-copy`), `V133-*`, `A47-*` (library and both readers) |
| `src/compiler/tests/srfi-review-lazy-data.npl` | `ST-*` (SRFI 41), `G158-*`, `BX-*` (SRFI 111/195), `U161-*` |
| `src/compiler/tests/srfi-review-foundation.npl` | PR #2: `L1-*`, `O189-*`, `R196-*`, `H125-*` |
| `src/compiler/tests/imports/*.npl` | Import-set cases. Must compile; `*-reject` cases must be rejected. |
| `tests/r7rs/fixtures/srfi-i2-i3/` | Differential fixture: nplc and `npli` must both produce `expected.out`. |

```sh
tools/check_full_compiler.sh             # includes the four review suites
tools/run_r7rs_fixture.sh tests/r7rs/fixtures/srfi-i2-i3
```

## Why these cases

The suites already on the branch mostly check one happy-path call per
procedure. These cases focus on the gaps:

1. **Optional arguments and ranges.** Every `[start end]`, `[final end]`, fill,
   padding, and multi-sequence form, using the SRFI's own examples where
   there are any (S13-21/22/32, V133-13, L1-18..43).
2. **Error contracts.** Bad ranges, invalid criteria, exhausted inputs, and
   wrong arity (S13-50..55, VB-05..08, A47-10..20, BX-05/06, H125-01/06/11/19).
3. **Return types between related SRFIs.** SRFI 130 returns cursors, SRFI 140
   returns indexes or `#f`, and SRFI 140 results must be istrings while their
   arguments stay mutable (SC-01..03, S140-04..07, S140-10..13).
4. **Representation boundaries.** `#nA` literals versus ordinary vectors,
   both readers giving the same datum, datum labels next to array prefixes, and
   very large ranks on untrusted `read` input (A47-01..16, A47-29/30).
5. **Laziness and stack depth.** Memoization, not forcing earlier heads, and
   long streams, folds, and coroutine generators (ST-15..18, G158-07, L1-52/53).
6. **Coexisting imports.** README says canonical libraries can be imported
   with `(scheme base)`. A module that imports both only compiles if
   re-exports keep their binding identity. Comparing procedures with `eq?`
   cannot test this, because a global procedure reference in compiled code is
   not `eq?` to itself.
7. **Both execution paths.** No new library had compiler/evaluator coverage.
   The fixture runs every I2/I3 library, including a `#2A` source literal
   read by the C reader in `npli`, through nplc and `npli`.

## Results after the fixes

```text
PASS review-foundation 119
PASS review-lazy-data 60
PASS review-strings 100
PASS review-vectors-arrays 69
imports: all compile/reject cases as expected
fixture srfi-i2-i3: native PASS, repl PASS, guile SKIP (oracle=none)
```

## KNOWN-FAILURES (fixed, kept for history)

Each case reproduced a review finding; all are fixed and the table is
retained for traceability.

| Case IDs | Finding | Severity |
| --- | --- | --- |
| VB-05, VB-06 | `(scheme base)` `vector-copy` no longer rejects `end`/`start` past the length; it pads with `#f`. This is an R7RS regression for every program, not only SRFI 133 users. | high |
| A47-14 | `#<huge>A()` allocates a `rank`-long shape. The C reader accepted rank 10^8 (`read` on untrusted input). The compiler reader did not finish on `#50000000A()` within 60 s. | medium |
| A47-01, A47-02, A47-09 | Fixed: `#1A` is an ordinary vector and `make-array` returns a record, not a vector. |
| A47-08 | Accepted limitation: rank-n literals use the reserved tag `npl-array-literal-5f8d`; a user vector matching the tag/shape/data layout is treated as an array. README documents the reservation. | low |
| S13-01 | `string-concatenate-reverse` passes `end` as the start index of `final`: `'("c" "b") "xyz" 2` gives `"bcz"`, not `"bcxy"`. | medium |
| G158-05 | `reverse-vector->generator` with `start` indexes from `end-1-at` with `at` starting at `start`: `#(a b c d) 1 3` gives `(b a)`, not `(c b)`. | medium |
| S140-12 | `string-contains-right` returns a SRFI 130 cursor record. SRFI 140 requires an index or `#f`. | medium |
| imports/srfi-140-spec-names | `(srfi 140)` lacks `string-prefix?`, `string-suffix?`, `string-prefix-length`, and `string-suffix-length`, which SRFI 140 specifies. `string-append!` and `string-replace!` (as in SRFI 118) are also absent. | medium |
| A47-15 | The shared digit loop caps datum labels at 2^30 with the message "array rank too large". `master` accepted labels up to about 2^40. | low |
| S140-08 | `string-repeat` rejects a character. SRFI 140 says `string-or-character`. | low |
| S13-04 | `(string-hash s 0)` is an error. SRFI 13 says a bound of 0 means "use the default". | low |
| S140-20 | `string->utf16` always writes a big-endian BOM. SRFI 140 says the endianness should match `(features)`. | low |
| A47-18 | `(array-ref (vector 1 2))` fails with "cdr: expected a pair". | low |
| imports/scheme-char-and-srfi-13 | `(scheme char)` with `(srfi 13)` conflicts on `string-upcase`/`string-downcase`. Needs a decision: document it next to the SRFI 13/130 note, or accept it. | decision |

## Observations

- Fixed: `lib/npl/vector.sld` now documents that `vector-copy` is shared
  with `(scheme base)` and that SRFI 133 fill is not provided.
- Fixed: the `unifiable-box.sld` header describes union by rank.
- Fixed: the unused `string-copy-range` helper was removed.
- Fixed: the plan's case counts now match the suites (29/24).
- Still true: in compiled code, an arity error ends the process and cannot
  be caught by `with-exception-handler`; this is master behavior.

## Moving cases into the gate

Done:

1. The suites are `src/compiler/tests/srfi-review-*.npl`; their counts are
   registered in `check_full_compiler.sh`.
2. The fixture is `tests/r7rs/fixtures/srfi-i2-i3` and is in
   `tools/run_tests.sh`.
3. The import cases are in `src/compiler/tests/imports/` and are compile or
   reject steps in `check_full_compiler.sh`.

A follow-up review found that `cond-expand` reported both endianness
features. The compiler now derives them from the host through
`npl_little_endian_p`, matching `(features)`. `utf16->string` without a BOM
also uses the host byte order, with regression case S140-20b.

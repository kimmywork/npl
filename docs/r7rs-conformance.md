# R7RS-small Conformance

Status: frozen at the language milestone, 2026-11-12. This document states
what NPL claims about R7RS-small and the evidence for each claim. It replaces
the "Current Gap" table of the archived plan
(`.agents/archive/plan-r7rs.md`).

## Claim

**NPL is an R7RS-small implementation: every standard library is present and
behaviorally checked.** It does not claim R7RS *large* (R6RS-style
libraries, `syntax-case`, the numeric tower beyond R7RS-small).

## Evidence

### Export coverage: 556/556

`python3 tools/r7rs_inventory.py` checks every name R7RS-small Appendix A
gives each standard library against the `export` list of `lib/scheme/*.sld`:

| Library | Names | Library | Names |
|---|---:|---|---:|
| `(scheme base)` | 238/238 | `(scheme load)` | 1/1 |
| `(scheme r5rs)` | 221/221 | `(scheme process-context)` | 5/5 |
| `(scheme cxr)` | 24/24 | `(scheme read)` | 1/1 |
| `(scheme char)` | 22/22 | `(scheme repl)` | 1/1 |
| `(scheme inexact)` | 12/12 | `(scheme time)` | 3/3 |
| `(scheme file)` | 10/10 | `(scheme write)` | 4/4 |
| `(scheme complex)` | 6/6 | `(scheme case-lambda)` | 1/1 |
| `(scheme lazy)` | 5/5 | `(scheme eval)` | 2/2 |

Presence is not behavior; the two checks below establish behavior.

### Feature identifiers

`(features)` advertises the standard identifiers the implementation
satisfies: `r7rs`, `npl`, `full-unicode`, `ratios`, `exact-closed`,
`exact-complex`, `ieee-float`, `posix`, and `unix`. `cond-expand` accepts
the same identifiers, and `(library ...)` is true for an installed
`(scheme ...)`, `(srfi ...)`, or user library whose file exists, even before
it is loaded.

### External conformance suite: 1208 passed, 4 failed, 10 aborted

`tools/run_chibi_suite.sh --native` runs chibi-scheme's independent
`tests/r7rs-tests.scm` on native `nplc` output (and `tools/run_chibi_suite.sh`
on npli); both give the same result. The four failures, each classified and
accepted:

| Test | Expected | NPL | Classification |
|---|---|---|---|
| `(sqrt -1.0-0.0i)` | `0.0+1.0i` | `0.0-1.0i` | open decision, see below |
| `|+NaN.0abc|` round trip | `"|+NaN.0abc|"` | `"+NaN.0abc"` | implementation-allowed |
| `1.7976931348623157e308` member | `"1.7976931348623157e+308"` | `"...e308"` | implementation-allowed |
| `-1.7976931348623157e308` member | `"-1.7976931348623157e+308"` | `"...e308"` | implementation-allowed |

- The two `e308` entries are the same issue: chibi's list of accepted
  spellings contains only the `+`-exponent form, and R7RS number *output* is
  implementation-dependent in the exponent sign; NPL and Guile both print
  `e308`, and the value reads back correctly.
- `|+NaN.0abc|`: R7RS 7.1.1 makes a `|...|` symbol print unbarred only when it
  cannot be read as anything else; `+NaN.0abc` is not a number, so NPL prints
  it unbarred, as Guile and Racket do. chibi's expectation is its own choice.
- The 10 aborted forms are the R5RS exponent markers `s f d l`, which are not
  R7RS syntax.

### Three-way fixtures: 41 in the gate

Every behavior the project commits to has a fixture under
`tests/r7rs/fixtures/` run by `tools/run_r7rs_fixture.sh` through native,
npli, and Guile 3 with byte-identical stdout, stderr, and status. Fixtures
with `oracle=none` (Guile lacks the behavior: datum labels in `read`,
`record-update`, cyclic printing, `match-define`) are checked against
outputs derived from R7RS and hand-verified, recorded in each fixture's
`behavior.scm`. See [testing](testing.md) for the contract and the list.

Not all fixture directories are in the gate: `core-bindings` runs only inside
`tools/check_core_consistency.sh`, and `smoke`, `integer-io`, `port-wrappers`
are manual. That is recorded in [testing](testing.md#fixtures-outside-the-gate).

## Known deviations

Open, all non-blocking:

1. **Operand evaluation order differs between native and npli.** In
   `(list (vector-ref v 0) (f))` where `f` mutates `v`, native sees the new
   value and npli (like Guile) the old one. R7RS leaves the order
   unspecified; the project's own rule is that the two paths agree, so this
   is a consistency gap, not a conformance gap.
2. **`(sqrt -1.0-0.0i)`.** NPL follows the IEEE signed-zero branch
   (`0.0-1.0i`), as Guile and Racket do; chibi's test expects the other
   branch. Changing it would break agreement with Guile, so this stays open
   until a decision is made. It is not a defect against R7RS, which does not
   specify the branch.
3. **Guile 3.0 oracle gaps.** Guile 3.0 does not make a failed
   `open-input-file` a `file-error?` object, and reports absent irritants as
   `#f`. The native suite `error-objects.npl` covers these instead; fixtures
   do not depend on Guile for them.

Two compiler limits that are *not* R7RS deviations but are worth knowing:

- A call whose head is a literal number, such as `(2 3)` or `(list (2 3))`,
  is rejected with `control conversion: unsupported core form` rather than
  raising at run time. R7RS lets an implementation reject it, but the message
  is a compiler internal.
- The npli `read`/`write` paths and the compiler reader share one datum
  reader; `tools/check_core_consistency.sh` and the `core-bindings` fixture
  guard the shared behavior.

## Deliberately outside the language stage

These are **not** R7RS-small and are not implemented. Each is a decision, not
an omission:

| Area | Status | Route, if wanted |
|---|---|---|
| `syntax-case` / procedural macros | deferred by the user | R6RS; needs compile-time evaluation in the compiler |
| Object system, classes, generic methods | absent | no mainstream SRFI; SRFI 20 is dormant; NPL route is records + dispatch (see [extending](extending.md)) |
| Keyword arguments (`#:key`) | absent | SRFI 88 (keyword objects), SRFI 89 (named/optional parameters), SRFI 177 (portable keyword arguments) |
| Default-valued optional parameters | absent (rest args and `case-lambda` exist) | SRFI 89, or `case-lambda` |
| Records: inheritance, `#:guard`, opaque, prefab | absent (all records transparent, generative) | SRFI 99 / SRFI 256 |
| `filter`, `fold`, `partition` | **implemented** in `(npl list)` / `(srfi 1)`, not `(scheme base)`; `sort` is still absent | SRFI 1 / SRFI 132 for `sort` |
| `string-split`, `string-join`, `string-trim` | absent | SRFI 13 / SRFI 152 |
| Sets | absent | SRFI 113 |
| Boxes, ranges, Maybe/Either | **implemented** | `(npl data box)` / SRFI 111, `(npl data range)` / SRFI 196, `(npl data maybe)` and `(npl data either)` / SRFI 189 |
| Hash tables | **implemented** | `(npl hashtable)`, `(srfi 125)` |
| `match`, `match-define` | **implemented** as `(npl match)` and import-free | SRFI 200 pattern language |
| `pretty-print` | absent | R6RS / implementation extension |

Adding any of these follows [extending](extending.md); most need no compiler
change and no seed relay.

## What "frozen" means

The language stage is closed at this claim: export coverage, the external
suite result, and the three-way fixtures above. New library work (the SRFI
rows) is ordinary library work and does not reopen it. Changing any behavior
listed under *Known deviations* requires updating this document and the
fixture that guards it.

# Workstate

## 0.1.2 (planning, 2026-11-12)

Plan: [0.1.2-srfi](plans/0.1.2-srfi.md). Scope is the SRFI library train
(requested SRFI-1/13/130/140/133/19/170 plus the data-structure SRFIs) with
canonical `(npl ...)` library names and `(srfi N)` aliases. Libraries are
imported individually; there is no aggregate import syntax. Remaining
diagnostics (D1) run in parallel; SRFI-18/230 and full SRFI-226 move to
0.1.3 after a runtime/control design.

I0b is implemented on `release/0.1.2`: `(npl primitive)` -> `(npl internal
primitive)`, `(npl frontend)`/`(npl repl-io)` -> `(npl internal ...)`, and
the evaluator/eval-runtime -> `(scheme internal ...)`. `(npl internal *)`
now has no `(scheme base)` dependency; `(scheme *)` may import only the
internal core and scheme-internal services.

Hash tables are canonical as `(npl hashtable)`; `(srfi 125)` is an alias and
the standalone `(srfi 69)` library/fixture is removed.

I1 is implemented on `release/0.1.2`: `(npl list)` / `(srfi 1)`,

`(npl data box)` / `(srfi 111)`, `(npl data range)` / `(srfi 196)`, and
`(npl data maybe)` / `(npl data either)` / `(npl data option)` /
`(srfi 189)`. The new compiler suites and the `srfi-core` native/npli fixture pass;
`tools/run_tests.sh --skip-current-fixed-point` is 56/56 PASS; the
implemented SRFI references are committed. PR #2 merged at `c05dd68`.

I2 and part of I3 are implemented on `feature/srfi-i2-i3`:

- I2: `(npl string)` / 13, `(npl string cursor)` / 130,
  `(npl string immutable)` / 140.
- I3: `(npl vector)` / 133, `(npl data stream)` / 41,
  `(npl data generator)` / 158, `(npl data unifiable-box)` / 161,
  `(npl data multiple-value-box)` / 195, `(npl data array)` / 47.
- SRFI 111 boxes now store a value list so SRFI 195 shares its bindings.
- SRFI 47 adds `#2A`/`#nA` array literals to both readers: the NPL compiler
  reader and the C runtime reader. Rank-1 literals read as ordinary vectors;
  rank-n literals use a reserved tagged flat vector; `make-array`, shared
  arrays, and rank-0/prototype values use opaque records.
- The seed was relayed for the reader change.
- Review (2026-11-12): all findings are fixed. The spec-derived suites,
  import cases, and native/npli fixture were merged into
  `src/compiler/tests/` and `tests/r7rs/fixtures/srfi-i2-i3`; the design note
  is `.agents/reviews/2026-11-12-srfi-i2-i3-test-design.md`.

Remaining I3: SRFI 113 (with SRFI 128 comparators), 44, 146, 178.
`check_full_compiler.sh` covers 43 suites.

## 0.1.1 (2026-11-12)

Release-hardening work on branch `release/0.1.1`, tracked by
[2026-11-12-0.1.0-state-review](reviews/2026-11-12-0.1.0-state-review.md):
MIT `LICENSE`, root `README.md` with a GitHub Pages `docs/README.md`,
Linux-only CI, `nplc`/`npli --version`, a locale-independent gate, compiler
diagnostics on
standard error without the partial-SSA relay, standard feature identifiers,
`cond-expand` detection of installed `(srfi ...)` and user libraries, and
synchronized test/version documentation. No language semantics changed.
PR: https://github.com/kimmywork/npl/pull/1.

The new seed is 263,408 lines; the strict relay passes. The macOS gate with
`tools/run_tests.sh --skip-current-fixed-point` and the Linux gate with
`tools/check_linux.sh --skip-current-fixed-point` are each 56/56 PASS. The
GitHub Actions Linux gate also passes on Ubuntu 24.04; strict glibc C99
builds define `_DEFAULT_SOURCE`, and the `strings` fixture marks the Guile
3.0.8/3.0.9 `string-for-each` defect. The review's remaining P1/P2
findings (source spans and stack traces,
uncaught-error exit status, SRFI 1/13/113, separate compilation,
performance) are not part of this branch.

## Milestone 0.1.0 (2026-11-12)

The language stage is closed: R7RS-small conformance claim and evidence in
[r7rs-conformance](../docs/r7rs-conformance.md), the gate in
[testing](../docs/testing.md), extension paths in
[extending](../docs/extending.md), release notes in
[CHANGELOG](../CHANGELOG.md). Completed designs and plans are under
[archive](archive/). Everything below is the history that led here; its
"Next" pointers are superseded by this section. Nothing is scheduled after
the milestone; candidate next work (SRFI 1/13 libraries, the open items
below) waits for the user.

## Current (history up to the milestone)

Standard library stage per [plan-r7rs](archive/plan-r7rs.md). L0-L3 and L4a
(records, error objects, file/read errors) are done. L4b (catchable
primitive errors, plan section "Primitive Errors") is done: runtime value,
port, and file errors and library argument checks raise error objects.
L5 is done (current ports as parameter objects, text I/O, with-*-file,
features, (scheme time), (scheme complex)). L7 (scheme r5rs) is done; all
R7RS-small libraries export their full name lists (556/556). Remaining: L6
(Unicode strings and chars, datum labels): design
[design-l6](archive/design/design-l6.md) accepted; L6a+b (Unicode strings,
characters, ports, read/write syntax) and L6c (`(scheme char)` over Unicode)
done, L6d (datum labels in read) and L6e (cycle-aware output, terminating
equal?) done: L6 is complete. External check: chibi's r7rs-tests.scm via
`tools/run_chibi_suite.sh [--native]`: npli and native both 1207 passed,
4 failed, all classified as not NPL bugs (worklogs 2026-10-12, -13).
Next: P1 (Guile performance baseline); include compile time of large
generated programs (inline guard expansion was very slow). The toolchain stage is complete and committed
(`b8508d1`): unified `nplc` seed, `npli`, install layout, Linux parity.

## Performance Baseline (P1)

Harness is in place: `tools/bench.py` + `bench/workloads/*.scm` — 9
R7RS-small workloads under native, npli, Guile, and Racket, with compile
time, median/min-max wall and CPU time, timeouts, and a cross-implementation
stdout check. First pass (host load ~9, not a baseline): native is 1.4x
slower than Guile only on `fact` and 42-140x slower on the rest; npli exceeds
15 s on every workload; no output mismatches.

Root cause confirmed: with `(import (scheme base))`, `=`, `+`, `-`, `<`
became calls into the generic varargs NPL procedures in `integer.scm`. Native
fixnum fast paths for `+ - * = < <= > >=` are done (worklog 2026-10-19):
loop 53.6 s -> 1.14 s, fib 10.5 -> 1.11 s, tak 5.46 -> 0.52 s; the slow path
uses `npl_call_sync` (runtime/trampoline.c). Bench after it (`--runs 3`,
host load ~7, `ae41438`): native vs Guile wall is 0.8x on fact, 2.6x tak,
5.1x loop, 6.6x closures, 8.1x fib, 8.4x vectors, 9.3x lists and callcc,
16x strings; native compile is 3.7-6.9 s (Guile 0.3-0.6 s); npli still
exceeds 30 s on every workload. npli cost is structural (string-keyed
environment lookup, string `cond` primitive dispatch, library numeric
procedures interpreted), not only numeric. Allocator free-bin bitmap
(worklog 2026-10-19 15:40) cut strings/lists/callcc user CPU 3-4x; a
per-chunk GC sweep (19:10) cut compiler self-emit to ~23 s wall at the
default threshold. The 1 GiB default threshold is now a CPU/memory
trade-off (worklog 19:45; 256 MiB: 4x less memory, ~10% more self-emit CPU),
pending a user decision. Design for shared primitive declarations and C
foreign calls: `.agents/archive/design/design-primitives.md` (accepted
2026-10-20, implemented; the user-facing guide is [ffi](../docs/ffi.md)). Bench after these changes (worklog 19:50): native 1.1-6.1x
Guile wall, compile 2.3-3.5 s; npli > 30 s everywhere.

## Agreed Plan (2026-10-20)

Scope: shared primitive declarations and C foreign calls
([design-primitives](archive/design/design-primitives.md)), an npli evaluator
rewrite, and a GC nursery. npli is the priority. Revised after P0 (worklog
2026-10-20 11:10), user-accepted.

- P0 done: npli cost breakdown (measurement only).
- G1 write barrier in runtime mutators (~40 sites; compiled code mutates the
  heap only through runtime calls) plus `NPL_GC_VERIFY`, which checks that
  every old-to-young pointer is remembered at each full collection. Full
  suite under verify mode; emitted SSA unchanged.
- G2 non-moving nursery (conservative stack scan rules out copying): young
  objects marked in place, minor collection from roots plus remembered set,
  in-place promotion, full collection when the old generation doubles;
  `NPL_GC_NURSERY_BYTES`. Full suite with a 64 KiB nursery under verify,
  ASan gc_stress, identical output and SSA, relay.
- G3 defaults: self-emit and bench CPU <= 105% of the 1 GiB threshold, peak
  RSS <= the 256 MiB threshold (self-emit ~0.72 GB).
- R0 runtime argument errors raise catchable error objects: the 9
  `numeric_error` sites, and an audit of `exit(1)` sites separating argument
  errors from internal invariant failures. Fixture: each class caught by
  `guard` under native.
- P1 wrapper symbols by C symbol, not table index; SSA equal after symbol
  renaming; relay.
- P2 (revised 2026-10-22: import-free programs, including the compiler,
  use the table directly) P2a: one shared table in the frontend with
  rule-derived C symbols and generated `(npl primitive)` exports; SSA
  byte-identical; relay. P2b: `%c-call`/`%c-procedure` and typed
  `define-c-primitive` so libraries declare new primitives without a relay.
- E1 evaluator design: compile the shared frontend's core AST to native
  closures, lexical addresses, native control operators and higher-order
  procedures, native primitives from P2, compiled standard library (absorbs
  the former P3 and P4). For user review.
- E2 implement E1; differential native/npli fixture incl. error messages;
  chibi npli 1207 passed; imported loop within 1.5x of the import-free loop.
- E3 evaluator-side tuning (direct operand reads) until all 9 bench
  workloads finish under npli within 30 s. If native non-tail call cost
  still blocks it, a compiler change is decided separately with the user.
- P5 FFI level A (nplc `-l`/`-L`; npli whole-runtime link, `dlopen`,
  fixed-arity trampolines); C extension test on macOS and Linux.
- P6 FFI level B (opaque pointer, `define-c-function`, generated stubs);
  stub driver and pointer representation decided first.
- P7 (revised 2026-10-27) replaced by O1 inline fixnum arithmetic, O2
  direct self tail calls, O3 call-convention design for user review; see
  [handoff-performance](archive/handoff-performance.md) (archived, completed).

G1 done (worklog 2026-10-20 13:40). G2 done (17:30). G3 done (worklog
2026-10-21 08:00): defaults 1 MiB nursery and 128 MiB full threshold;
self-emit ~80% of nursery-off CPU at 0.27 GB peak. R0 done (worklog
2026-10-21 10:30). P1 done (14:50). P2a done (worklog
2026-10-22 10:50): one shared primitive table, rule-derived C symbols,
generated `(npl primitive)` exports. P2b done (worklog 2026-10-22
13:30): `define-c-primitive`, `%c-call`, `%c-procedure` register rows per
compilation; npli resolves them since P5b. FFI user guide `docs/ffi.md`. E1 done:
`.agents/archive/design/design-evaluator.md` accepted 2026-10-22 with D1 runtime unspecified
immediate, D2 `%library-definitions`, D3 native REPL re-entry. Increments
now U0 (unspecified value), E2a, E2b, E3 as listed there. U0 done (worklog
2026-10-22 17:40). E2a done (worklog 2026-10-23 11:40). E2b done (worklog
2026-10-23 12:50): imported loop 1.02x the import-free one. E3 done
(worklog 2026-10-23 13:30): all 9 bench workloads under npli in 30 s
(slowest loop 18.4 s). Remaining npli cost is native non-tail call
allocation; a cheaper call convention is a separate compiler decision.
P5 (FFI level A) done: P5a nplc -l/-L (14:20), P5b npli -l with
runtime/ffi.c (15:10), Linux gate fixes (16:20), Guile 3.0.8 defect
marker (16:50). macOS and Linux gates all PASS.
guard/exit dynamic extent fixed (worklog 2026-10-25), prerequisite for
deterministic release. Next: P6 (FFI level B). Decided 2026-10-24/25:
nplc builds stubs automatically, `nplc --emit=c-stubs` for npli; pointer
is a new box type without finalizer (explicit release); nplc links the
same stubs as npli; output parameters return as extra values;
`with-resource` (dynamic-wind macro) ships with the P6 library.
nplc runs the program's top level in order and generates the C main;
`main` is an ordinary name (worklog 2026-10-26). P6a done: BOX_POINTER,
(npl ffi) with with-resource, installed include/npl/npl.h. Result type
`values` (worklog 2026-10-26). P6b done (worklog 2026-10-27):
define-c-function with generated C stubs, built by nplc itself and by
`nplc --emit=c-stubs` for npli. P7 (optional inline type checks) was gated on bench evidence and dropped
(worklog 2026-10-27); `.agents/archive/research/gui` is the user's GUI
library research, not yet scheduled.
P7 bench done (worklog 2026-10-27): native 9.5x Guile on loop/fib/tak,
32x vectors; costs are generic arithmetic dispatch, the tail trampoline,
and per-call environment allocation, not type checks. Order decided:
O1, O2, then the O3 design (no O3 code before acceptance), per
[handoff-performance](archive/handoff-performance.md) (archived, completed). O1 done (worklog
2026-10-27): inline fixnum add/sub/compare with overflow fallback; scaled
CPU loop 7.41 -> 1.27 s, fib 3.77 -> 2.85, tak 2.46 -> 2.05 (Guile 0.78,
0.40, 0.27). O2 done (worklog 2026-10-27): self tail calls are jumps;
loop 1.26 -> 0.33 s (Guile 0.79), fib/tak ~3% (bound by non-tail calls).
O3 design [design-calls](archive/design/design-calls.md) accepted 2026-10-27
with the recommended D1-D4. Order: C0 (GC stack scan fix), C1a, C1b, C1c,
then stop for the C2 decision on measurements. C0 done (worklog
2026-10-29): the GC stack scan reaches the caller of a nested trampoline.
C1a done (worklog 2026-10-29): continuations rebind with `let`, no cell;
fib 2.77 -> 1.94 s, tak 2.06 -> 1.55 s, callcc -15%. C1b done (worklog
2026-10-29): flat closures, inline capture loads, `npl_make_environment`
and `npl_environment_ref` removed; fib 2.01 -> 1.27 s, tak 1.60 -> 1.02,
vectors 4.41 -> 2.84, callcc 4.41 -> 2.78. C1c dropped by the user
after measurement (seed +33%, no gain; worklog 2026-10-29). O2b done
(worklog 2026-10-29): named-let and letrec loops whose name is assigned
once are jumps; no bench change, because a `begin` in a loop body still
became a continuation closure. O2b-2 done (worklog 2026-11-02): simple
non-final sequence expressions stay in place; vectors 2.83 -> 0.50 s,
strings 4.01 -> 3.51 s. C2 addendum in
[design-calls](archive/design/design-calls.md) accepted 2026-11-02 (E1-E3 as
recommended). C2a done (worklog 2026-11-02): `NPL_DIRECT_REPORT` analysis,
no SSA change; fib/tak/loop safe; compiler 545/898 safe, C2b seed growth
estimate +6.2%. C2b done (worklog 2026-11-03): direct entries
`$_npl_dfn_<name>` with a stack-limit fallback (`runtime/stack.c`, at most
256 KiB of direct frames); fib 1.23 -> 0.12 s, tak 0.97 -> 0.11 (both now
below Guile), seed -8.6%. Follow-ups done (worklog 2026-11-03): fallback
windows (`npl_direct_fallback`) and loop registers; loop back to C2a,
fact +2% CPU / +7.8% instructions from scanning deep direct frames at every
collection (GC change, not done). C2c done (worklog 2026-11-03): `raise`
and variadic direct entries; compiler 576/941 direct-safe; strings -24%,
fact -7% (now below C2a), closures -4% against the previous commit.
Against Guile/Racket (worklog 2026-11-03): ahead on fib, tak, loop; mixed
on fact, vectors; behind on callcc (vs Racket), strings 3-4x, closures 4x,
lists 5-9x. Perf is judged against Guile/Racket, not our own commits; GC
fine-tuning (incremental stack scan) is deferred. 2026-11-05: closures
(bignum in place) and strings (write-string, string case in C) are now
level with Guile/Racket (worklog 2026-11-05). C3 done (direct entries for
lambdas, design-calls addendum C3): lists 5.00 -> 1.94 s (Guile 0.97,
Racket 0.57), callcc 3.56 -> 1.93 (Guile 2.24). Remaining gaps: lists and
vectors (2x Guile) and callcc (4x Racket); the lists remainder is
allocation/GC, deferred with GC tuning. npli performance (user, 2026-11-06):
nested closure call sites done (worklog 2026-11-06): npli fib 1.24 -> 0.93 s,
loop 8.86 -> 6.42; still 2-3x the Guile interpreter. If-test constant folding
investigated: ~4% improvement, too small to justify complexity (worklog
2026-11-06). npli performance investigation complete: 2-3x gap vs Guile
interpreter is acceptable for an AST walker; further optimization would
require structural changes (bytecode, loop specialization). C4 (design-calls
addendum) deferred, not started: its estimate did not hold.

## Language Data (2026-11-09)

Design [design-data](archive/design/design-data.md) (user: implement without a
review round). Order (design sections 1-5): inline primitives, built-in
records (structural `equal?`, `record-update`), SRFI 125 hash tables,
`match`, compiler adoption; `syntax-case` deferred. Inline primitives done
(worklog 2026-11-09): car, cdr, vector-length, vector-ref, vector-set!
inline; self-compile 45.56 -> 41.19 G instructions, seed +28%. Records
done (worklog 2026-11-09): derived `define-record-type` and
`record-update` (also `(npl record)`), inline predicate/accessor/modifier,
structural `equal?`, Guile-style printing, and a macro hygiene fix
(one alias per template name); record read 128 -> 85 instructions
(vector-ref 69; an accessor inliner is a follow-up). SRFI 125 hash tables
done (worklog 2026-11-09): runtime `BOX_HASHTABLE`, import-free primitive
rows, `(srfi 125)` (the `(srfi 69)` compatibility library was removed in 0.1.2); 1M eqv inserts and lookups 0.33 s
(Guile compiled SRFI 69 1.5 s). `match` done (worklog 2026-11-10):
Shinn's matcher as derived syntax, import-free, `(npl match)`, record
patterns with `$`, `match-define`; fixed alias-environment copying (one
`or` pattern compiled in 86 s, now 0.1 s; self-compile -12.6%
instructions), derived-syntax keyword prebinding, literal identifiers for
primitives, `define-values` from macros, literals in spliced macro output.
Section 5 step 1 done (worklog 2026-11-11): sampled hot spots, not the
planned tables: `byte-data` was O(n^2) (now halving) and syntax marks an
eq association list (now an eq hash table); self-compile 98.20 G -> 20.29 G
instructions, 7.07 -> 1.41 s user. Step 1b: predeclared globals as one
hashed layer in the value environment, 20.29 -> 14.06 G, 1.02 s user.
Step 1c: backend function, arity, and global tables hashed (`SYMS` stays
an ordered list), 14.06 -> 12.49 G, 0.90 s user. Step 1 is done: the
remaining profile is allocation, GC, string building, and file reads.
Step 2 done: syntax captures, syntax-rules transformers, `SELF-FUNCTION`,
and direct-call entries are records (accessor names unchanged), +1%
instructions (12.61 G), seed -500 lines; a record used by the frontend
must be defined before its first use, because npli compiles the frontend
as a library. Riskier structures (environment entries, primitive rows,
npli session, library info) stay as they are. Step 3 pilot (worklog 2026-11-11): `match` in the control-conversion
dispatch grew the seed by 1,644 lines and self-compile by 0.8% (a failure
closure per clause), failing the section 4 rule; reverted, not adopted.
User chose (2026-11-11): a cheaper `match` expansion first, then redo the
pilot. Re-running the pilot with a closure-free clause expansion exposed
an exponential control conversion of `if` tests that are themselves
`if`s of simple parts; fixed (worklog 2026-11-11), seed 316,843 -> 288,995
lines, bench unchanged. Also fixed: a `let` with simple inits stays a
`let` in control conversion instead of becoming a closure applied to them
(seed 288,995 -> 261,586 lines, -9.5%; self-compile 12.28 -> 11.13 G).
Second pilot done (worklog 2026-11-11): the closure-free `%match-clause`
expansion is linear in clauses and slightly smaller than Shinn's (24
clauses 46,410 vs 46,636 lines), but the expander costs 4,138 seed lines
(+1.6%) and the pilot still grows the seed (+549 vs Shinn's +633 while
removing 22 source lines), so section 4's rule fails under either
expansion. Reverted; the compiler keeps its hand-written dispatch and
`match` is unchanged for programs and libraries. Section 5 is complete as
far as the rule allows. `syntax-case`: deferred, and the user does not
want it while it is deferred.

## Accepted Seed

- `bootstrap/current.ssa`: 265,343 lines; SHA-256
  `c1212d190d7506a475805d67508bc520ec5cee748b037d3eda47f27f18f8b342`
  (strict relay, 2026-11-13, review fixes and endianness feature). The seed calls
  `npl_gc_write_barrier_value`, so it needs a runtime from 2026-11-09 or
  later; its `(scheme base)` needs `define-record-type` from the derived
  syntax, so it must not be paired with a `lib/` from before records. The closure
  layout changed with C3 (capture i at byte 48 + 8i): a seed from before
  C3 must be linked with a runtime from before C3.
- `tools/run_tests.sh` (with relay): all PASS, macOS ~9-16 min, Linux
  (`tools/check_linux.sh`) ~12 min. No GC override is needed on a loaded
  host since the 2026-10-21 defaults.
- Permanent `bootstrap/bootstrap.ssa` and `bootstrap/compiler/` unchanged.

## Open Items

- 0.1.1 review follow-ups: [state review](reviews/2026-11-12-0.1.0-state-review.md).
  Highest-value next work is source spans and stack traces, a unified
  uncaught-error exit status, SRFI 1/13/113, and separate compilation.
- `(sqrt -1.0-0.0i)` branch convention: follow IEEE signed-zero convention
  (Guile and Racket precedent) when sqrt is implemented. Not blocking.
- Operand evaluation order differs between native and npli: in
  `(list (vector-ref v 0) (f))` with f mutating v, native shows the new
  value (operands evaluated right to left, or the first operand read after
  the call), npli and Guile the old one. R7RS leaves the order
  unspecified, but the two paths should agree (worklog 2026-11-09).
- Guile 3.0 oracle gaps (covered by native suite `error-objects` instead):
  failed `open-input-file` is not `file-error?`; absent irritants are #f.
  Not blocking; native tests provide coverage.

History: [worklog](worklog/). Expect ~10 min for gates without the relay;
a relay can take over an hour under heavy load, so run it in the
background.

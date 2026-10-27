# Workstate

## Current

Standard library stage per [plan-r7rs](../docs/plan-r7rs.md). L0-L3 and L4a
(records, error objects, file/read errors) are done. L4b (catchable
primitive errors, plan section "Primitive Errors") is done: runtime value,
port, and file errors and library argument checks raise error objects.
L5 is done (current ports as parameter objects, text I/O, with-*-file,
features, (scheme time), (scheme complex)). L7 (scheme r5rs) is done; all
R7RS-small libraries export their full name lists (556/556). Remaining: L6
(Unicode strings and chars, datum labels): design
[design-l6](../docs/design-l6.md) accepted; L6a+b (Unicode strings,
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
host load ~7, `e554744`): native vs Guile wall is 0.8x on fact, 2.6x tak,
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
foreign calls drafted in `docs/design-primitives.md` (uncommitted, awaiting
review). Bench after these changes (worklog 19:50): native 1.1-6.1x
Guile wall, compile 2.3-3.5 s; npli > 30 s everywhere.

## Agreed Plan (2026-10-20)

Scope: shared primitive declarations and C foreign calls
([design-primitives](../docs/design-primitives.md)), an npli evaluator
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
  [handoff-performance](handoff-performance.md).

G1 done (worklog 2026-10-20 13:40). G2 done (17:30). G3 done (worklog
2026-10-21 08:00): defaults 1 MiB nursery and 128 MiB full threshold;
self-emit ~80% of nursery-off CPU at 0.27 GB peak. R0 done (worklog
2026-10-21 10:30). P1 done (14:50). P2a done (worklog
2026-10-22 10:50): one shared primitive table, rule-derived C symbols,
generated `(npl primitive)` exports. P2b done (worklog 2026-10-22
13:30): `define-c-primitive`, `%c-call`, `%c-procedure` register rows per
compilation; npli resolves them since P5b. FFI user guide `docs/ffi.md`. E1 done:
`docs/design-evaluator.md` accepted 2026-10-22 with D1 runtime unspecified
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
`nplc --emit=c-stubs` for npli. Next: P7 (optional inline type checks,
gated on bench evidence); docs/research/gui is the user's GUI library
research, not yet scheduled.
P7 bench done (worklog 2026-10-27): native 9.5x Guile on loop/fib/tak,
32x vectors; costs are generic arithmetic dispatch, the tail trampoline,
and per-call environment allocation, not type checks. Order decided:
O1, O2, then the O3 design (no O3 code before acceptance), per
[handoff-performance](handoff-performance.md). O1 done (worklog
2026-10-27): inline fixnum add/sub/compare with overflow fallback; scaled
CPU loop 7.41 -> 1.27 s, fib 3.77 -> 2.85, tak 2.46 -> 2.05 (Guile 0.78,
0.40, 0.27). O2 done (worklog 2026-10-27): self tail calls are jumps;
loop 1.26 -> 0.33 s (Guile 0.79), fib/tak ~3% (bound by non-tail calls).
Next: O3 design `docs/design-calls.md`, awaiting user review; no O3 code
before acceptance.

## Accepted Seed

- `bootstrap/current.ssa`: 264,732 lines; SHA-256
  `2cfd62c89a700b072d377c967a0f5aa8aeecb4041b0437d3d5d1d54d1cdf2547`
  (macOS strict relay, 2026-10-27, O2).
- `tools/run_tests.sh` (with relay): all PASS, macOS ~11-16 min, Linux
  (`tools/check_linux.sh`) ~12 min. No GC override is needed on a loaded
  host since the 2026-10-21 defaults.
- Permanent `bootstrap/bootstrap.ssa` and `bootstrap/compiler/` unchanged.

## Open Items

- `(sqrt -1.0-0.0i)` branch convention needs a user decision.
- npli does not reject mutation of vector literals.
- Evaluator built-in numeric primitives used without imports still report
  plain evaluator errors (`+: expected numbers`).
- Guile 3.0 oracle gaps (covered by native suite `error-objects` instead):
  failed `open-input-file` is not `file-error?`; absent irritants are #f.

History: [worklog](worklog/). Expect ~10 min for gates without the relay;
a relay can take over an hour under heavy load, so run it in the
background.

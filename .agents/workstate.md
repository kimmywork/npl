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
(worklog 2026-10-19 15:40) cut strings/lists/callcc user CPU 3-4x. Next:
GC sweep and heap growth (compiler self-emit ~160 s wall, sweep-dominated),
then an npli evaluator redesign (compile-to-closures, lexical addresses,
direct primitive procedures), then a 10+ run baseline.

## Accepted Seed

- `bootstrap/current.ssa`: 224,969 lines; SHA-256
  `6253693bd20fe87a4b2b495f4a8c839d63ecab4eb75c57656f09ec38ef542830`
  (macOS strict relay, 2026-10-19).
- `tools/run_tests.sh` (with relay): all PASS, macOS ~11 min, Linux
  (`tools/check_linux.sh`) ~12 min. On a swap-bound host set
  `NPL_GC_THRESHOLD_BYTES=268435456`.
- Permanent `bootstrap/bootstrap.ssa` and `bootstrap/compiler/` unchanged.

## Open Items

- `(sqrt -1.0-0.0i)` branch convention needs a user decision.
- `guard` clauses run before dynamic-wind after thunks are unwound (R7RS
  4.2.7 evaluates them in guard's dynamic environment).
- npli does not reject mutation of vector literals.
- Evaluator built-in numeric primitives used without imports still report
  plain evaluator errors (`+: expected numbers`).
- Guile 3.0 oracle gaps (covered by native suite `error-objects` instead):
  failed `open-input-file` is not `file-error?`; absent irritants are #f.

History: [worklog](worklog/). On a loaded host, run gates with
`NPL_GC_THRESHOLD_BYTES=268435456` and expect ~10 min without the relay; a
relay can take over an hour under heavy load, so run it in the background.

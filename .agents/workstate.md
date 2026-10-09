# Workstate

## Current

Standard library stage per [plan-r7rs](../docs/plan-r7rs.md). L0-L3 and L4a
(records, error objects, file/read errors) are done. L4b (catchable
primitive errors, plan section "Primitive Errors") batch 1 is done: the
mechanism plus pair, vector, char, string-ref, arity, and non-procedure
errors. Next batches: remaining `runtime/value.c` checks (bytevectors,
strings, append, list->vector), numeric errors (now raised by libraries as
strings), then `runtime/io.c` port errors. The toolchain stage is complete and committed
(`b8508d1`): unified `nplc` seed, `npli`, install layout, Linux parity.

## Accepted Seed

- `bootstrap/current.ssa`: 203,397 lines; SHA-256
  `bb9769d40560dfb0fbfbe530f5de18482dbd5a5d513e8c039ce0eed942c420fd`
  (macOS strict relay).
- `tools/run_tests.sh` (with relay): all PASS, macOS ~11 min, Linux
  (`tools/check_linux.sh`) ~12 min. On a swap-bound host set
  `NPL_GC_THRESHOLD_BYTES=268435456`.
- Permanent `bootstrap/bootstrap.ssa` and `bootstrap/compiler/` unchanged.

## Open Items

- `(sqrt -1.0-0.0i)` branch convention needs a user decision.
- Files above the 800-line soft limit, to split when touched:
  `runtime/io.c`, `runtime/control.c`.
- `guard` clauses run before dynamic-wind after thunks are unwound (R7RS
  4.2.7 evaluates them in guard's dynamic environment).
- npli does not reject mutation of vector literals.
- Guile 3.0 oracle gaps (covered by native suite `error-objects` instead):
  failed `open-input-file` is not `file-error?`; absent irritants are #f.

History: [worklog](worklog/). On a loaded host, run gates with
`NPL_GC_THRESHOLD_BYTES=268435456` and expect ~10 min without the relay; a
relay can take over an hour under heavy load, so run it in the background.

# Workstate

## Current

Standard library stage per [plan-r7rs](../docs/plan-r7rs.md). L0-L3 are done;
next is L4 (record box type, define-record-type, error objects, catchable
primitive errors). The toolchain stage is complete and committed
(`b8508d1`): unified `nplc` seed, `npli`, install layout, Linux parity.

## Accepted Seed

- `bootstrap/current.ssa`: 198,010 lines; SHA-256
  `acf91c177e52d171630d80a975f25f4fba04afc45c0a07494c9d0fef2f4c85fa`
  (macOS strict relay).
- `tools/run_tests.sh` (with relay): all PASS, macOS ~11 min, Linux
  (`tools/check_linux.sh`) ~12 min. On a swap-bound host set
  `NPL_GC_THRESHOLD_BYTES=268435456`.
- Permanent `bootstrap/bootstrap.ssa` and `bootstrap/compiler/` unchanged.

## Open Items

- `(sqrt -1.0-0.0i)` branch convention needs a user decision.
- Files above the 800-line soft limit, to split when touched:
  `lib/npl/evaluator-core.scm`, `src/compiler/frontend/control.npl`,
  `expand.npl`, `library.npl`, `runtime/io.c`, `runtime/control.c`.

History: [worklog](worklog/).

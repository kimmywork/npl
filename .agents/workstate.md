# Workstate

## Current

Standard library stage per [plan-r7rs](../docs/plan-r7rs.md); next increment
L1 (lists and equivalence). The toolchain stage is complete and committed
(`b8508d1`): unified `nplc` seed, `npli`, install layout, Linux parity.

## Accepted Seed

- `bootstrap/current.ssa`: 195,239 lines; SHA-256
  `e6a183cb406a6be914b1589d9eacf9748d9e21c718797987d55c29ece7526b5e`
  (macOS relay; the previous seed `b9a93cea...` was also verified on Linux).
- `tools/run_tests.sh` (with relay): all PASS, macOS ~11 min, Linux
  (`tools/check_linux.sh`) ~12 min. On a swap-bound host set
  `NPL_GC_THRESHOLD_BYTES=268435456`.
- Permanent `bootstrap/bootstrap.ssa` and `bootstrap/compiler/` unchanged.

## Open Items

- Ignored `build/` holds ~380 stale scratch entries from earlier stages
  (relay dirs, probes, logs); unreferenced and reproducible, pending user
  confirmation before deletion.
- `(sqrt -1.0-0.0i)` branch convention needs a user decision.
- Files above the 800-line soft limit, to split when touched:
  `lib/npl/evaluator-core.scm`, `src/compiler/frontend/control.npl`,
  `expand.npl`, `library.npl`, `runtime/io.c`, `runtime/control.c`.

History: [worklog](worklog/).

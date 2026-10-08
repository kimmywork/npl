# Workstate

## Current

Standard library stage per [plan-r7rs](../docs/plan-r7rs.md). L0 (confirmed): L0a
symbol interning done; next L0b primitives as values in native code
(`(map car xs)` fails to compile), then L0c native `apply`, then L1. The toolchain stage is complete and committed
(`b8508d1`): unified `nplc` seed, `npli`, install layout, Linux parity.

## Accepted Seed

- `bootstrap/current.ssa`: 191,111 lines; SHA-256
  `3482e39418c26b4df3814b6d0fe3a8a7c0343a8be4a23f20a830acddb22f48aa`
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

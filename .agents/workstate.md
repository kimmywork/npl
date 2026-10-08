# Workstate

## Current

Standard library stage per [plan-r7rs](../docs/plan-r7rs.md). L0-L2 are done;
next is L3 (strings and chars: `string-ref` returns a char, string
mutation primitives, then the string library). The toolchain stage is complete and committed
(`b8508d1`): unified `nplc` seed, `npli`, install layout, Linux parity.

## Accepted Seed

- `bootstrap/current.ssa`: 197,695 lines; SHA-256
  `f0df562dc5e1ff06c24bebce31e185ef1f5cc45241b41841699622e0a7a31f34`
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

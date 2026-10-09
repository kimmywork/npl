# Workstate

## Current

Standard library stage per [plan-r7rs](../docs/plan-r7rs.md). L0-L3 are done;
next is L4 (record box type, define-record-type, error objects, catchable
primitive errors). The toolchain stage is complete and committed
(`b8508d1`): unified `nplc` seed, `npli`, install layout, Linux parity.

## Accepted Seed

- `bootstrap/current.ssa`: 200,188 lines; SHA-256
  `9d11c91845fd4b8d89fd1c56dac4197816e348f48c1f3607ae27d375291a2b4f`
  (macOS strict relay).
- `tools/run_tests.sh` (with relay): all PASS, macOS ~11 min, Linux
  (`tools/check_linux.sh`) ~12 min. On a swap-bound host set
  `NPL_GC_THRESHOLD_BYTES=268435456`.
- Permanent `bootstrap/bootstrap.ssa` and `bootstrap/compiler/` unchanged.

## Open Items

- `(sqrt -1.0-0.0i)` branch convention needs a user decision.
- Files above the 800-line soft limit, to split when touched:
  `src/compiler/frontend/expand.npl`, `library.npl`, `runtime/io.c`,
  `runtime/control.c`.

History: [worklog](worklog/). On a loaded host, run gates with
`NPL_GC_THRESHOLD_BYTES=268435456` and expect ~10 min without the relay.

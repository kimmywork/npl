# Workstate

## Current

Standard library stage per [plan-r7rs](../docs/plan-r7rs.md). L1 is blocked on
three missing native capabilities found 2026-10-08: symbols are not interned
(`(eq? 'a 'a)` is `#f` in npli; no `string->symbol`), primitives cannot be
used as values in native code (`(map car xs)` fails to compile), and native
code has no `apply`. Proposed L0a-L0c (symbol intern table; generated
primitive wrappers from `backend/primitives.npl`; an `apply` tail-call
primitive) await user confirmation. The toolchain stage is complete and committed
(`b8508d1`): unified `nplc` seed, `npli`, install layout, Linux parity.

## Accepted Seed

- `bootstrap/current.ssa`: 190,873 lines; SHA-256
  `3b415b8b70847fbb21428b4c8bf4729ac1a69552583676cba9c6f115e9771da3`
  (macOS relay; seed `b9a93cea...` from before the registry refactor was also
  verified on Linux).
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

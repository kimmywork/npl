# Workstate

## Current

Standard library stage per [plan-r7rs](../docs/plan-r7rs.md). L0-L3 and L4a
(records, error objects, file/read errors) are done. L4b (catchable
primitive errors, plan section "Primitive Errors") is done: runtime value,
port, and file errors and library argument checks raise error objects.
L5 is done (current ports as parameter objects, text I/O, with-*-file,
features, (scheme time), (scheme complex)). L7 (scheme r5rs) is done; all
R7RS-small libraries export their full name lists (556/556). Remaining: L6
(Unicode strings and chars, datum labels) needs a design review before code;
then P1 (Guile performance baseline). The toolchain stage is complete and committed
(`b8508d1`): unified `nplc` seed, `npli`, install layout, Linux parity.

## Accepted Seed

- `bootstrap/current.ssa`: 203,437 lines; SHA-256
  `daa4fe0012f3d74133e9cd4156163932920a6941d9a74f75e983325ab1e07093`
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
- Evaluator built-in numeric primitives used without imports still report
  plain evaluator errors (`+: expected numbers`).
- Guile 3.0 oracle gaps (covered by native suite `error-objects` instead):
  failed `open-input-file` is not `file-error?`; absent irritants are #f.

History: [worklog](worklog/). On a loaded host, run gates with
`NPL_GC_THRESHOLD_BYTES=268435456` and expect ~10 min without the relay; a
relay can take over an hour under heavy load, so run it in the background.

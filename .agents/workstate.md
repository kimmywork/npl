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

## Accepted Seed

- `bootstrap/current.ssa`: 219,191 lines; SHA-256
  `820dde3719244bb485fdb196b5ac80263e7e3845bf73a0033f7c47a80d76a2a1`
  (macOS strict relay).
- `tools/run_tests.sh` (with relay): all PASS, macOS ~11 min, Linux
  (`tools/check_linux.sh`) ~12 min. On a swap-bound host set
  `NPL_GC_THRESHOLD_BYTES=268435456`.
- Permanent `bootstrap/bootstrap.ssa` and `bootstrap/compiler/` unchanged.

## Open Items

- `(sqrt -1.0-0.0i)` branch convention needs a user decision.
- Files above the 800-line soft limit, to split when touched:
  `runtime/control.c`.
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

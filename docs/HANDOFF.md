# R7RS Numeric Handoff

## Delivery Boundary

The bounded `(scheme inexact)` math increment is implemented and its shared
native/REPL/Guile fixture passes. This is NOT complete `(scheme inexact)`,
`(scheme complex)`, number-tower, or compiler/evaluator conformance. The finite
real-axis `asin`/`acos` principal-value and signed-zero boundary matrix is now
normalized and verified; one branch conflict, `(sqrt -1.0-0.0i)` (R7RS
generalized principal value vs IEEE negative-zero convention), remains
unresolved. Continue only within R7RS Small.

Read [workstate](../.agents/workstate.md) for the current snapshot and
[conformance plan](plan-r7rs-conformance-guile.md) for the broader backlog.

## Implemented

- `lib/scheme/inexact.sld` exports the 12 standard procedures. NPL owns public
  type checks, catchable errors, one/two-argument `atan`, and optional-base `log`.
  It imports only the required raw primitives alongside `(scheme base)`.
- `runtime/inexact.c` wraps C99 real/complex libm routines through existing
  `npl_inexact_op` codes 8-16 and new `npl_inexact_binary` code 0 (`atan2`).
  Codes 0-7 retain their roles; classification codes reuse the named predicates.
  Exact values and both complex components participate in finite/NaN/infinite
  classification. GC roots cover allocating conversions and boxed results.
- Native registration/lowering and evaluator name/arity/dispatch agree for the
  two math primitives. The malformed nested evaluator name predicate was
  replaced by `cond`. This is a scoped check, not a whole-registry audit.
- `finite?`, `nan?`, and `infinite?` now belong to `(scheme inexact)`, not the
  nonstandard previous `(scheme base)` export placement. Update client imports.
- `tests/r7rs/fixtures/inexact-math/behavior.scm` is consumed unchanged by all
  three surfaces. It covers real/rational inputs, off-cut complex functions,
  negative-real log/sqrt, optional arities, non-finite classification, catchable
  errors, and the positive-side real-axis `asin`/`acos` principal-value signs
  plus atan/log signed-zero cuts. Finite comparisons use 1e-9 absolute
  tolerance; stdout, stderr, and process status match exactly.
- `runtime/inexact.c` normalizes the imaginary sign of finite real-axis
  `(asin x)`/`(acos x)` with `|x| > 1` to the R7RS/CLtL2 principal value instead
  of the raw C99 `cimag` result. Explicit zero-signed complex inputs and the
  negative-side cases are verified by the C substrate matrix
  (`runtime/numeric_stress.c`, +41 checks), because Guile is not a reliable
  oracle there (`oracle-unavailable`).
- `tools/run_tests.sh` now includes the six shared numeric fixtures, from exact
  integers through inexact math. No separate evaluator math semantics were added.
  The existing frontend registry files remain slightly above the 800-line soft
  limit (control 854, library 810): this recovery increment kept minimal routes
  adjacent to their existing families. Cohesive extraction belongs with the
  upcoming scoped registry audit, not a broader bootstrap restructuring here.

## Verified Evidence

- Strict isolated relay: `build/branch-relay/gen1.ssa`, `gen2.ssa`, and
  `gen3.ssa` are byte-identical after the runtime change; all emits exited 44
  with empty stderr. `current.ssa` was not replaced.
- Accepted `bootstrap/current.ssa`: 352,393 lines, SHA-256
  `ebc4b2f21bfb25452062c7154950b1f7c2e31d9d1c186bbf8c6f216b36540110`.
- `tools/build_npl_repl.sh` (now `tools/build_npli.sh`) rebuilt both driver and persistent REPL; the updated
  three-way fixture (with `branches`/`cuts` reports) passes with those fresh
  binaries, as do the other five shared numeric fixtures.
- Normal `tools/check_numeric_reference.sh`: 12,859 substrate checks (incl. 41
  new branch/signed-zero checks), 2,244 Guile oracle results, four zero-divisor
  errors pass.
- `tools/run_tests.sh --skip-current-fixed-point`: all 19 checks PASS with
  `all suites: PASS` after the runtime change, including full compiler, driver,
  persistent REPL core, core consistency, GC, six numeric fixtures, baseline and
  property tests.
- `git diff --check` and `bash -n tools/run_tests.sh` pass.
- `bootstrap/bootstrap.ssa` and `bootstrap/compiler/` were not changed.

## Open Boundaries And Next Work

1. The finite real-axis `asin`/`acos` principal-value convention and the
   atan/log signed-zero boundary matrix are now normalized and verified against
   R7RS 6.2 / CLtL2 12.5.3. Remaining boundary work is scoped to
   `(sqrt -1.0-0.0i)`, which still conflicts between the R7RS generalized
   principal value and the IEEE negative-zero convention; it must not be closed
   without the user's decision. Guile is not a reliable oracle for explicit
   zero-signed complex inputs or negative-side `asin`/`acos` (classified
   `oracle-unavailable`); those cases are covered by the C substrate matrix.
2. Complete mixed exact/inexact public utilities and classification. Current
   `rational?`/`integer?`, rounding, numerator/denominator, gcd/lcm, and conversion
   wrappers still have exact-only limitations; complex exactness is incomplete.
   Add standard `exact`/`inexact` names with tests, not just legacy aliases.
3. Add the standard `(scheme complex)` library and remaining `angle`, `magnitude`,
   `make-polar` procedures, reusing existing rectangular mechanisms. Broader
   complex grammar and non-integer/complex `expt` remain open.
4. Continue numeric primitive name/arity/type/error parity checks. Do not infer
   broad consistency from two newly covered raw routes. Math benchmarks and
   exhaustive overflow/underflow/non-finite matrices were not run in this stage.

## Recovery And Reproduction

The pre-recovery current hash was `07c81cd3...`; it still registered withdrawn
`npl_inexact_math/atan2` names. The preserved validated exact-utility seed
`build/utility-relay4.ssa` (`d55b9caa...`) generated `build/inexact-recovery.ssa`,
which then passed the strict isolated relay before replacing current. The old
current is retained at `build/inexact-pre-recovery-current.ssa`. Historical
self-compilation timeout causation is not conclusively diagnosed; the successful
recovery does not prove that evaluator parentheses caused it.

```sh
tools/build_npli.sh
tools/run_r7rs_fixture.sh tests/r7rs/fixtures/inexact-math
bash tools/check_numeric_reference.sh
tools/run_tests.sh --skip-current-fixed-point
# After compiler/runtime changes, preserve the strict three-generation gate:
tools/check_current_fixed_point.sh --work-dir build/branch-relay
```

Raw `--emit-qbe` success is exit 44, not 0. Diagnostics may appear on stdout.
Never accept an empty SSA or copy a transition into current before verification.
Fixture runners reuse existing binaries: explicitly rebuild after source changes.
Guile's fixture uses a private library module to avoid core-binding warnings and
imports `(scheme complex)` explicitly; NPL currently obtains rectangular helpers
from base. The workspace contains many pre-existing uncommitted numeric changes;
no commit, destructive cleanup, or permanent-bootstrap update was performed.

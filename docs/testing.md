# Verification

The gate is driven by `tools/run-tests.npl`; `tools/run_tests.sh` is a
compatibility wrapper that builds `nplc`/`npli`, sets the UTF-8 locale and
native-package path, then runs the NPL runner.

```bash
tools/run_tests.sh --skip-current-fixed-point   # no seed relay
tools/run_tests.sh                              # includes the seed relay
tools/run-tests.npl --skip-current-fixed-point  # direct npli invocation
```

Runner options:

- `--skip-current-fixed-point` skips the gen1/gen2/gen3 relay check.
- `--skip-legacy` runs only the fixture manifest and property suites.
- `--properties-only` runs only the property suites (it skips fixtures,
  unit self-tests, and legacy adapters).

The runner prints one `PASS`/`SKIP`/`FAIL` line per stage and exits with the
number of failures. Legacy shell checks are called as adapters; fixture and
property discovery is already NPL-native. `NPL_TEST_LC_ALL` overrides the
UTF-8 locale used for Guile comparisons.

The main gate intentionally excludes the frozen bootstrap chain
(`bootstrap/bootstrap.ssa`, `bootstrap/compiler/`, `bootstrap/runtime/`) and
stage0. Run those separately:

```bash
bootstrap/stage0/tools/run.sh
```

That script owns the stage0 reader/QBE/core/R7RS checks, the permanent
bootstrap fixed point, and the stage0 property tests. Keeping it outside the
main gate lets the evolving compiler and libraries move without treating
frozen recovery artifacts as current behavior.

## Test layers

| Layer | Location | Purpose | Driver |
|---|---|---|---|
| Unit/acceptance | `src/compiler/tests/` | compiler and runtime behavior, negative contracts, C extensions | `check_full_compiler.sh` (legacy adapter), `(npl test)` |
| Framework self-tests | `tests/unit/` | `(npl test)` behavior | `(npl test)` |
| Expect fixtures | `tests/fixtures/` | identical observable result on native, npli, and Guile | `(npl test expect)` |
| Property tests | `tests/property/` | deterministic generated inputs | `(npl test property)` |
| Numeric oracle | `tests/numeric/` | exact numeric substrate against Guile | `(tools toolchain)` |
| Upstream suite | `tests/chibi/` | independent chibi R7RS suite | `run_chibi_suite.sh` |
| Library metadata | `(tools libraries)` | exports, imports, cycles, layering, alias re-exports | `tools/run-tests.npl` |
| Toolchain checks (NPL) | `(tools toolchain)` | current fixed point, GC, numeric oracle, driver, install, core consistency | `tools/run-tests.npl` |
| Toolchain checks (legacy) | `tools/check_*.sh` | REPL, full compiler, relay | shell adapters |
| Frozen bootstrap | `bootstrap/stage0/tools/`, `bootstrap/tools/` | stage0, permanent bootstrap chain | separate script |

`tests/fixtures/` now contains both R7RS cases and NPL extension cases
(`native-packages`, `native-zlib`, `unix`, `time`, `match-extensions`,
`syntax-case-npl`, ...). The directory name is historical; the fixture
manifest and `fixture.meta` describe the behavior, not an R7RS-only claim.

## Unit test framework

`(npl test)` is an SRFI 64-compatible framework; `(srfi 64)` is an alias.
The implementation is `lib/npl/test/core.scm`. Historical suites include
`src/compiler/tests/framework.npl`, which includes the same core, so both
import styles share one implementation.

```scheme
(import (scheme base) (npl test))

(test-begin "arithmetic")
(test-equal "addition" 4 (+ 2 2))
(test-assert (> 3 2))
(test-error (car '()))
(test-skip 1)
(test-end "arithmetic")
(finish-tests "arithmetic")   ; NPL gate summary + exit status
```

Forms: `test-begin`, `test-end`, `test-assert`, `test-equal`, `test-eqv`,
`test-eq`, `test-inexact`, `test-error`, `test-expect-fail`, `test-skip`,
`test-group`, `test-group-with-cleanup`, `finish-tests`, and the runner count
accessors. `test-identity` and the legacy
`(test-equal "name" (lambda () expr) expected)` form are NPL extensions so
existing suites keep working.

`finish-tests` keeps the historical output and exit status:

```
PASS <suite> <count>
FAILED <suite> <failures>/<total>
```

New suites can import `(npl test)` and call `finish-tests`; the old
`(include "framework.npl")` form remains valid.

The implementation follows SRFI 64's immediate model. A later layer can add
SRFI 269-style first-class `test`/`suite` definitions for filtering and
re-running subsets; the fixture manifest already separates discovery from
execution. `test-error` accepts an error-type argument but currently only
checks that an error was raised; `test-inexact` compares with `=`.

## Property tests

`(npl test property)` follows the SRFI 252 shape; `(srfi 252)` is an alias.
Generators are thunks, and `property-seed` makes runs deterministic.

```scheme
(import (scheme base) (npl test) (npl test property))

(test-property (lambda (a b) (= (+ a b) (+ b a)))
               (list (exact-integer-generator) (exact-integer-generator))
               200)
(test-property-error (lambda (x) (error "boom" x))
                     (list (exact-integer-generator)))
(finish-tests "pbt")
```

Built-in generators: `boolean-generator`, `bytevector-generator`,
`char-generator`, `exact-integer-generator`/`integer-generator`,
`exact-real-generator`, `real-generator`, `number-generator`,
`rational-generator`, `complex-generator`, `string-generator`,
`symbol-generator`. A custom generator is any thunk returning a value or an
eof object.

`test-property` runs one recorded test per property, not one per generated
input. Failures report the generated values. `test-property-expect-fail`,
`test-property-skip`, `test-property-error`, and
`test-property-error-type` cover the other SRFI 252 outcomes.

Add a property suite as `tests/property/<name>.npl`; the runner discovers it
with `glob`.

## Expect fixtures

A fixture directory contains `fixture.meta`, a native source, a REPL input,
expected stdout/stderr/status, and optional Guile settings. `(npl test
expect)` compiles and runs the native source, feeds the REPL transcript, runs
the Guile oracle when requested, and compares all three surfaces.

The implementation is `lib/npl/test/expect.sld`; the CLI is
`tools/expect-runner.npl`. `tools/run_r7rs_fixture.sh` is a thin wrapper.

### `fixture.meta`

| Key | Required | Meaning |
|---|---|---|
| `name` | yes | fixture name in report lines |
| `native` | yes | source for the native lane |
| `repl` | yes | stdin transcript for npli |
| `repl-body` | no | appended to `repl.in` with a final `(exit)` |
| `guile` | unless `oracle=none` | Guile script (usually a private-library wrapper) |
| `oracle` | no | `none` skips Guile and reports `oracle-unavailable` |
| `guile-defect` | no | space-separated Guile versions to skip (`oracle-known-defect`) |
| `guile-defect-reason` | with `guile-defect` | required explanation |
| `expected-out` | yes | expected stdout |
| `expected-err` | yes | expected stderr |
| `expected-status` | yes | expected exit status |

### Adding a fixture

1. Create `tests/fixtures/<name>/` with `behavior.scm`, `native.npl`,
   `repl.in`, `guile.scm` (unless `oracle=none`), `expected.out`,
   `expected.err`, and `fixture.meta`.
2. Add `<name>` to `tests/fixtures/manifest.scm`. This is the only list to
   update; the runner discovers fixture directories through the manifest.
3. Run `tools/run_r7rs_fixture.sh tests/fixtures/<name>`.

Fixtures outside the gate (`core-bindings`, `smoke`, `integer-io`,
`port-wrappers`) are still runnable directly with the same command.

## Library metadata

`(tools libraries)` walks every file under `lib/`, reads all `.sld`
definitions and their includes, and derives the binding graph from the
source. It checks:

- every export is defined exactly once locally, or resolves through an
  import (including `(npl internal primitive)` and C-backed bindings);
- duplicate exports, duplicate definitions, and export/import collisions;
- import conflicts by binding origin, including `only`/`except`/`prefix`/
  `rename` and alias re-exports;
- cycles in the library import graph;
- layer rules: internals -> scheme -> npl -> demo/srfi, with SRFI aliases
  not importing other SRFIs.

No alias table is maintained: an alias export is valid when its origin
resolves through the canonical library. The old `check_srfi_exports.py`
table was removed.

## Toolchain checks in NPL

`(tools toolchain)` runs the current fixed-point relay, GC stress, numeric
oracle, driver, install-layout, and core-consistency checks through
`run-process*` and file assertions. They are normal NPL functions, not shell
subprocesses.

`check_repl.sh`, `check_full_compiler.sh`, and
`check_relay_scripts.sh` are still shell adapters. They exercise `ar`,
`file`, `sed`, container paths, and REPL transcripts that have not been
ported yet; each is a candidate for the same `(tools toolchain)` API.

## Compiler suites and contracts

`check_full_compiler.sh` remains the compiler acceptance adapter. It builds a
compiler from `bootstrap/current.ssa` and runs `src/compiler/tests/*.npl`
through `(npl test)`, plus negative programs under `contracts/`,
`reader-errors/`, and `macro-errors/`, SSA probes, and C-extension checks.

The suite list and expected counts are still in that shell script; migrating
it to a compiler-suite manifest is the next test-infrastructure increment.

## The seed and relay

`bootstrap/current.ssa` is the compiler's own source compiled to QBE SSA. A
change to `src/compiler/` or the runtime ABI requires a relay; `lib/` changes
do not.

```bash
tools/check_current_fixed_point.sh   # verify only
tools/update_current_ssa.sh          # verify, then install gen3
```

The installed seed line count and SHA-256 are recorded in
`.agents/workstate.md` (Accepted Seed).

## What to run for a change

| Change | Required |
|---|---|
| `lib/**` only | `tools/run_tests.sh --skip-current-fixed-point`; add/extend a unit, property, or expect test |
| `src/compiler/**` | as above plus `tools/update_current_ssa.sh` (two-step if codegen changed) |
| `runtime/**` | as above plus the `runtime GC` suite in `(tools toolchain)`; ABI changes need a relay |
| `tools/**` | the affected check plus `tools/check_relay_scripts.sh` |
| documentation only | every `](…)` target under `docs/` and `.agents/` exists |

Before a milestone, run the gate on macOS and Linux
(`tools/check_linux.sh --skip-current-fixed-point`), plus
`tools/run_chibi_suite.sh --native`.

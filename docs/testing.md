# Verification

Status: frozen at the language milestone, 2026-11-12. This is the workflow a
change must pass. Build and install are in [toolchain](toolchain.md); the
conformance claim this workflow establishes is in
[r7rs-conformance](r7rs-conformance.md).

## The gate

```bash
tools/run_tests.sh                         # with the seed relay, ~11-16 min
tools/run_tests.sh --skip-current-fixed-point   # without it, ~6-10 min
```

`tools/run_tests.sh` prints one `PASS`/`FAIL` line per stage and exits with the
number of failures. On a failure it prints the last 30 lines of that stage's
output (captured in `/tmp/npl-suite.out`). 57 stages with
`--skip-current-fixed-point` (stage 5 is omitted), 58 with the relay. The
gate selects a UTF-8
locale (`NPL_TEST_LC_ALL` overrides it) so Guile encodes Unicode output
independently of the caller's environment. CI (`.github/workflows/ci.yml`)
runs the Linux gate without the relay on pull requests and pushes to
`master`.

| # | Stage | What it proves |
|---|---|---|
| 1 | `build_toolchain.sh` | `nplc` (from `bootstrap/current.ssa`) and `npli` build; downstream checks reuse them via `NPL_TOOLCHAIN_BUILT` |
| 2 | `check_stage0.sh` | the frozen stage0 reader and executable |
| 3 | `check_stage0_qbe.sh` | frozen stage0 QBE output |
| 4 | `check_bootstrap.sh` | the permanent chain (`bootstrap/bootstrap.ssa` + `bootstrap/compiler/`) against the frozen runtime |
| 5 | `check_current_fixed_point.sh` | the seed relay: gen1 == gen2 == gen3 (skipped by the flag) |
| 6 | `check_full_compiler.sh` | 34 compiler suites, 77 negative checks, SSA probes, C extensions |
| 7 | `check_driver.sh` | unified `nplc`: output kinds, path handling, failure cleanup, `-l`/`-L` |
| 8 | `check_install.sh` | install layout used from outside the repo, including a prefix with a space |
| 9 | `check_repl.sh` | `npli` REPL, script mode, `-l` |
| 10 | `check_core_consistency.sh` | shared core semantics and malformed-syntax recovery (runs the `core-bindings` fixture) |
| 11 | `check_relay_scripts.sh` | relay orchestration with tiny tool substitutes |
| 12 | `check_gc.sh` | GC stress: 1M allocations, 64 KiB nursery with `NPL_GC_VERIFY` |
| 13 | `check_numeric_reference.sh` | exact numeric substrate against Guile |
| 14-54 | 41 × `run_r7rs_fixture.sh` | three-way fixtures (native, npli, Guile) |
| 55 | `check_core.sh` | stage0 dynamic core |
| 56 | `check_r7rs.sh` | R7RS baseline through stage0 |
| 57 | `prop_tests.py` | property tests for the stage0 compiler |

Optional, not in the gate:

| Command | Needs | Purpose |
|---|---|---|
| `tools/check_linux.sh [run_tests.sh flags]` | Docker (colima context) with `npl-linux:bookworm` | runs the gate on Debian/bookworm, clang, Guile 3 |
| `tools/run_chibi_suite.sh [--native]` | network (or a local `r7rs-tests.scm` argument) | chibi's independent R7RS suite |
| `python3 tools/bench.py [--runs N] [--scaled]` | Guile; Racket from `RACKET_BIN` (its `bin` directory) or `PATH` | performance against Guile and Racket |

## Fixtures

A fixture is a directory `tests/r7rs/fixtures/<name>/` describing one
behavior, checked on three surfaces with identical stdout, stderr, and exit
status:

| Surface | How it runs |
|---|---|
| native | `nplc native.npl -o prog`, then `prog` |
| npli | `npli --no-prompt < repl.in` |
| Guile 3 | `guile --r7rs -q --no-auto-compile -s guile.scm` |

### `fixture.meta` keys

| Key | Required | Meaning |
|---|---|---|
| `name` | yes | fixture name used in report lines |
| `native` | yes | source for the native lane |
| `repl` | yes | stdin transcript for npli |
| `repl-body` | no | appended to `repl.in` with a final `(exit)`; keeps the native, evaluator, and Guile bodies identical |
| `guile` | unless `oracle=none` | Guile script (wraps the body in a private `define-library`) |
| `oracle` | no | `none`: Guile lacks the behavior, so its surface is reported `oracle-unavailable` and expected output comes from R7RS plus a hand cross-check recorded in `behavior.scm` |
| `guile-defect` | no | space-separated Guile versions on which the oracle is skipped (`oracle-known-defect`) |
| `guile-defect-reason` | with `guile-defect` | required text explaining the defect |
| `expected-out` | yes | expected stdout |
| `expected-err` | yes | expected stderr |
| `expected-status` | yes | expected exit status |

Every value must be non-empty (`get_meta` fails otherwise).

### Adding a fixture

1. Create `tests/r7rs/fixtures/<name>/` with `behavior.scm` (definitions
   only), `native.npl` (imports + `(include "behavior.scm")`), `repl.in`
   (the imports for the `repl-body` lane), `guile.scm` (the private-library
   wrapper) unless `oracle=none`, `expected.out`, `expected.err`, and
   `fixture.meta`.
2. Write `expected.*` from the implementation only after checking each line
   against R7RS and, where available, Guile and Racket; record the reasoning
   in `behavior.scm`.
3. Add the name to the `for fixture in ...` loop in `tools/run_tests.sh`.
   Nothing is auto-discovered.
4. Run `tools/run_r7rs_fixture.sh tests/r7rs/fixtures/<name>`.

Differences with Guile are classified `npl-bug`, `guile-extension`,
`implementation-allowed`, `fixture-bug`, `unimplemented`, or
`oracle-unavailable`; the last two never count as conformance.

### Fixtures outside the gate

45 fixture directories exist; 41 are in the loop above. The others:

- `core-bindings` — run by `tools/check_core_consistency.sh` (stage 10).
- `smoke`, `integer-io`, `port-wrappers` — manual; run
  `tools/run_r7rs_fixture.sh tests/r7rs/fixtures/<name>` directly.

## Compiler suites and contracts

`tools/check_full_compiler.sh` builds a compiler from
`bootstrap/current.ssa`, then runs:

- `src/compiler/tests/<name>.npl` suites (`run_suite`): a program that
  `(include "framework.npl")`, calls `test-equal`/`test-identity`, and ends
  with `(finish-tests "<name>")`. The output string, `PASS <name> <n>`, is the
  expected value in the registration line, so **adding a test requires
  updating the count there**.
- negative programs (`reject_compile`, `reject_reader`, `reject_runtime`,
  `reject_literal_mutation`): `src/compiler/tests/contracts/*.npl`,
  `reader-errors/`, `macro-errors/`. Each must be registered explicitly; an
  unreferenced contract file is silently dead.
- SSA probes and C-extension checks: self tail calls are jumps, direct
  entries, inline primitives, closures have no environment object, C
  extension programs build with `-Wall -Wextra -Werror`, `--emit=c-stubs`
  output compiles.

Register by adding one line beside the existing ones in
`tools/check_full_compiler.sh`; there is no discovery.

## The seed and the relay

`bootstrap/current.ssa` is the compiler's own source compiled to QBE SSA; it
is the recovery artifact and is checked in. A change to `src/compiler/`,
`bootstrap/current.ssa`, or the runtime ABI requires a relay; a change under
`lib/` (libraries) does not.

```bash
tools/check_current_fixed_point.sh          # verify only
tools/update_current_ssa.sh                 # verify, then install gen3 as the seed
```

Only `bootstrap/current.ssa` is copied; the permanent `bootstrap/bootstrap.ssa`
and `bootstrap/compiler/` stay frozen. The line count and SHA-256 of the
installed seed are recorded in `.agents/workstate.md` (Accepted Seed).

### Two-step transition

When a change affects **code generation**, the old seed compiles the new source
into gen1, which still emits the old code shape, so gen1 differs from gen2 and
a single `update_current_ssa.sh` cannot pass. Do this instead:

1. Build the new source with the previous seed; this is the hand-built gen2.
   Require `gen2 == gen3` (build gen2, use it to emit gen3, compare).
2. Copy that gen2 over `bootstrap/current.ssa`.
3. Run `tools/update_current_ssa.sh` and require `gen1 == gen2 == gen3`;
   record the new lines and SHA in `.agents/workstate.md`.

Steps 1-3 happened on 2026-10-22, 2026-10-27, 2026-11-03 and twice on
2026-11-11; the worklogs for those dates show the exact commands.

## What to run for a change

| Change | Required |
|---|---|
| `lib/**` only | `tools/run_tests.sh --skip-current-fixed-point`; new/extended fixture for behavior |
| `src/compiler/**` | as above, plus `tools/update_current_ssa.sh` (two-step if codegen changed) and a test that fails on the previous seed |
| `runtime/**` | as above, plus `tools/check_gc.sh`; a runtime ABI change also needs the relay |
| shell scripts | `tools/check_relay_scripts.sh` (orchestration) and the affected check |
| documentation only | link check: every `](…)` target under `docs/` and `.agents/` exists |

Before a milestone, run the gate on macOS and `tools/check_linux.sh
--skip-current-fixed-point`, plus `tools/run_chibi_suite.sh --native`.

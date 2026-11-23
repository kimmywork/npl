# Test Infrastructure

Status: accepted direction, 2026-11-23. Increments 1-3 implemented.

## Goal

Move test orchestration and expect checks into NPL, remove append-only shell
lists, and provide unit and property-test APIs that normal packages can use.

## Decisions

- `(npl test)` is the unit API, compatible with SRFI 64 and aliased as
  `(srfi 64)`. It keeps the historical `finish-tests` output so existing
  compiler suites migrate without changing every call.
- `(npl test property)` follows SRFI 252 and aliases `(srfi 252)`.
  Deterministic random values come from `property-seed`; generators are
  SRFI 158-shaped thunks.
- `(npl test expect)` is the expect runner. `fixture.meta` remains the
  fixture description; `tests/fixtures/manifest.scm` is the only fixture
  list.
- `tools/run-tests.npl` is the gate orchestrator; `run_tests.sh` and
  `run_r7rs_fixture.sh` are thin compatibility wrappers.
- SRFI 269-style first-class test definitions are a later layer. The current
  implementation follows SRFI 64's immediate model because compatibility
  with the existing suites is the priority; the manifest provides discovery
  independent of execution.
- SRFI 78 `check` is optional and not implemented yet; SRFI 64/252 cover the
  current unit and PBT needs.

## Increments

1. **Unit framework (done 2026-11-23).** `lib/npl/test.sld`,
   `lib/npl/test/core.scm`, `(srfi 64)`, `framework.npl` compatibility,
   explicit imports for importless suites, all compiler suites pass.
2. **Property testing (done 2026-11-23).** `lib/npl/test/property.sld`,
   `(srfi 252)`, generators, `tests/property/core.npl`.
3. **Expect runner and manifest (done 2026-11-23).**
   `lib/npl/test/expect.sld`, `tools/expect-runner.npl`,
   `tools/run-tests.npl`, `tests/fixtures/manifest.scm`; fixture directories
   moved from `tests/r7rs/fixtures` to `tests/fixtures`.
4. **Compiler suite manifest (next).** Replace the `run_suite` lines and
   expected counts in `check_full_compiler.sh` with a Scheme manifest and an
   NPL runner; keep C-extension/SSA probes as typed adapters.
5. **First-class definitions (optional).** Add SRFI 269-style `test`,
   `testing`, `suite`, and `metadata` entities on top of the runner; use it
   for filtering and re-running subsets without changing test bodies.
6. **Optional SRFI 78.** Add a `check`/`check-report` compatibility layer if
   external suites need it.
7. **Unify remaining adapters.** Port stage0/bootstrap/driver/install/GC
   checks where NPL can express the assertions; stage0 and its checks now
   live under `bootstrap/stage0/`; keep shell only for bootstrapping and
   container-specific setup.

## Verification

- `tools/check_full_compiler.sh`, `tools/check_repl.sh`, and the NPL gate
  pass.
- `tools/run-tests.npl --skip-legacy` discovers all fixtures from
  `tests/fixtures/manifest.scm` and passes.
- `tools/run-tests.npl --properties-only` runs `tests/property/*.npl`.
- `tools/run_r7rs_fixture.sh` delegates to the NPL expect runner.

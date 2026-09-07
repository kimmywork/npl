# Workstate

## Current

The uncommitted C4 integer substrate checkpoint has passed focused numeric/IO checks and the final-tree three-generation relay. It was stabilized in place at the user's request; do not revert it. `runtime/numeric.c` owns boxed integer representation, decimal conversion, and add/subtract/multiply/compare mechanisms. IO only delegates decimal recognition/construction and formatting and protects live output objects with existing GC roots. The focused substrate and decimal IO checks do not complete C4.1: compiler literals, arithmetic lowering, numeric predicates/equality, evaluator exposure, and public NPL validation/error policy still need integration.

After the current increment, the user requires evaluator/compiler semantic consistency before further standard-library increments. Prior readiness claims establish only the tested subset. Confirmed mismatches include `let*` and `letrec*`: the frontend emits core nodes 220/222 and the native backend handles them, but `eval-expression` has no corresponding branches and reports `unsupported core expression`. Probes also establish shared native/REPL 32-bit comparison truncation, and native static `pair?` header decoding generates a negative symbol relocation rejected by the local linker. The authoritative issue list is in `docs/plan-r7rs-conformance-guile.md` (CONS-01, NUM-01/02, REP-01, TOOL-01, PERF-01). Audit the shared core AST, bindings, control, and error contracts; repair by reusing existing frontend and execution boundaries, and consider reimplementation only if that audit justifies it. Acceptance requires shared positive/negative native/REPL fixtures and Guile comparisons where applicable, not only evaluator unit tests.

The public `(scheme eval)` core and a usable `(scheme load)` subset are implemented over the existing engine for native programs and the persistent REPL. The baseline implementation review and shared native/REPL/Guile fixture runner are complete; obsolete planning, audit, and historical `docs/` status artifacts have been removed, while the restored current R7RS plan is retained. B3 port-wrapper semantics are implemented for `call-with-port`, `call-with-input-file`, and `call-with-output-file`, including normal-return multiple-value forwarding and continuation escape behavior. The new `prune` skill defines independent pre-commit and branch-sync housekeeping. It may form provisional cleanup classifications when none exist, but requires user confirmation and persistence before destructive removal; it does not replace `worklog`.

The permanent bootstrap language and compiler remain frozen. The evolving full compiler has completed Dynamic Core, Reader/Datum, hygienic macros and derived forms, explicit-control execution, multiple values, multi-shot continuations, `dynamic-wind`, exceptions, parameters, promises, and the minimal R7RS library loader.

- `stage0/`: untyped C99 compiler with QBE and executable output.
- `bootstrap/compiler/`: permanent NPL bootstrap compiler.
- `src/compiler/`: evolving full compiler.
- `bootstrap/bootstrap.ssa`: permanent 6,753-line bootstrap fixed point.
- `bootstrap/current.ssa`: tracked 348,879-line full-compiler fixed point, SHA-256 `1c8b7d6a965a090e2d89497f47da420835923b4de58ec4e690217d9168f2ec91`.

Independent sources under `lib/` now include `(scheme eval)` and `(scheme load)` in addition to the first-wave `(scheme base)`, read/write, file/process-context, `cxr`, `char`, `case-lambda`, and lazy libraries. `(scheme eval)` exports `environment` and `eval`; each public environment has isolated frontend, library, macro, and evaluator dynamic state, honors import modifiers, rejects top-level mutation, and uses a private runtime library bridge in the REPL instead of self-interpreting `(npl evaluator)`.

The persistent REPL remains ready for the implemented language subset. `(npl frontend)` packages the compiler reader, hygienic expander, derived forms, library resolver, and core lowering as explicit sessions. `(npl evaluator)` executes the shared core AST and now stores wind, handler, and parameter state per evaluator environment. Common fixtures and dedicated native/REPL tests cover arithmetic, imported syntax, closure results, environment isolation, immutable bindings, hidden evaluator internals, import errors, and recovery after failure.

`(scheme load)` reads source files one datum at a time, evaluates forms sequentially, closes the input port at EOF or evaluator failure, and returns an unspecified value. The native bridge uses a persistent evaluator load environment; the REPL bridge uses the persistent interaction evaluator. The optional second environment-specifier form is not yet exposed.

Known `(scheme eval)` gaps remain explicit: the native wrapper cannot yet pass native procedure values into an evaluated closure or convert evaluator multiple-values objects back to native multiple values. Core syntactic forms are still recognized intrinsically rather than filtered entirely by an environment's import set. These do not block the implemented expression, macro, import, isolation, and ordinary closure workflows, but broad R7RS conformance must not be claimed until they are resolved.

## Evidence

- Current integer substrate: `build/runtime/numeric_stress` reports `numeric substrate: PASS (8368 checks)`, including fixnum boundaries, generated arithmetic, multi-limb identities, explicit GC survival, and memory-port growth. ASan/UBSan instrumentation of numeric and IO code passes the same checks.
- Freshly rebuilt native driver and REPL pass `integer-io` and `smoke` via `tools/run_r7rs_fixture.sh`, with identical stdout, stderr, and exit status against Guile. `integer-io` verifies decimal read/write only; its nested `let` bindings avoid the separately tracked `let*` gap without changing read order.
- Final-tree gen1/gen2/gen3 and the input current SSA are byte-identical at the tracked hash above. The measured relay passed: emission took 287/262/286 seconds; all reported stages total 902 seconds. The earlier 600-second timeout was insufficient, not evidence of a broken fixed point. No current full-suite PASS is claimed.
- Relay script focused tests pass success, compiler failure, empty output, generation drift, cancellation with child cleanup, and seed preservation. Progress/timing and failed-artifact retention are implemented without reducing the three-generation criterion. `docs/toolchain.md` assesses retaining recovery SSA and self-compilation checks while reducing duplicate daily runs; policy changes remain recommendations pending approval.

Previously recorded baseline evidence (not acceptance of the uncommitted numeric changes):

- Current gen1/gen2/gen3 are byte-identical at 348,879 lines, SHA-256 `ed6f3a59116ac86cff1dc24bbc07562470ab140b2dc5cb058e054cf66f93509e`.
- `./tools/run_tests.sh --skip-current-fixed-point` reports `all suites: PASS` after the accepted relay.
- `src/compiler/tests/eval.npl` reports `PASS eval 14` through native compilation.
- The native load fixture runs two source files sequentially and reports `42` from the second file.
- `./tools/check_repl.sh` reports `persistent NPL REPL: PASS`, including the public eval and load transcripts and the existing 42 focused evaluator checks.
- `tools/run_r7rs_fixture.sh tests/r7rs/fixtures/port-wrappers` reports native, persistent REPL, and Guile `--r7rs` PASS with identical stdout, stderr, and status; the fixture covers zero/one/multiple-value forwarding, automatic close, file wrappers, and continuation escape.
- `git diff --check` passes; `bootstrap/compiler/` and `bootstrap/bootstrap.ssa` are unchanged.

## Next

1. Execute the user-requested evaluator/compiler consistency work, beginning with a core AST and shared semantic boundary audit, nodes 220/222, comparison width, and boxed-header decoding. Use common fixtures; do not add parallel reader, macro, import, control, or multiple-values semantics. Fix bounded, testable problems directly and track larger refactors in the plan's issue list.
2. Resume C4.1 numeric integration and other standard-library work after that consistency boundary is accepted. The completed substrate checkpoint is not full language-level bignum support.
3. Obtain approval before reducing relay frequency/generation count or introducing caching. Retain current SSA and permanent bootstrap recovery; do not treat fixed-point equality as semantic conformance.

Keep one accepted fixed-point relay per exact compiler/runtime source tree. The current focused checks do not supersede the remaining integration or family acceptance gates.

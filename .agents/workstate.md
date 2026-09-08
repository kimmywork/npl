# Workstate

## Current

The bounded core consistency result is validated. Shared frontend normalization and evaluator support cover `let*`, `letrec*`, internal definitions, continuation zero/single/multiple values, 64-bit and variadic numeric comparisons, boxed header decoding, and evaluator identity tags. Native/REPL/Guile `core-bindings` passes, including seven malformed-form recovery cases. Primitive registry parity and complete arity/type/error contracts remain open; this is not full evaluator/compiler conformance or C4.1.

Exact-integer language integration is now validated separately: compiler and reader paths construct boxed integers through the shared numeric substrate; static literals, `number?`, `number->string`, numeric `eqv?`, exact arithmetic, comparisons, and public NPL numeric checks pass in native/REPL/Guile. Inexact/rational/complex number types remain unimplemented.



The strict three-generation relay was rerun after the core repair and exact-integer integration. The first run from the previous seed differed at gen1 and converged at gen2/gen3; a subsequent strict three-generation run seeded from that converged output passed. `bootstrap/current.ssa` is now 349,088 lines with SHA-256 `c24ab41a491ce103bdc88e9f39a18ca6ce8219ccb2d5e584fd3769370f175224`; `bootstrap/bootstrap.ssa` remains frozen. These seed transitions are intentional compiler artifact maintenance, not a reduction of the relay criterion.

The related native regression set passed: Dynamic Core, Reader/Datum, macros, derived forms, explicit control, multiple values, continuations, dynamic-wind, exceptions, guard, parameters, promises, and evaluator core (42 checks). The shared consistency checker passes with fresh native/REPL binaries. The full suite has not been rerun.

The public `(scheme eval)` core and a usable `(scheme load)` subset are implemented over the existing engine for native programs and the persistent REPL. The baseline implementation review and shared native/REPL/Guile fixture runner are complete; obsolete planning, audit, and historical `docs/` status artifacts have been removed, while the restored current R7RS plan is retained. B3 port-wrapper semantics are implemented for `call-with-port`, `call-with-input-file`, and `call-with-output-file`, including normal-return multiple-value forwarding and continuation escape behavior. The new `prune` skill defines independent pre-commit and branch-sync housekeeping. It may form provisional cleanup classifications when none exist, but requires user confirmation and persistence before destructive removal; it does not replace `worklog`.

The permanent bootstrap language and compiler remain frozen. The evolving full compiler has completed Dynamic Core, Reader/Datum, hygienic macros and derived forms, explicit-control execution, multiple values, multi-shot continuations, `dynamic-wind`, exceptions, parameters, promises, and the minimal R7RS library loader.

- `stage0/`: untyped C99 compiler with QBE and executable output.
- `bootstrap/compiler/`: permanent NPL bootstrap compiler.
- `src/compiler/`: evolving full compiler.
- `bootstrap/bootstrap.ssa`: permanent 6,753-line bootstrap fixed point.
`bootstrap/current.ssa` is now 349,088 lines, SHA-256 `c24ab41a491ce103bdc88e9f39a18ca6ce8219ccb2d5e584fd3769370f175224`. The previous seed transition was intentional and followed by a strict three-generation relay PASS.

Independent sources under `lib/` now include `(scheme eval)` and `(scheme load)` in addition to the first-wave `(scheme base)`, read/write, file/process-context, `cxr`, `char`, `case-lambda`, and lazy libraries. `(scheme eval)` exports `environment` and `eval`; each public environment has isolated frontend, library, macro, and evaluator dynamic state, honors import modifiers, rejects top-level mutation, and uses a private runtime library bridge in the REPL instead of self-interpreting `(npl evaluator)`.

The persistent REPL remains ready for the implemented language subset. `(npl frontend)` packages the compiler reader, hygienic expander, derived forms, library resolver, and core lowering as explicit sessions. `(npl evaluator)` executes the shared core AST and now stores wind, handler, and parameter state per evaluator environment. Common fixtures and dedicated native/REPL tests cover arithmetic, imported syntax, closure results, environment isolation, immutable bindings, hidden evaluator internals, import errors, and recovery after failure.

`(scheme load)` reads source files one datum at a time, evaluates forms sequentially, closes the input port at EOF or evaluator failure, and returns an unspecified value. The native bridge uses a persistent evaluator load environment; the REPL bridge uses the persistent interaction evaluator. The optional second environment-specifier form is not yet exposed.

Known `(scheme eval)` gaps remain explicit: the native wrapper cannot yet pass native procedure values into an evaluated closure or convert evaluator multiple-values objects back to native multiple values. Core syntactic forms are still recognized intrinsically rather than filtered entirely by an environment's import set. These do not block the implemented expression, macro, import, isolation, and ordinary closure workflows, but broad R7RS conformance must not be claimed until they are resolved.

## Evidence

- Current integer substrate: `build/runtime/numeric_stress` reports `numeric substrate: PASS (8368 checks)`, including fixnum boundaries, generated arithmetic, multi-limb identities, explicit GC survival, and memory-port growth. ASan/UBSan instrumentation of numeric and IO code passes the same checks.
- Freshly rebuilt native driver and REPL pass `integer-io` and `smoke` via `tools/run_r7rs_fixture.sh`, with identical stdout, stderr, and exit status against Guile. `integer-io` verifies decimal read/write only; its nested `let` bindings avoid the separately tracked `let*` gap without changing read order.
- The repaired consistency boundary passes `tools/check_core_consistency.sh`: native, persistent REPL, and Guile have identical expected results for the shared core-bindings fixture; seven malformed forms are rejected and the REPL recovers. The focused native regression set passes Dynamic Core, Reader/Datum, macros, derived forms, explicit control, multiple values, continuations, dynamic-wind, exceptions, guard, parameters, promises, and evaluator core (42 checks).
- Strict relay after the core repair and exact-integer integration: the first run from the previous seed produced converged gen2/gen3 but differed at gen1; the subsequent strict relay passed all three generations. Current SSA is 349,088 lines at SHA-256 `c24ab41a491ce103bdc88e9f39a18ca6ce8219ccb2d5e584fd3769370f175224`.
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

1. Continue bounded evaluator/compiler audit for primitive registry parity, arity/type/error contracts, and remaining evaluator gaps; do not claim full consistency from the core fixture alone.
2. Continue numeric work only into the next approved subtype or operation family. Exact-integer language integration is complete for the covered contract; inexact, rational, and complex numbers remain open.
3. Continue the primitive registry and arity/type/error audit without claiming full evaluator/compiler conformance.
4. Obtain approval before reducing relay frequency/generation count or introducing caching.

Keep one accepted fixed-point relay per exact compiler/runtime source tree. The current focused checks do not supersede the remaining integration or family acceptance gates.

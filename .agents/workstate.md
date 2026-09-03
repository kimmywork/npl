# Workstate

## Current

The public `(scheme eval)` core and a usable `(scheme load)` subset are implemented over the existing engine for native programs and the persistent REPL. A detailed full R7RS-small implementation, Guile differential-validation, and benchmark plan is written. The baseline implementation audit, binding surface snapshot, and minimal shared native/REPL/Guile smoke runner are complete. No new standard-library semantic increment is active: the attempted B3 port-wrapper increment is stopped at a concrete prerequisite gap.

The permanent bootstrap language and compiler remain frozen. The evolving full compiler has completed Dynamic Core, Reader/Datum, hygienic macros and derived forms, explicit-control execution, multiple values, multi-shot continuations, `dynamic-wind`, exceptions, parameters, promises, and the minimal R7RS library loader.

- `stage0/`: untyped C99 compiler with QBE and executable output.
- `bootstrap/compiler/`: permanent NPL bootstrap compiler.
- `src/compiler/`: evolving full compiler.
- `bootstrap/bootstrap.ssa`: permanent 6,753-line bootstrap fixed point.
- `bootstrap/current.ssa`: current 348,879-line full-compiler fixed point, SHA-256 `ed6f3a59116ac86cff1dc24bbc07562470ab140b2dc5cb058e054cf66f93509e`.

Independent sources under `lib/` now include `(scheme eval)` and `(scheme load)` in addition to the first-wave `(scheme base)`, read/write, file/process-context, `cxr`, `char`, `case-lambda`, and lazy libraries. `(scheme eval)` exports `environment` and `eval`; each public environment has isolated frontend, library, macro, and evaluator dynamic state, honors import modifiers, rejects top-level mutation, and uses a private runtime library bridge in the REPL instead of self-interpreting `(npl evaluator)`.

The persistent REPL remains ready for the implemented language subset. `(npl frontend)` packages the compiler reader, hygienic expander, derived forms, library resolver, and core lowering as explicit sessions. `(npl evaluator)` executes the shared core AST and now stores wind, handler, and parameter state per evaluator environment. Common fixtures and dedicated native/REPL tests cover arithmetic, imported syntax, closure results, environment isolation, immutable bindings, hidden evaluator internals, import errors, and recovery after failure.

`(scheme load)` reads source files one datum at a time, evaluates forms sequentially, closes the input port at EOF or evaluator failure, and returns an unspecified value. The native bridge uses a persistent evaluator load environment; the REPL bridge uses the persistent interaction evaluator. The optional second environment-specifier form is not yet exposed.

Known `(scheme eval)` gaps remain explicit: the native wrapper cannot yet pass native procedure values into an evaluated closure or convert evaluator multiple-values objects back to native multiple values. Core syntactic forms are still recognized intrinsically rather than filtered entirely by an environment's import set. These do not block the implemented expression, macro, import, isolation, and ordinary closure workflows, but broad R7RS conformance must not be claimed until they are resolved.

## Evidence

- Current gen1/gen2/gen3 are byte-identical at 348,879 lines, SHA-256 `ed6f3a59116ac86cff1dc24bbc07562470ab140b2dc5cb058e054cf66f93509e`.
- `./tools/run_tests.sh --skip-current-fixed-point` reports `all suites: PASS` after the accepted relay.
- `src/compiler/tests/eval.npl` reports `PASS eval 14` through native compilation.
- The native load fixture runs two source files sequentially and reports `42` from the second file.
- `./tools/check_repl.sh` reports `persistent NPL REPL: PASS`, including the public eval and load transcripts and the existing 42 focused evaluator checks.
- `git diff --check` passes; `bootstrap/compiler/` and `bootstrap/bootstrap.ssa` are unchanged.

## Next

1. Resolve the stopped B3 decision: `call-with-port` needs an existing multiple-values-preserving dynamic-apply boundary; do not approximate it with `dynamic-wind` or add an unapproved runtime/control redesign.
2. After that decision, select one approved semantic increment from the audited dependency graph; do not redo the completed B2 control/exception/multiple-values baseline.
3. Use `tools/run_r7rs_fixture.sh` for the minimum native/REPL/Guile comparison required by that increment.
4. Add benchmarks only when the relevant semantic family is complete or its performance-relevant implementation changes; run no unrelated validation.

Use one fixed-point relay per exact compiler/runtime source tree, then run the remaining gate with `--skip-current-fixed-point`.

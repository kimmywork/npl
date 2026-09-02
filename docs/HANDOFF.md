# NPL Implementation Handoff

Status: active handoff as of 2026-09-03 02:14 CST.

## Immediate Directive

Continue R7RS-small product delivery from the public `(scheme eval)` increment. The evaluator was not made self-hosting; instead, a private `(npl eval-runtime)` bridge connects the public library to the existing engine in both native programs and the persistent REPL. Keep the extraction limited to the session/API boundary already required by public environments.

The immediate order is:

1. implement `(scheme load)` using ports and shared source/frontend loading;
2. establish and execute the number-tower increment explicitly rather than extending evaluator internals;
3. proceed through time, Unicode, and compatibility work without adding unrelated facilities.

Follow root `AGENTS.md`: reuse before reinvention, NPL-first implementation, differential evidence for compiler/evaluator behavior, frozen permanent bootstrap, and proportional verification.

## Repository State

- Branch: `master`.
- Canonical full compiler: `bootstrap/current.ssa`, 348,879 lines, SHA-256 `ed6f3a59116ac86cff1dc24bbc07562470ab140b2dc5cb058e054cf66f93509e`.
- `bootstrap/compiler/` and `bootstrap/bootstrap.ssa` are unchanged and must remain frozen.
- Dynamic control, promises, and practical REPL recovery coverage are committed; the public `(scheme eval)` increment is the current logical change.

Latest verified commands:

```text
./tools/check_repl.sh
persistent NPL REPL: PASS

src/compiler/tests/eval.npl
PASS eval 14

./tools/run_tests.sh --skip-current-fixed-point
all suites: PASS

./tools/update_current_ssa.sh
current compiler fixed point: PASS
```

The compiler frontend changed only to distinguish normal library bound-call checking from the stricter atomic-identifier checking used by public eval environments. The accepted relay is byte-identical across gen1/gen2/gen3 at the line count and hash above.

## What Is Shared

`lib/npl/frontend.sld` directly includes and reuses:

```text
src/compiler/common/base.npl
src/compiler/reader/lexer.npl
src/compiler/reader/datum.npl
src/compiler/frontend/derived.npl
src/compiler/frontend/syntax.npl
src/compiler/frontend/expand.npl
src/compiler/frontend/forms.npl
src/compiler/frontend/source.npl
src/compiler/frontend/library.npl
```

This provides one implementation of reader/datum handling, hygienic `syntax-rules`, derived forms, binding expansion, imports, library loading, and core AST lowering. Frontend state is now captured in explicit sessions so multiple eval environments do not overwrite each other's value, macro, syntax-mark, or library registry state. Do not add evaluator-specific macro, import, library, binding, or surface-lowering implementations.

Important boundary: `lib/npl/frontend.sld` does **not** include `src/compiler/frontend/control.npl`. The evaluator currently receives pre-CPS core AST from `lower-program`; it does not execute the compiler's `control-program` output.

## Recorded Deviation

The agreed direction was to evaluate reuse of compiler CPS lowering before implementing dynamic control separately. The current dirty increment did not do that. It added evaluator-owned wind transition, handler, parameter, and `letrec` behavior directly in `lib/npl/evaluator-core.scm`.

This duplicated part of the semantic algorithm already represented by:

- `src/compiler/frontend/control.npl`;
- `runtime/control.c`;
- the native control test suites.

The deviation happened because the work treated a shared control IR/CPS adaptation as excessive scope and chose a local evaluator implementation. That decision conflicted with the stronger reuse-first expectation and increased `lib/npl/evaluator-core.scm` to 924 lines.

The evaluator still contains the previously accepted parallel control-state machinery. The public eval increment did not add another control implementation: it scoped existing wind, handler, and parameter globals into evaluator sessions and added a narrow engine API for imported environments and closure invocation. Do not stop product delivery for a broad control rewrite, but do preserve this boundary and avoid new semantic duplication.

## Completed Increments

`cb2ebb9 feat(evaluator): add dynamic control` added evaluator-side support for:

- continuation snapshots of wind, handler, and parameter state;
- wind exit and re-entry ordering;
- target handler restoration before re-entry `before` thunks;
- `dynamic-wind` normal return, continuation exit, and multi-shot re-entry;
- `with-exception-handler`, `raise`, and `raise-continuable`;
- parameter objects, converter calls, mutation, dynamic binding, and continuation re-entry;
- core tag 221 (`letrec`), required by the existing shared `parameterize` macro;
- vector operations required by that macro.

`c51f412 feat(evaluator): add promise support` added evaluator promise vectors with the native `DONE`, `DELAY`, `CHAIN`, and `REDIRECT` state contract. It dispatches the shared `npl_promise_op` operations, treats a promise as a zero-source-argument callable, memoizes final values, preserves recursive forcing behavior, observes active evaluator parameter/handler state during first force, and uses tail-style chain traversal. Focused evaluator coverage is now 38 checks and includes the ten native promise scenarios, including the million-element `delay-force` chain.

`6e50632 feat(repl): cover interactive library recovery` expanded focused evaluator coverage to 42 checks and added real persistent REPL transcripts for `except`, `prefix`, combined modifiers, library includes, exported macros, cache reuse, malformed `define-syntax`, unknown import names, library cycles, and session survival after each failure. It also converts common arithmetic, pair, symbol, and vector-index primitive misuse into `eval-error` values, so normal REPL use no longer exits through those underlying operations.

The public `(scheme eval)` increment adds source libraries `(npl eval-runtime)` and `(scheme eval)`. Native code reuses `(npl evaluator)` through a thin NPL wrapper; the REPL registers the same private library name as a host bridge, so it does not recursively interpret the evaluator implementation. Public environments are immutable after import, have isolated frontend/library/macro and dynamic evaluator sessions, honor import modifiers, hide evaluator internals, and continue working after evaluation or import errors. `src/compiler/tests/eval.npl` and `src/repl/tests/eval.*` cover both paths.

Known limitations that must remain visible:

- native wrappers for evaluator closures accept ordinary data arguments, but cannot yet translate native procedure arguments into evaluator-callable values;
- evaluator multiple-values objects are not yet converted back into native multiple values by `(npl eval-runtime)`;
- core syntactic forms are still recognized intrinsically, so import sets do not yet control every syntax binding with full R7RS precision;
- evaluator continuations currently enforce one argument rather than the already-supported native multiple-value continuation contract;
- primitive type/range errors are not uniformly converted to recoverable evaluator errors; the practical REPL coverage currently guarantees arithmetic, comparison, pair, symbol, and vector-index cases;
- the evaluator contains parallel control-state machinery that must be reconsidered later, but not during the next delivery increments.

## Promise Contract

The shared derived forms lower as follows in `src/compiler/frontend/derived.npl`:

```text
make-promise -> (npl_promise_op 0 value)
promise?     -> (npl_promise_op 1 value)
delay         -> (npl_promise_op 2 thunk)
delay-force   -> (npl_promise_op 3 thunk)
force         -> call the promise returned by operation 0
```

The evaluator implementation follows `runtime/control.c` and `src/compiler/tests/promises.npl`; it adds no promise macro or alternate public API. Its acceptance evidence is the three commands above, all passing without compiler/runtime changes or a fixed-point relay.

## Practical REPL Result

The practical REPL is ready for the implemented language subset. `build/bin/npl-repl` persists definitions and macros, uses the shared compiler frontend for all reader, expansion, import, library, and lowering behavior, and survives reader, frontend, and covered evaluator errors. `./tools/check_repl.sh` verifies the standard library from an external CWD through the injected absolute root, native/evaluator differential behavior, interactive library modifiers/includes/exported macros/cache behavior, frontend failure recovery, and common recoverable primitive failures.

This is not full R7RS-small availability. Public `(scheme eval)` now supports environment construction, imported expression/macro evaluation, ordinary closure results, isolated sessions, immutable bindings, and native/REPL error recovery. The crossing and syntax-visibility limitations above remain, while `(scheme load)`, the full numeric tower, Unicode completion, time/process completion, and broader primitive-error normalization are separate increments.

## Remaining R7RS-Small Flow

After practical REPL readiness, advance the existing plan in dependency order. Keep each increment limited to required R7RS behavior and exercise it through both native driver and REPL paths.

1. Implement `(scheme load)` using ports and shared source/frontend loading.
2. Resolve the remaining public eval crossing gaps without self-interpreting `(npl evaluator)`: native procedure arguments, native multiple-value conversion, and exact syntax-binding visibility.
3. Complete the numeric tower in an explicit representation/runtime/compiler increment: bignum promotion, exact rationals, complex and inexact behavior as required by the accepted R7RS scope.
4. Complete `(scheme time)` and remaining process-context behavior.
5. Complete the accepted Unicode/character/string conformance beyond the current ASCII boundary.
6. Add `(scheme r5rs)` only as a compatibility export layer for semantics that are actually implemented.
7. Maintain an explicit binding/behavior inventory; a resolving library name is not evidence of conformance.

For every shared compiler/evaluator behavior, use common fixtures or differential tests. Do not mark R7RS generally available until the required library inventory and behavioral gates are complete.

## Prohibited Scope

Until the user explicitly changes direction:

- do not introduce a new control IR or switch the evaluator to compiler CPS output;
- do not migrate `runtime/control.c` semantics to NPL;
- do not extract, split, or rewrite evaluator control code beyond a concrete API or correctness requirement;
- do not add evaluator-specific reader, macro, import, binding, derived-form, library, or lowering logic;
- do not make `(npl evaluator)` self-hosting or add `npl_lex_*` primitives for that purpose;
- do not implement package management, version solving, dynamic loading, precompiled module caches, JIT, or performance optimization;
- do not modify `bootstrap/compiler/` or `bootstrap/bootstrap.ssa`;
- do not regenerate canonical SSA unless compiler/runtime source changed and the exact tree passes a fixed-point relay;
- do not discard or silently rewrite user worktree changes.

## Verification Discipline

For evaluator/library-only increments:

```bash
./tools/check_repl.sh
git diff --check
./tools/run_tests.sh --skip-current-fixed-point
```

For compiler/runtime source changes, perform one fixed-point relay for the exact source tree, accept the canonical SSA only after gen1/gen2/gen3 byte equality, then use `--skip-current-fixed-point` for the remaining gates.

Before committing:

- inspect the complete dirty diff;
- confirm permanent bootstrap files are unchanged;
- report any intentional semantic duplication;
- update `.agents/worklog/YYYY-MM-DD.md` and `.agents/workstate.md`;
- use a Conventional Commit message.

## Key Files

```text
AGENTS.md
PLAN.md
docs/plan-stdlib-driver-repl.md
.agents/workstate.md
lib/npl/frontend.sld
lib/npl/eval-runtime.sld
lib/npl/evaluator.sld
lib/npl/evaluator-core.scm
lib/scheme/eval.sld
lib/scheme/repl.sld
src/compiler/frontend/expand.npl
src/compiler/frontend/control.npl
src/compiler/frontend/derived.npl
runtime/control.c
src/compiler/tests/promises.npl
src/evaluator/core-tests.npl
src/repl/main.npl
src/repl/tests/differential-fixture.scm
tools/check_repl.sh
tools/run_tests.sh
```

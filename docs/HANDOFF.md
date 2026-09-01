# NPL Implementation Handoff

Status: active handoff as of 2026-09-01 17:33 CST.

## Immediate Directive

Continue R7RS-small product delivery from the clean worktree after `6e50632 feat(repl): cover interactive library recovery`. The practical REPL is ready for the implemented language subset. Do not restart the evaluator architecture, introduce a control IR, migrate `runtime/control.c` to NPL, or extract/refactor the evaluator before the user explicitly reopens that discussion.

The immediate order is:

1. implement the public `(scheme eval)` environment API over the shared persistent frontend/evaluator engine;
2. implement `(scheme load)` using ports and shared source/frontend loading;
3. proceed through the remaining R7RS-small work without adding unrelated facilities.

Follow root `AGENTS.md`: reuse before reinvention, NPL-first implementation, differential evidence for compiler/evaluator behavior, frozen permanent bootstrap, and proportional verification.

## Repository State

- HEAD: `6e50632 feat(repl): cover interactive library recovery`.
- Branch: `master`.
- Canonical full compiler: `bootstrap/current.ssa`, 348,700 lines, SHA-256 `1df230fac643aabf7259b881371a07a40d6b1f43d2eb4ee715eaee2a0964beb5`.
- `bootstrap/compiler/` and `bootstrap/bootstrap.ssa` are unchanged and must remain frozen.
- Dynamic control, promises, and practical REPL recovery coverage are committed; the worktree was clean before this handoff alignment update.

Latest verified commands:

```text
./tools/check_repl.sh
persistent NPL REPL: PASS

./tools/run_tests.sh --skip-current-fixed-point
all suites: PASS

git diff --check
PASS (no output)
```

The current native/evaluator differential output is:

```text
(42 42 42 42 42 42 42123 42123 42 42 4212 85 42 42)
```

No compiler or runtime source changed in these evaluator/library-only increments, so no new fixed-point relay was required.

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

This provides one persistent implementation of reader/datum handling, hygienic `syntax-rules`, derived forms, binding expansion, imports, library loading, and core AST lowering. Do not add evaluator-specific macro, import, library, binding, or surface-lowering implementations.

Important boundary: `lib/npl/frontend.sld` does **not** include `src/compiler/frontend/control.npl`. The evaluator currently receives pre-CPS core AST from `lower-program`; it does not execute the compiler's `control-program` output.

## Recorded Deviation

The agreed direction was to evaluate reuse of compiler CPS lowering before implementing dynamic control separately. The current dirty increment did not do that. It added evaluator-owned wind transition, handler, parameter, and `letrec` behavior directly in `lib/npl/evaluator-core.scm`.

This duplicated part of the semantic algorithm already represented by:

- `src/compiler/frontend/control.npl`;
- `runtime/control.c`;
- the native control test suites.

The deviation happened because the work treated a shared control IR/CPS adaptation as excessive scope and chose a local evaluator implementation. That decision conflicted with the stronger reuse-first expectation and increased `lib/npl/evaluator-core.scm` to 924 lines.

Do not repeat or conceal this deviation. Also do not stop current delivery to repair it now. The user explicitly decided to finish promises and R7RS work using the current evaluator approach, then discuss evaluator extraction separately.

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

Known limitations that must remain visible:

- evaluator dynamic state is library-global and therefore suitable for the current singleton REPL, not yet multiple independent public `(scheme eval)` environments;
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

This is not full R7RS-small availability. Public `(scheme eval)`, `(scheme load)`, the full numeric tower, Unicode completion, time/process completion, and broader primitive-error normalization remain separate increments.

## Remaining R7RS-Small Flow

After practical REPL readiness, advance the existing plan in dependency order. Keep each increment limited to required R7RS behavior and exercise it through both native driver and REPL paths.

1. Implement the full public `(scheme eval)` environment API over the same persistent frontend/evaluator engine. Sessionize dynamic state only when this requirement makes multiple environments concrete; discuss evaluator extraction with the user before doing broader control refactoring.
2. Implement `(scheme load)` using ports and shared source/frontend loading.
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
- do not extract, split, or rewrite evaluator control code merely to reduce the 924-line file;
- do not add evaluator-specific reader, macro, import, binding, derived-form, library, or lowering logic;
- do not implement package management, version solving, dynamic loading, precompiled module caches, JIT, or performance optimization;
- do not modify `bootstrap/compiler/` or `bootstrap/bootstrap.ssa`;
- do not regenerate canonical SSA for evaluator/library-only changes;
- do not discard or silently rewrite the current dirty increment.

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
lib/npl/evaluator.sld
lib/npl/evaluator-core.scm
lib/scheme/repl.sld
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

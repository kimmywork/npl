# NPL Implementation Handoff

Status: active handoff as of 2026-09-01 15:00 CST.

## Immediate Directive

Continue product delivery from the current dirty worktree. Do not restart the evaluator architecture, introduce a control IR, migrate `runtime/control.c` to NPL, or extract/refactor the 924-line evaluator before the user explicitly reopens that discussion.

The immediate order is:

1. accept and commit the current evaluator dynamic-control increment after focused verification;
2. complete evaluator promises using the existing promise contract;
3. close interactive library and recoverable frontend failure coverage;
4. declare the practical REPL ready for the implemented language surface;
5. proceed through the remaining R7RS-small work without adding unrelated facilities.

Follow root `AGENTS.md`: reuse before reinvention, NPL-first implementation, differential evidence for compiler/evaluator behavior, frozen permanent bootstrap, and proportional verification.

## Repository State

- HEAD: `8bd5603 feat(repl): share compiler frontend semantics`.
- Branch: `master`, aligned with `origin/master` before the dirty increment.
- Canonical full compiler: `bootstrap/current.ssa`, 348,700 lines, SHA-256 `1df230fac643aabf7259b881371a07a40d6b1f43d2eb4ee715eaee2a0964beb5`.
- `bootstrap/compiler/` and `bootstrap/bootstrap.ssa` are unchanged and must remain frozen.
- The current dirty worktree is intentional. Do not revert it.

Dirty files at handoff:

```text
.agents/worklog/2026-09-01.md
.agents/workstate.md
PLAN.md
docs/plan-stdlib-driver-repl.md
lib/npl/evaluator-core.scm
src/evaluator/core-tests.npl
src/repl/tests/differential-fixture.scm
tools/check_repl.sh
```

This handoff document and its discovery references are additional documentation-only changes.

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
(42 42 42 42 42 42 42123 42123 42 42 4212)
```

No compiler or runtime source changed in the dirty increment, so no new fixed-point relay was required.

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

## Current Dirty Increment

`lib/npl/evaluator-core.scm` now contains evaluator-side support for:

- continuation snapshots of wind, handler, and parameter state;
- wind exit and re-entry ordering;
- target handler restoration before re-entry `before` thunks;
- `dynamic-wind` normal return, continuation exit, and multi-shot re-entry;
- `with-exception-handler`, `raise`, and `raise-continuable`;
- parameter objects, converter calls, mutation, dynamic binding, and continuation re-entry;
- core tag 221 (`letrec`), required by the existing shared `parameterize` macro;
- vector operations required by that macro.

Focused evaluator coverage increased from 17 to 28 checks. It includes normal wind order, wind exit/re-entry, non-continuable raise across wind, nested/continuable handlers, handler restoration before re-entry `before`, parameter conversion/restoration, and parameter continuation re-entry.

Known limitations that must remain visible:

- evaluator dynamic state is library-global and therefore suitable for the current singleton REPL, not yet multiple independent public `(scheme eval)` environments;
- evaluator continuations currently enforce one argument rather than the already-supported native multiple-value continuation contract;
- primitive type/range errors are not uniformly converted to recoverable evaluator errors;
- promise behavior is not implemented in the evaluator;
- the evaluator contains parallel control-state machinery that must be reconsidered later, but not during the next delivery increments.

## Next Increment: Promises

Do not invent a new promise surface. The shared derived forms already lower as follows in `src/compiler/frontend/derived.npl`:

```text
make-promise -> (npl_promise_op 0 value)
promise?     -> (npl_promise_op 1 value)
delay         -> (npl_promise_op 2 thunk)
delay-force   -> (npl_promise_op 3 thunk)
force         -> call the promise returned by operation 0
```

Use the existing behavior as the contract:

- `runtime/control.c`: `PROMISE_DONE`, `PROMISE_DELAY`, `PROMISE_CHAIN`, `PROMISE_REDIRECT`, `npl_promise_op`, and `npl_force`;
- `runtime/control.c`: `npl_tail_request_closure` treats a promise as a zero-argument callable after CPS adds its continuation;
- `src/compiler/tests/promises.npl`: ten positive native tests, including memoization, recursive forcing, dynamic parameter/handler interaction, and a million-element `delay-force` chain.

Implement only the evaluator representation and dispatch required by those already-shared forms:

1. add an evaluator promise value/state representation;
2. add evaluator handling for `npl_promise_op` operations 0 through 3;
3. make evaluator promise application with zero source arguments perform `force`;
4. preserve memoization and redirect/chaining semantics exactly;
5. ensure the first force observes the active evaluator parameter and handler state;
6. use continuation/tail-style iteration so the million-link `delay-force` contract does not consume host stack;
7. return evaluator errors instead of terminating the REPL.

Reuse `src/compiler/tests/promises.npl` as the behavior inventory. Add focused evaluator tests and extend the existing `src/repl/tests/differential-fixture.scm`; do not create a second promise macro or alternate public API.

Acceptance for the promise increment:

```bash
./tools/check_repl.sh
git diff --check
./tools/run_tests.sh --skip-current-fixed-point
```

Do not modify or relay `bootstrap/current.ssa` unless compiler/runtime source actually changes.

## Then Close Practical REPL

Use the existing shared frontend/library implementation. Do not add evaluator import logic.

Add focused interactive coverage for:

- `except`, `prefix`, and combined import modifiers;
- library includes and exported macros;
- library cache behavior and cycle/unknown-library errors;
- external-CWD operation through the injected absolute stdlib root;
- malformed `define-syntax`, unsupported imports, and failed library expansion;
- proof that failed expansion does not corrupt prior value or macro session state;
- required core AST tags and primitives encountered by real standard-library workflows;
- recoverable primitive arity/type failures needed for normal REPL use.

Relevant shared APIs:

```text
frontend-expand-datum
library-resolve-import-set
library-import-sets
library-load
with-frontend-errors
```

Practical REPL readiness means the implemented language subset works persistently through `build/bin/npl-repl`, survives reader/frontend/evaluator errors, and matches native observable behavior on common fixtures. Do not call this full R7RS-small GA.

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

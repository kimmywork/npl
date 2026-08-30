# Steps 3-4 Execution Plan

Status: complete. The acceptance criteria below were implemented and verified through the current compiler relay.

## Outcome

Step 3 adds the R7RS hygienic macro system and defines the remaining derived forms through that system. Step 4 replaces escape-only control with unlimited-extent, multi-shot continuations and builds multiple values, dynamic state, exceptions, parameters, and promises on the same control model.

The implementation must preserve the permanent recovery chain:

```text
bootstrap compiler -> full compiler seed -> canonical current SSA -> restored full compiler
```

Consequently, `src/compiler/` itself must remain within the permanent bootstrap compiler's source subset. Programs compiled by the full compiler may use Steps 3 and 4; the compiler implementation may not depend on those features to build itself.

## Fixed Boundaries

- Do not change `bootstrap/compiler/` or `bootstrap/bootstrap.ssa`.
- Keep macros declarative: R7RS `syntax-rules`, not procedural transformers.
- Keep macro bindings within the current program and recursive `include` source graph. Library import/export scoping is implemented separately in Step 5.
- Keep the existing direct runtime ABI available for permanent bootstrap artifacts.
- Do not implement stack-copying continuations or extend `setjmp` escape frames into a claimed full `call/cc`.
- Do not add threads, delimited continuations, Unicode expansion, types, or tensor behavior in these steps.

## Architecture Decisions

### Expansion pipeline

The current reader recognizes special forms while parsing and immediately emits numeric AST tags. Step 3 changes the full frontend to three explicit phases:

```text
raw syntax -> hygienic expansion -> core tagged AST -> QBE lowering
```

Identifiers in raw syntax carry a spelling and a fresh binding identity. Pattern substitutions retain use-site identities. Template identifiers retain definition-site identities, while binders introduced by a template receive fresh identities for each expansion. Literal identifiers compare binding identity, or spelling when both are unbound, as required by R7RS 4.3.2.

The `syntax-rules` matcher records captures by repetition path, rather than flattening repeated values. That representation is required for nested ellipses and for detecting incompatible repetition depths during template transcription.

Core derived transformers are stored as quoted transformer data understood by the expander. They are not written as active macros in compiler implementation code, because the permanent bootstrap compiler must still compile that code.

### Control lowering

The existing `npl_call_cc` uses `setjmp` frames and rejects a continuation after its defining C frame returns. It remains an escape-only compatibility path and cannot satisfy R7RS unlimited extent or multi-shot invocation.

Full-compiler output will use a second, explicit-control ABI:

- generated procedures receive a hidden continuation;
- calls return control to a runtime trampoline instead of nesting the native C stack;
- compiler-generated continuations and captured user continuations are GC-managed heap objects;
- continuation invocation schedules the captured continuation and ignores the caller's continuation;
- argument/value arrays are dynamically sized rather than limited by `NPL_MAX_CALL_ARGS`;
- the old `NplGenericFunction` ABI remains for the permanent bootstrap compiler.

A persistent dynamic state accompanies the control trampoline. It contains independent wind, exception-handler, and parameter-binding chains. Captured continuations retain this state. A continuation transfer computes wind-chain divergence, runs exiting `after` thunks from inner to outer, then entering `before` thunks from outer to inner, before installing the target state.

This is preferred over native stack copying because stack copying would depend on C/QBE frame layout, complicate conservative GC, and remain architecture-specific.

## Step 3: Macros and Derived Forms

### 3.1 Raw syntax and expansion boundary

Result: existing programs pass through a separate no-op expansion phase before core AST lowering.

Actions:

- Add raw form parsing without early dispatch on identifier spelling.
- Add syntax identifiers and fresh binding identities for core syntax, lexical variables, and top-level bindings.
- Split current form parsing into expansion and core-AST lowering modules.
- Preserve quoted datum, quasiquote, include ordering, internal definitions, and existing diagnostics.

Verification:

- Existing Dynamic Core and Reader/Datum suites are unchanged.
- Shadowing a core keyword as a variable is distinguished from the core syntax binding where R7RS permits it.
- Bootstrap and current compiler fixed points reproduce through the recovery chain.

Complete when the phase split changes no existing language result and raw syntax never reaches QBE lowering.

### 3.2 Basic `syntax-rules` and hygiene

Result: top-level `define-syntax` supports ordered, fixed-cardinality rewrite rules.

Actions:

- Parse and validate `define-syntax`, `syntax-rules`, `_`, literal identifiers, constants, proper/improper list patterns, and vector patterns.
- Implement left-to-right rule matching and template transcription without ellipsis.
- Preserve use-site identifiers and definition-site free identifiers.
- Freshen template-introduced binders and their references.
- Expand `syntax-error` during expansion.

Verification:

- Tests cover rule order, constants, wildcard matching, literal identifiers, free-identifier reference, introduced temporary names, use-site shadowing, improper lists, and vectors.
- Negative fixtures cover malformed rules, duplicate pattern variables, non-identifier literals, no matching rule, and explicit `syntax-error`.

Complete when the standard temporary-capture and definition-site-reference hygiene examples pass.

### 3.3 Ellipsis

Result: R7RS repetition works at arbitrary nested pattern/template depth.

Actions:

- Add zero-or-more matching with fixed prefix/suffix patterns.
- Track pattern-variable captures by repetition path.
- Transcribe repeated list, improper-list, and vector templates.
- Support nested ellipses, a custom ellipsis identifier, and `(ellipsis template)` escaping.
- Reject mismatched repetition depth and templates whose output cardinality cannot be determined.

Verification:

- Tests cover zero, one, and many matches; nested repetitions; multiple variables at the same depth; suffix patterns; vectors; improper lists; custom ellipsis; and emitted literal ellipses.
- Negative fixtures cover misplaced ellipses, inconsistent repetition lengths, and illegal pattern-variable depth.

Complete when nested and custom-ellipsis examples reproduce the expected expanded behavior without capture.

### 3.4 Syntactic scopes

Result: global, local, and internal syntax bindings have R7RS regions.

Actions:

- Implement top-level and internal `define-syntax` ordering.
- Implement `let-syntax` and `letrec-syntax`, including their distinct transformer-definition regions.
- Allow macros to expand into definitions and splice top-level/body `begin` forms where definitions are permitted.
- Detect duplicate syntax bindings and use-before-internal-definition errors.

Verification:

- Tests cover local shadowing, recursive local macros, internal syntax definitions, macro-generated definitions, and restoration of an outer transformer.
- Negative fixtures cover duplicate keywords, invalid definition placement, and premature keyword use.

Complete when syntax binding identity, not spelling alone, determines transformer selection.

### 3.5 Derived forms

Result: `and`, `or`, `when`, `unless`, `do`, complete `cond`, and complete `case` are installed through `syntax-rules` transformer data.

Actions:

- Replace direct `cond`/`case` parsing with hygienic transformers.
- Implement `cond` test-only and `=>` clauses with single test evaluation.
- Implement `case` single key evaluation, `=>` and `else =>`, duplicate-datum rejection, and `eqv?` comparison for the currently supported datum types.
- Implement short-circuit value-preserving `and`/`or`, sequencing forms, and `do` default-step/result behavior.

Verification:

- A `derived-forms.npl` suite covers evaluation order, single evaluation, non-boolean result preservation, `=>`, local bindings named `else`/`=>`, `do` iteration, omitted steps, and unspecified-result paths without asserting a particular unspecified value.
- Negative fixtures cover misplaced `else`, malformed `=>`, duplicate case data, and malformed `do` bindings.

Complete when all listed forms are ordinary hygienic transformers and no dedicated parser dispatch remains for them.

## Step 4: Control and Exceptions

### 4.1 Explicit-control backend foundation

Result: all full-compiler target programs use the new continuation ABI while preserving existing behavior.

Actions:

- Add GC-scanned control closure/continuation representations and a control trampoline.
- CPS-lower expressions, sequencing, calls, global initialization, and program entry.
- Preserve the old closure/trampoline ABI for bootstrap artifacts.
- Replace fixed-size control argument storage with GC-safe dynamically sized storage.

Verification:

- All existing suites pass, including ten million mutual tail calls and GC stress.
- The permanent bootstrap still links against the same runtime.
- The full compiler seed, canonical current SSA, and restored compiler are byte-identical at the fixed-point gates.

Complete when no generated full-compiler user call depends on the native C call stack for its continuation.

### 4.2 Multiple values

Result: continuations carry zero or more values and enforce context arity.

Actions:

- Implement `values` and `call-with-values` as control operations.
- Give ordinary expression continuations one-value arity and discarded sequence positions arbitrary-value arity.
- Add `define-values`, `let-values`, and `let*-values` through Step 3 transformers.
- Preserve tail position from producer through consumer.

Verification:

- Tests cover zero, one, and many values; fixed and dotted formals; nested calls; discarded values; evaluation order; and tail calls.
- Negative fixtures cover value-count/formals mismatches.

Complete when multiple values are not encoded as a user-visible list/vector and survive tail calls unchanged.

### 4.3 Unlimited-extent `call/cc`

Result: `call-with-current-continuation` and `call/cc` produce multi-shot continuations with unlimited extent.

Actions:

- Capture the current heap continuation and dynamic state.
- Implement continuation procedures that accept the value count expected by the captured continuation.
- On invocation, abandon the current continuation and schedule the captured one.
- Retire escape-only behavior from full-compiler routing while retaining its bootstrap compatibility symbol.

Verification:

- Tests cover immediate escape, invocation after the defining call returns, storage in data/closures, repeated invocation, nested continuations, mutation observed across re-entry, and multiple-value continuations.

Complete when a continuation can be invoked repeatedly after its capture call has returned, without native stack restoration.

### 4.4 `dynamic-wind`

Result: continuation transfers preserve dynamic extent ordering.

Actions:

- Add persistent wind frames with unique identity, parent, `before`, and `after` thunks.
- Implement normal entry/exit and continuation transfer through the common control scheduler.
- Compute the common wind ancestor and schedule exiting `after` thunks before entering `before` thunks.

Verification:

- Tests cover normal order, nested order, jumps out, jumps in, repeated re-entry, and the R7RS connect/talk/disconnect example.

Complete when wind behavior is identical for normal return and arbitrary continuation transfer where R7RS specifies it.

### 4.5 Exceptions and `guard`

Result: dynamic exception handlers support continuable and non-continuable raises.

Actions:

- Add persistent handler frames to dynamic state.
- Implement `with-exception-handler`, `raise`, and `raise-continuable` with the specified previous-handler environment.
- Define `guard` and its auxiliary transformer through `syntax-rules`.
- Route uncaught exceptions to a deterministic runtime diagnostic and nonzero exit.

Verification:

- Tests cover handler installation/restoration, continuable return values, non-continuable secondary raise, nested handlers, re-raise, continuation transfer through handlers, `guard` test/`=>`/`else`, and wind ordering during raise.

Complete when handlers follow dynamic state across continuation exit and re-entry.

### 4.6 Parameters

Result: parameter objects provide continuation-safe dynamic bindings.

Actions:

- Add parameter identities, converters, and persistent parameter bindings to dynamic state.
- Implement one- and two-argument `make-parameter`.
- Define `parameterize` through `syntax-rules`, evaluating parameter/value expressions before installing bindings and restoring bindings through dynamic state transitions.

Verification:

- Tests cover conversion, nested rebinding, restoration, independent parameters, continuation escape/re-entry, and parameter reads inside handlers and wind thunks.

Complete when invoking a captured continuation restores the parameter bindings captured with it.

### 4.7 Promises

Result: `delay`, `delay-force`, `force`, `promise?`, and `make-promise` implement memoized lazy evaluation.

Actions:

- Add a GC-scanned promise state or an equivalent private runtime representation.
- Define `delay` and `delay-force` through `syntax-rules`.
- Memoize exactly once while handling recursive forcing according to the R7RS update algorithm.
- Make `delay-force` chains execute through the control trampoline without growing native or control stacks.

Verification:

- Tests cover one-time evaluation, mutation after memoization, recursive forcing/update, `make-promise` idempotence, `promise?`, forcing a non-promise under the chosen permitted behavior, and a million-link `delay-force` chain.

Complete when lazy chains are space-safe and first-force dynamic parameters/handlers determine evaluation.

## Gate for Every Subincrement

1. Add a focused positive suite and malformed/runtime contract fixtures proportional to the change.
2. Run the suite through the compiler restored from `bootstrap/current.ssa`.
3. Regenerate `bootstrap/current.ssa` through the full-seed recovery chain.
4. Restore the regenerated compiler and require byte-identical SSA reproduction.
5. Run `./tools/run_tests.sh` and `git diff --check`.
6. Confirm no diff under `bootstrap/compiler/` or `bootstrap/bootstrap.ssa`.
7. Record evidence in the worklog before the conventional commit.

## Primary Evidence

- R7RS 4.3.1-4.3.3: syntax bindings, `syntax-rules`, hygiene, ellipsis, and `syntax-error` in `docs/reference/r7rs.html`.
- R7RS 5.4: top-level and internal `define-syntax` regions.
- R7RS 6.10: unlimited-extent continuations, multiple values, and `dynamic-wind`.
- R7RS 6.11 and 4.2.7: exception handler and `guard` dynamic environments.
- R7RS 4.2.5-4.2.6: promises and parameters.
- R7RS 7.3: reference `syntax-rules` definitions for derived forms.
- Current escape-only implementation: `runtime/alloc.c`, section `Escape-only continuations`.

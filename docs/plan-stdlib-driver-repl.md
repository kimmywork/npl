# Standard Libraries, Compiler Driver, and REPL Plan

Status: proposed for execution.

This plan covers the next three user-facing capabilities after the minimal library system:

1. R7RS-small standard libraries and their runtime substrate.
2. A one-command native compilation driver for `.npl` programs.
3. A persistent Scheme REPL.

The plan also defines when existing `src/compiler/` modules may be rewritten using the new library code.

## Invariants

- `bootstrap/compiler/` and `bootstrap/bootstrap.ssa` remain permanently frozen.
- `bootstrap/current.ssa` remains the only checked-in recovery artifact for the evolving full compiler. No compiler binary, QBE assembly, or object file is committed.
- Every implementation increment is compiled by the previous current compiler, then verified through gen1/gen2/gen3 byte-identical relay recovery.
- The compiler implementation may use only libraries that the previous compiler can already load and compile. A standard library must not depend on compiler implementation files.
- Standard libraries are checked-in `.sld` sources under `lib/scheme/`, with explicit exports and imports from the private `(npl primitive)` intrinsic surface. Their implementation and export changes must not alter compiler SSA unless the primitive registry, language semantics, loader, or lowering changes.
- R7RS semantics are the source contract. NPL-specific process and native-build operations are separate extensions, not silently added to `(scheme base)`.
- No package manager, library version solver, dynamic library loader, precompiled module cache, or cyclic library support is added.

## Scope And Order

Delivery is product-first:

1. Complete a broad first-wave R7RS dependency closure for native compilation and interactive evaluation.
2. Deliver the explicit three-stage driver and its NPL wrapper.
3. Deliver the persistent evaluator-backed REPL core and `(scheme repl)`.
4. Return to the remaining R7RS-small conformance surface using driver and REPL workflows as acceptance fixtures.

The first wave is intentionally broad but not presented as full conformance. Numeric tower completion, full `eval`/`load`, and compatibility layers must not delay the first usable compiler command and interaction environment.

### Step 6.1: Port And Runtime Substrate

Result: a small, explicit runtime API for text and binary ports, standard streams, files, and process context.

Actions:

- Define the port value representation and GC scanning rules.
- Implement current input/output/error ports, open/close, flush, textual reads/writes, binary reads/writes, EOF, and file errors.
- Add command-line arguments, environment access where supported, and process exit status through `(scheme process-context)`.
- Keep OS calls in `runtime/`; keep R7RS behavior and argument validation in NPL library code.
- Add the non-standard `(npl process)` primitive only for argv-based child-process execution and status reporting. It must not execute a shell command string.

Verification:

- Focused port tests cover stdin/stdout redirection, EOF, close/flush, file creation, overwrite, append, missing files, binary bytes, and invalid port operations.
- GC stress retains open ports and buffered values across collection.
- Negative contracts check wrong types, closed ports, invalid modes, and child-process failures.
- Existing full compiler, runtime, and bootstrap gates remain green.

Complete when ports and process context work from a compiler restored from `bootstrap/current.ssa`, and no process primitive is exported by an R7RS library.

### Step 6.2: First-Wave R7RS Libraries

Result: the standard library provides the practical dependency closure needed by the native driver and persistent REPL, with explicit source-based export surfaces.

First-wave libraries and substrate:

- `(scheme base)`: the implemented booleans, pairs/lists, symbols, numbers, characters, strings, vectors, bytevectors, control, exceptions, parameters, promises, core syntax, string ports, and bytevector ports.
- `(scheme cxr)`: the twenty-four composite car/cdr accessors.
- `(scheme case-lambda)`: `case-lambda` syntax.
- `(scheme char)`: ASCII character predicates, comparisons, and case conversion.
- `(scheme write)` and `(scheme read)`: transcript-capable datum I/O.
- `(scheme file)` and `(scheme process-context)`: the file/process operations needed by tools.
- `(scheme lazy)`: the already-supported promise surface.
- Private `(npl process)`: argv-based child process execution for the NPL driver, never shell command strings.

Explicitly deferred until after driver/REPL core:

- exact rational, bignum, complex, and broader inexact numeric semantics;
- complete `(scheme eval)`, `(scheme load)`, `(scheme time)`, and `(scheme r5rs)`;
- Unicode expansion beyond the current ASCII repertoire;
- exhaustive R7RS edge-case and compatibility conformance.

Actions:

- Represent standard libraries as checked-in `.sld` sources with explicit exports. Keep only the private `(npl primitive)` intrinsic binding registry in the compiler; do not embed `(scheme ...)` source or export manifests in compiler SSA.
- Reuse runtime primitives where they are already correct; implement list, string, vector, bytevector, and character algorithms in NPL where that improves portability.
- Ensure import modifiers and macro exports work identically for standard and user libraries.
- Preserve static literal immutability and the current ASCII reader boundary until Unicode support is separately planned.

Verification:

- Each library gets a focused positive suite and malformed/arity/type contracts.
- A binding inventory is checked against the R7RS reference in `docs/reference/r7rs.html`.
- Tests prove that an unimported standard binding is not accidentally available in a library or program.
- Macro exports, `cond-expand`, `include-ci`, and modifier combinations are tested with standard libraries as well as user libraries.

Complete when the implemented subset is explicit, tested through restored full compilers, and the remaining unimplemented standard bindings are listed rather than implied complete.

### Step 7.1: Explicit Three-Stage Driver

Result: one command compiles a source file to a native executable while retaining `--emit-qbe` as an inspection mode.

The driver pipeline is:

```text
source.npl
  -> nplc --emit-qbe -> temporary source.ssa
  -> qbe              -> temporary source.s
  -> cc + runtime     -> requested executable
```

Actions:

- Extend the stable tool entry point so `nplc source.npl -o output` performs all three stages.
- Keep the raw compiler emitter available as `nplc --emit-qbe source.npl` for canonical SSA and relay tests.
- Use temporary directories, argv-based subprocesses, cleanup traps, propagated exit status, and paths that may contain spaces.
- Make runtime archive and QBE paths explicit build configuration rather than relying on the caller's `PATH`.
- Keep the current `tools/build_nplc.sh` path for restoring the compiler binary from `bootstrap/current.ssa`; add a separate build/check path for the user-facing driver if the names would otherwise collide.

Verification:

- `hello.npl -o hello` is compared against the existing explicit SSA -> QBE -> `cc` sequence.
- Test arithmetic, libraries, static data, runtime initialization, non-zero program results, compiler errors, QBE errors, linker errors, output paths with spaces, and cleanup after failure.
- Verify `--emit-qbe` output remains byte-for-byte unchanged for the current compiler fixed point.
- Add an end-to-end command to the default gate without storing generated binaries.

Complete when a user can compile and link a program with one command and every stage's failure is observable and correctly propagated.

### Step 7.2: NPL Driver Wrapper

Result: the orchestration wrapper is itself an NPL program, compiled to a native executable by the existing compiler.

Actions:

- Implement a small `src/driver/` program using file and process libraries. It invokes the raw compiler, QBE, and `cc` with argument vectors, not shell interpolation.
- Define the minimal `(npl process)` interface needed by the wrapper and keep it outside R7RS standard libraries.
- Pass the runtime archive, QBE executable, C compiler, target, and output path as explicit driver configuration.
- Keep a small shell bootstrap wrapper until the NPL wrapper can rebuild itself.

Verification:

- Build the NPL wrapper from the compiler restored from `bootstrap/current.ssa`, then use that wrapper to compile the same programs as Step 7.1.
- Compare generated SSA and executable behavior with the shell driver.
- Rebuild the wrapper through the historical relay and verify deterministic output.
- Test process exit propagation and cleanup under compiler, QBE, and linker failure.

### Step 8.1: Persistent REPL Core

Status: in progress. The NPL interaction loop and persistent core evaluator now consume the existing compiler frontend through private `(npl frontend)`: compiler reader, hygienic `syntax-rules`, derived forms, library/import resolution, and core lowering are shared rather than reimplemented. Persistent definitions, mutation, closures, recursion, macros, `only`/`rename` imports, multiple values, `call/cc`, `dynamic-wind`, exceptions, parameters, continuation-aware dynamic state, prompts/EOF, value printing, and recoverable evaluator/reader errors are implemented. Promises and broader interactive library coverage are still required for completion.

Result: a native `npl-repl` process reads, evaluates, and prints multiple forms while preserving definitions between inputs.

Implementation choice: use a persistent interpreter/evaluator, not repeated whole-program recompilation. Recompiling a growing temporary source file cannot provide correct interactive state, error recovery, or practical latency.

Actions:

- Extract or reuse reader functionality behind a runtime-facing API that reads one datum at a time from a port.
- Reuse the compiler's reader, hygienic macro expander, import resolver, derived-form expansion, and core lowering through a persistent frontend session; the evaluator interprets the resulting common core AST.
- Keep the evaluator backend responsible only for persistent runtime environments and core AST execution, without parallel macro/import semantics.
- Add prompts, EOF handling, `(exit)`/process termination, printed results, error recovery to the next input, and standard streams.
- Define the initial interaction environment through `(scheme repl)` and explicit library imports.
- Keep compiler-only QBE lowering out of the first REPL implementation. A later optimizing evaluator or JIT is not a prerequisite.

Verification:

- Golden transcript and focused evaluator tests cover literals, arithmetic, definitions, mutation, closures, recursive definitions, shared macros/imports, multiple values, `dynamic-wind`, exceptions, parameters, continuation-aware dynamic-state restoration, recoverable errors, and EOF; promises remain pending.
- A separate transcript proves state persists across ten or more inputs and that an error does not destroy prior definitions.
- Redirected stdin tests prove the REPL is deterministic without a TTY.
- Test output formatting, prompt behavior, non-zero exits, and no leaked open ports.
- Compare REPL evaluation with compiled execution for the same expression fixtures.

Complete when the REPL behaves as a persistent Scheme interaction environment for the implemented language subset, with explicit limitations recorded for unsupported `eval`, numeric, Unicode, or implementation-specific features.

The REPL must use the shared evaluator engine intended for `(scheme eval)`. Full public `eval` conformance is deferred, but the evaluator itself is not: persistent environments, lexical evaluation, macro expansion, imports, control behavior, and recoverable errors are REPL-core requirements.

### Step 9.1: Remaining R7RS-small Conformance

This work starts only after the three-stage driver and persistent REPL core are usable. The driver and REPL become primary acceptance surfaces for the remaining standardization work.

Result: complete the public `(scheme eval)` environment API and the remaining R7RS-small libraries in dependency order:

- `(scheme complex)` and `(scheme inexact)` with the numeric operations supported by the numeric representation.
- `(scheme load)` on top of ports and the source loader.
- remaining `(scheme process-context)` and `(scheme time)` behavior;
- full `(scheme eval)` environment construction and conformance over the shared evaluator engine;
- `(scheme repl)` interaction-environment exports;
- `(scheme r5rs)` as a compatibility export layer only where semantics are actually implemented.

The numeric tower is a separate increment when exact rationals, complex values, and bignum promotion are ready. A library is not marked complete merely because its name resolves.

Verification:

- Use per-library conformance fixtures, cross-library import tests, and negative contracts.
- Compare observable results and errors with the R7RS reference behavior for the supported numeric and port domains.
- Exercise the remaining APIs through both native programs and persistent REPL transcripts.
- Run the entire existing suite after every library increment.

## Opportunistic Compiler Rewrites

Compiler rewrites may be inserted only between the numbered increments above. They are module-level replacements, not a second compiler architecture.

### Good Rewrite Candidates

1. Pure list/string/path helpers in `src/compiler/common/`, after equivalent standard-library functions have focused tests.
2. Reader utilities, after one-datum port reading and error objects are stable.
3. Library and expansion helpers, after standard-library source exports and macro import tests are stable.
4. Backend collection/state helpers, only when the new library version removes real duplication or fixes a measured defect.

For each rewrite:

- preserve the old behavior with golden SSA or semantic fixture tests;
- replace one module at a time and run the full compiler gate;
- regenerate and compare current gen1/gen2/gen3;
- do not alter the permanent bootstrap sources or ABI;
- keep the rewrite only if it reduces code, removes a known limitation, or enables a required feature.

### Rewrite Candidates To Defer

Do not rewrite the CPS/control backend, runtime ABI, or reader representation merely for style. Their current fixed points are high-cost and heavily validated; change them only for a concrete semantic or performance requirement.

## Verification Protocol

Every increment must produce all of the following evidence:

1. Focused tests for the increment, including negative contracts and boundary cases.
2. `./tools/check_bootstrap.sh` for the permanent and current recovery paths.
3. `./tools/check_current_fixed_point.sh` with byte-identical gen1/gen2/gen3 output.
4. `./tools/check_full_compiler.sh` for all language suites and contracts.
5. `./tools/run_tests.sh` for the repository-wide gate.
6. `git diff --check` and a check that `bootstrap/compiler/` and `bootstrap/bootstrap.ssa` are unchanged.
7. For executable features, QBE and `cc` end-to-end tests from a compiler restored from `bootstrap/current.ssa`.
8. A recorded SSA line count and SHA-256 for every accepted `bootstrap/current.ssa` update.

No increment is complete when only the emitter or only a hand-built binary has been tested.

## Exit Criteria

The plan is complete when:

- the implemented R7RS-small libraries have explicit, tested export surfaces;
- `nplc source.npl -o output` performs SSA emission, QBE lowering, and native linking in one command;
- the NPL driver wrapper can rebuild and perform that same pipeline;
- `npl-repl` preserves a Scheme interaction environment and recovers from errors;
- opportunistic compiler rewrites are backed by semantic and fixed-point evidence;
- all permanent and current recovery invariants remain green.

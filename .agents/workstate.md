# Workstate

## Current

Active implementation handoff: `docs/HANDOFF.md`. It records the intentional dirty worktree, the evaluator/CPS reuse deviation, the no-refactor boundary, and the required promises -> practical REPL -> remaining R7RS flow.

The permanent bootstrap language and compiler are frozen. The evolving full compiler has completed Dynamic Core, Reader/Datum, hygienic macros and derived forms, explicit-control execution, multiple values, multi-shot continuations, `dynamic-wind`, exceptions, parameters, promises, and the minimal R7RS library loader.

- `stage0/`: untyped C99 compiler with QBE and executable output.
- `bootstrap/compiler/`: permanent NPL bootstrap compiler.
- `src/compiler/`: evolving full compiler.
- `bootstrap/bootstrap.ssa`: permanent 6,753-line bootstrap fixed point.
- `bootstrap/current.ssa`: current 348,700-line full-compiler fixed point, SHA-256 `1df230fac643aabf7259b881371a07a40d6b1f43d2eb4ee715eaee2a0964beb5`.

The first-wave standard-library closure for driver/REPL is complete. Independent sources under `lib/` provide `(scheme base)`, `(scheme read)`, `(scheme write)`, `(scheme file)`, `(scheme process-context)`, `(scheme cxr)`, `(scheme char)`, `(scheme case-lambda)`, `(scheme lazy)`, and private `(npl process)`. Ports support textual/binary files, string/bytevector memory ports, bulk binary I/O, and single-datum read/write. The compiler accepts explicit `--stdlib-root` configuration.

The native three-stage driver is implemented in `src/driver/main.npl`: `build/bin/nplc source.npl -o output` performs raw SSA emission, QBE assembly generation, and native linking, while `--emit-qbe` remains available. `tools/build_nplc.sh` is only the minimal C/host bootstrap that restores canonical raw compiler SSA, injects absolute paths, and compiles the NPL driver. Toolchain and stdlib paths are explicit and overridable, and driver failures clean their isolated temporary directories.

The persistent REPL core is in progress under the NPL-first and reuse-first rules. `(npl frontend)` directly packages the existing compiler reader, hygienic `syntax-rules` expander, derived-form expansion, library/import resolver, and core lowering for persistent interaction sessions. `(npl evaluator)` executes that shared core AST with persistent lexical environments, definitions, mutation, closures, recursion, core forms, primitive procedures, multiple values, evaluator-level `call/cc`, `dynamic-wind`, exceptions, parameters, and continuation restoration across wind, handler, and parameter state; it does not contain a second macro or import implementation. `src/repl/main.npl` provides prompts, EOF/exit, value printing, and recovery from evaluator and malformed-reader errors. Persistent top-level macros, hygienic expansion, derived syntax, and `only`/`rename` imports are verified. Remaining work is promises plus broader import/library coverage.

Project-wide agent instructions now require reuse before reinvention and NPL-first implementation. New evaluator/REPL work must identify and extract shared compiler frontend semantics instead of growing parallel macro, import, binding, or core-form implementations; C remains limited to runtime, OS/FFI boundaries, and minimal host bootstrap.


## Evidence

- Current gen1/gen2/gen3 are byte-identical at 348,700 lines, SHA-256 `1df230fac643aabf7259b881371a07a40d6b1f43d2eb4ee715eaee2a0964beb5`.
- `./tools/run_tests.sh --skip-current-fixed-point` reports `all suites: PASS`.
- `./tools/check_repl.sh` passes 28 evaluator checks, persistent macro/import transcripts from an external CWD, prompt/EOF and evaluator/reader error recovery, and byte-identical evaluator/native differential output `(42 42 42 42 42 42 42123 42123 42 42 4212)`.
- A compiler launched from `/tmp` can compile a standard-library program with explicit `--stdlib-root`.
- GC stress passes one million allocations; permanent bootstrap sources and SSA remain unchanged.

## Next

Deliver a concrete usable product before further R7RS conformance expansion:

1. Reuse the existing promise state-machine contract to add evaluator `make-promise`, `promise?`, `delay`, `delay-force`, and `force` behavior.
2. Broaden shared interactive library coverage and negative import/error recovery fixtures without adding evaluator-specific import logic.
3. Wrap the same frontend session and core evaluator with full public `(scheme eval)` after REPL completion.
4. Return to remaining R7RS-small numeric, `load`, time, Unicode, and compatibility conformance using native driver and REPL workflows.
5. Measure compiler phases and consider output-path optimization only after driver/REPL completion.

Use one fixed-point relay per exact compiler/runtime source tree, then run the remaining gate with `--skip-current-fixed-point`.

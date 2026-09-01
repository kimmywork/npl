# Workstate

## Current

The permanent bootstrap language and compiler are frozen. The evolving full compiler has completed Dynamic Core, Reader/Datum, hygienic macros and derived forms, explicit-control execution, multiple values, multi-shot continuations, `dynamic-wind`, exceptions, parameters, promises, and the minimal R7RS library loader.

- `stage0/`: untyped C99 compiler with QBE and executable output.
- `bootstrap/compiler/`: permanent NPL bootstrap compiler.
- `src/compiler/`: evolving full compiler.
- `bootstrap/bootstrap.ssa`: permanent 6,753-line bootstrap fixed point.
- `bootstrap/current.ssa`: current 347,532-line full-compiler fixed point, SHA-256 `0be8fc68e99ad8f209b1c3171df96d2d754692c21fbf8d88addcd7d9fd27d050`.

The first-wave standard-library closure for driver/REPL is complete. Independent sources under `lib/` provide `(scheme base)`, `(scheme read)`, `(scheme write)`, `(scheme file)`, `(scheme process-context)`, `(scheme cxr)`, `(scheme char)`, `(scheme case-lambda)`, `(scheme lazy)`, and private `(npl process)`. Ports support textual/binary files, string/bytevector memory ports, bulk binary I/O, and single-datum read/write. The compiler accepts explicit `--stdlib-root` configuration.

The native three-stage driver is implemented in `src/driver/main.npl`: `build/bin/nplc source.npl -o output` performs raw SSA emission, QBE assembly generation, and native linking, while `--emit-qbe` remains available. `tools/build_nplc.sh` is only the minimal C/host bootstrap that restores canonical raw compiler SSA, injects absolute paths, and compiles the NPL driver. Toolchain and stdlib paths are explicit and overridable, and driver failures clean their isolated temporary directories.

The persistent REPL core is now in progress under the same NPL-first rule. `(npl evaluator)` is a shared NPL library with persistent lexical environments, definitions, mutation, closures, recursion, core forms, primitive procedures, multiple values, and evaluator-level `call/cc`. `src/repl/main.npl` implements prompts, EOF/exit, value printing, and recovery from evaluator and malformed-reader errors. C only exposes `symbol?` and structured recoverable datum-read boundaries required by the NPL engine. Hygienic macros, complete import behavior, and remaining exception/parameter/promise semantics are not yet complete.


## Evidence

- Current gen1/gen2/gen3 are byte-identical at 347,829 lines, SHA-256 `8ca387dfc379f379cbb7b331ff4b55e7beda4d79a04c7fc4a693eef3f3c86b26`.
- `./tools/run_tests.sh --skip-current-fixed-point` reports `all suites: PASS`.
- `./tools/check_repl.sh` passes shared evaluator semantics, persistent transcript, prompt/EOF behavior, and evaluator/reader error recovery.
- A compiler launched from `/tmp` can compile a standard-library program with explicit `--stdlib-root`.
- GC stress passes one million allocations; permanent bootstrap sources and SSA remain unchanged.

## Next

Deliver a concrete usable product before further R7RS conformance expansion:

1. Complete persistent hygienic macro expansion and import-set behavior in `(npl evaluator)`.
2. Add evaluator exception, parameter, promise, and dynamic control semantics required by the REPL contract.
3. Expose `(scheme repl)`, then wrap the same engine with full public `(scheme eval)` after REPL completion.
4. Return to remaining R7RS-small numeric, `load`, time, Unicode, and compatibility conformance using native driver and REPL workflows.
5. Measure compiler phases and consider output-path optimization only after driver/REPL completion.

Use one fixed-point relay per exact compiler/runtime source tree, then run the remaining gate with `--skip-current-fixed-point`.

# Workstate

## Current

The permanent bootstrap language and compiler are frozen. The evolving full compiler has completed Dynamic Core, Reader/Datum, hygienic macros and derived forms, explicit-control execution, multiple values, multi-shot continuations, `dynamic-wind`, exceptions, parameters, promises, and the minimal R7RS library loader.

- `stage0/`: untyped C99 compiler with QBE and executable output.
- `bootstrap/compiler/`: permanent NPL bootstrap compiler.
- `src/compiler/`: evolving full compiler.
- `bootstrap/bootstrap.ssa`: permanent 6,753-line bootstrap fixed point.
- `bootstrap/current.ssa`: current 347,532-line full-compiler fixed point, SHA-256 `0be8fc68e99ad8f209b1c3171df96d2d754692c21fbf8d88addcd7d9fd27d050`.

The first-wave standard-library closure for driver/REPL is complete. Independent sources under `lib/` provide `(scheme base)`, `(scheme read)`, `(scheme write)`, `(scheme file)`, `(scheme process-context)`, `(scheme cxr)`, `(scheme char)`, `(scheme case-lambda)`, `(scheme lazy)`, and private `(npl process)`. Ports support textual/binary files, string/bytevector memory ports, bulk binary I/O, and single-datum read/write. The compiler accepts explicit `--stdlib-root` configuration.

The native three-stage driver is usable: `tools/build_nplc.sh` restores `build/bin/nplc-raw` and builds `build/bin/nplc`; native mode emits SSA, invokes QBE, and links with the runtime, while `--emit-qbe` remains available. Toolchain and stdlib paths are explicit and overridable, and driver failures clean their isolated temporary directories.

The hygienic macro engine now correctly handles nested dotted patterns whose atomic symbol tail is used as a binding identifier. The root cause was repeated matching treating pair-backed datum symbols as list structure; focused regressions cover nested dotted capture and binding transcription.

## Evidence

- Current gen1/gen2/gen3 are byte-identical when linked with the final runtime.
- `./tools/run_tests.sh --skip-current-fixed-point` reports `all suites: PASS`.
- `./tools/check_driver.sh` passes native output, raw SSA equality, path-space, external-CWD, failure propagation, and cleanup checks.
- A compiler launched from `/tmp` can compile a standard-library program with explicit `--stdlib-root`.
- GC stress passes one million allocations; permanent bootstrap sources and SSA remain unchanged.

## Next

Deliver a concrete usable product before further R7RS conformance expansion:

1. Implement the NPL orchestration wrapper using `(npl process)` and explicit stdlib/toolchain configuration.
2. Build the shared persistent evaluator engine and native REPL core; full public `(scheme eval)` wraps this engine later.
3. Return to remaining R7RS-small numeric, `eval`/`load`, time, Unicode, and compatibility conformance using native driver and REPL workflows.
4. Measure compiler phases and consider output-path optimization only after driver/REPL completion.

Use one fixed-point relay per exact compiler/runtime source tree, then run the remaining gate with `--skip-current-fixed-point`.

# Workstate

## Current

The permanent bootstrap language and compiler are frozen. The evolving full compiler has completed Dynamic Core, Reader/Datum, hygienic macros and derived forms, explicit-control execution, multiple values, multi-shot continuations, `dynamic-wind`, exceptions, parameters, promises, and the minimal R7RS library loader.

- `stage0/`: untyped C99 compiler with QBE and executable output.
- `bootstrap/compiler/`: permanent NPL bootstrap compiler.
- `src/compiler/`: evolving full compiler.
- `bootstrap/bootstrap.ssa`: permanent 6,753-line bootstrap fixed point.
- `bootstrap/current.ssa`: current 344,774-line full-compiler fixed point, SHA-256 `b6ffa3d40256f7b46cbbe4ad4acf7d44aeba4ccc3e4d12b6446666c909ab1382`.

The runtime is split by responsibility: `alloc.c` owns allocation and GC state, `value.c` owns value operations, `control.c` owns explicit control and dynamic state, `io.c` owns ports/external I/O, and `lexer.c` owns bootstrap lexer hooks. Ports support textual files, binary files, memory bytevectors, optional-port character/byte operations, bulk bytevector I/O, EOF, stream-aware display/write, and ASCII single-datum read. Unused SIMD code is deferred until the tensor CPU-optimization phase; live collector root discovery remains in `gc.c`.

## Evidence

The accepted relay has byte-identical gen1/gen2/gen3 output. GC stress passes one million allocations. `./tools/run_tests.sh --skip-current-fixed-point` reports `all suites: PASS`; permanent bootstrap sources and SSA remain unchanged.

Stable toolchain entry points:

- `tools/check_current_fixed_point.sh` verifies the evolving full compiler relay.
- `tools/update_current_ssa.sh` installs a verified candidate as `bootstrap/current.ssa`.
- `tools/build_nplc.sh` restores `build/bin/nplc` from `bootstrap/current.ssa`.

## Next

Before adding more R7RS APIs, separate standard libraries from compiler implementation:

1. Keep only a private intrinsic/core binding surface in the compiler.
2. Move `(scheme base)`, `(scheme read)`, `(scheme write)`, and `(scheme file)` to tracked `lib/scheme/*.sld` sources.
3. Make standard-library resolution deterministic without embedding standard source manifests in compiler SSA.
4. Prove that an implementation-only standard-library edit does not change canonical compiler SSA.

Then complete string ports and process context, implement the remaining R7RS-small libraries, add the three-stage native driver, and build the persistent evaluator-backed REPL. Use one fixed-point relay per exact source tree, then run the remaining gate with `--skip-current-fixed-point`.

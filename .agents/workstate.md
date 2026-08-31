# Workstate

## Current

The permanent bootstrap language and compiler are frozen. The evolving full compiler has completed Dynamic Core, Reader/Datum, hygienic macros and derived forms, explicit-control execution, multiple values, multi-shot continuations, `dynamic-wind`, exceptions, parameters, promises, and the minimal R7RS library loader.

- `stage0/`: untyped C99 compiler with QBE and executable output.
- `bootstrap/compiler/`: permanent NPL bootstrap compiler.
- `src/compiler/`: evolving full compiler.
- `bootstrap/bootstrap.ssa`: permanent 6,753-line bootstrap fixed point.
- `bootstrap/current.ssa`: current 345,584-line full-compiler fixed point, SHA-256 `d751742f9f8124e4a7a013181bbf0cd8429115e46306e6c36d7405a462ec5640`.

Standard libraries are independent tracked sources under `lib/scheme/`. The compiler embeds only the private `(npl primitive)` intrinsic/derived-syntax registry and standard-library namespace/path policy. Current sources provide `(scheme base)`, `(scheme read)`, `(scheme write)`, `(scheme file)`, and the implemented `command-line`/`exit` subset of `(scheme process-context)`. Library-mode expansion requires explicit value and macro imports; standard implementations and exports are not ambient compiler bindings.

The modular runtime supports textual files, binary files, memory bytevectors, optional-port character/byte operations, bulk bytevector I/O, EOF, stream-aware display/write, and ASCII single-datum read. Unused SIMD code is deferred until the tensor CPU-optimization phase; live collector root discovery remains in `gc.c`.

## Evidence

- The accepted relay has byte-identical gen1/gen2/gen3 output.
- The same compiler source emitted against normal `lib/` and a copied, modified `lib/scheme/write.sld` is byte-identical.
- `./tools/run_tests.sh --skip-current-fixed-point` reports `all suites: PASS`.
- The full compiler gate also passes when launched outside the repository root.
- GC stress passes one million allocations; permanent bootstrap sources and SSA remain unchanged.

## Next

Continue [`docs/plan-stdlib-driver-repl.md`](../docs/plan-stdlib-driver-repl.md) in dependency order:

1. Complete Step 6.1 with textual string ports, remaining process context/environment behavior, and focused contracts.
2. Add independent `(scheme cxr)`, `(scheme case-lambda)`, `(scheme char)`, and remaining supported R7RS-small libraries under `lib/scheme/`.
3. Add the explicit source -> SSA -> QBE -> native three-stage driver and NPL wrapper, including an explicit installed stdlib root.
4. Build the persistent evaluator-backed REPL.
5. Measure compiler phases and consider output-path optimization only after driver/REPL completion.

Use one fixed-point relay per exact compiler source tree, then run the remaining gate with `--skip-current-fixed-point`.

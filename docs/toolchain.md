# Compiler Toolchain

The repository stores compiler source and canonical QBE SSA, not a compiler binary. The permanent bootstrap compiler and the evolving full compiler have separate recovery roles.

## Verify The Relay

`bootstrap/current.ssa` is the current full-compiler recovery artifact. Verify the historical relay with:

```bash
./tools/check_current_fixed_point.sh
```

The command builds a compiler from `bootstrap/current.ssa`, emits `gen1.ssa`, rebuilds from gen1, emits gen2, rebuilds from gen2, emits gen3, and requires all three SSA files to be byte-identical. It prints the canonical line count and SHA-256. To retain relay artifacts:

```bash
./tools/check_current_fixed_point.sh --keep --work-dir /tmp/npl-fixed-point
```

The current artifact is 347,532 lines with SHA-256 `0be8fc68e99ad8f209b1c3171df96d2d754692c21fbf8d88addcd7d9fd27d050`.

## Update Current SSA

After changing `src/compiler/`, regenerate and install the canonical artifact with:

```bash
./tools/update_current_ssa.sh
```

This first runs the complete gen1/gen2/gen3 comparison. Only after it succeeds does it replace `bootstrap/current.ssa`; no assembly, object, or binary is stored in Git.

## Build Nplc

Build the raw compiler from canonical SSA and the native three-stage driver with:

```bash
./tools/build_nplc.sh
```

The outputs are:

```text
build/bin/nplc-raw   raw NPL -> QBE SSA emitter
build/bin/nplc       source -> SSA -> QBE assembly -> native driver
```

Compile a native program in one command:

```bash
build/bin/nplc program.npl -o program
```

Raw SSA inspection remains available and preserves the emitter's successful status `44`:

```bash
build/bin/nplc --emit-qbe program.npl > program.ssa
```

The driver uses argv-based child processes, an isolated temporary directory, and explicit configured paths for the raw compiler, QBE, C compiler, runtime archive, and standard library. These may be overridden with `--raw-compiler`, `--qbe`, `--cc`, `--runtime`, and `--stdlib-root`. A custom driver output path is accepted by `tools/build_nplc.sh`; it continues to use the configured build-tree dependencies.

## Standard Libraries

R7RS library sources live under `lib/scheme/`; they are runtime compiler inputs, not embedded in `bootstrap/current.ssa`. The raw compiler accepts `--stdlib-root DIR`, and the native driver supplies its configured absolute source-tree root by default. User libraries remain relative to the source program.

Only `(npl primitive)` is compiler-defined. It maps the backend/runtime intrinsics and derived syntax needed by checked-in standard libraries. Changes to ordinary `(scheme ...)` implementation or exports do not require canonical compiler regeneration; changes to the primitive registry, loader, lowering, or runtime ABI do.

## Acceptance

Run the full repository gate with:

```bash
./tools/run_tests.sh
```

The gate includes permanent bootstrap recovery, current fixed-point recovery, full compiler language and contract suites, runtime GC, stage0 checks, the R7RS baseline, and property tests. After `check_current_fixed_point.sh` has already passed for the exact same tree, run `./tools/run_tests.sh --skip-current-fixed-point` to execute the remaining gate without repeating the three-generation relay. SIMD kernels and their standalone benchmark are deferred until the tensor runtime reaches the CPU-optimization phase; the current runtime does not expose or call them.

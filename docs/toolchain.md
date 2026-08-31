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

The current artifact is presently 340,925 lines with SHA-256 `7f60cf9501a997ee1df137783639afd5a42248e4c05cce9a45344c325507e945`.

## Update Current SSA

After changing `src/compiler/`, regenerate and install the canonical artifact with:

```bash
./tools/update_current_ssa.sh
```

This first runs the complete gen1/gen2/gen3 comparison. Only after it succeeds does it replace `bootstrap/current.ssa`; no assembly, object, or binary is stored in Git.

## Build Nplc

Build the complete native compiler binary from the canonical SSA with:

```bash
./tools/build_nplc.sh
```

The default output is `build/bin/nplc`. A custom output path is accepted:

```bash
./tools/build_nplc.sh /tmp/nplc
```

The process is QBE lowering followed by clang linking against the built runtime archive. The resulting compiler emits QBE SSA:

```bash
build/bin/nplc --emit-qbe program.npl > program.ssa
```

The compiler's successful emit mode exits with status `44`, which is the existing compiler convention. The generated SSA can be lowered and linked with QBE and `build/runtime/libnpl_runtime.a`.

## Standard Libraries

R7RS library sources live under `lib/scheme/`; they are runtime compiler inputs, not embedded in `bootstrap/current.ssa`. The current resolver uses `lib/` as the deterministic standard-library root relative to the compiler process working directory, while user libraries remain relative to the source program. Tooling runs from the repository root. The native driver increment will pass an explicit configured standard-library root so installed compilers do not depend on their caller's working directory.

Only `(npl primitive)` is compiler-defined. It maps the backend/runtime intrinsics and derived syntax needed by checked-in standard libraries. Changes to ordinary `(scheme ...)` implementation or exports do not require canonical compiler regeneration; changes to the primitive registry, loader, lowering, or runtime ABI do.

## Acceptance

Run the full repository gate with:

```bash
./tools/run_tests.sh
```

The gate includes permanent bootstrap recovery, current fixed-point recovery, full compiler language and contract suites, runtime GC, stage0 checks, the R7RS baseline, and property tests. After `check_current_fixed_point.sh` has already passed for the exact same tree, run `./tools/run_tests.sh --skip-current-fixed-point` to execute the remaining gate without repeating the three-generation relay. SIMD kernels and their standalone benchmark are deferred until the tensor runtime reaches the CPU-optimization phase; the current runtime does not expose or call them.

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

The current artifact is presently 340,067 lines with SHA-256 `7a1583ceed18fe8e0977437b734a3473d2f9918d1b3ea9526157b60844685a46`.

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

## Acceptance

Run the full repository gate with:

```bash
./tools/run_tests.sh
```

The gate includes permanent bootstrap recovery, current fixed-point recovery, full compiler language and contract suites, runtime GC, stage0 checks, the R7RS baseline, and property tests. `tools/check_simd.sh` remains an independent Phase 9 benchmark because its `simd_bench` target exists, but it is not part of the default gate. No existing tool script was removed: the remaining scripts have live build targets or current acceptance coverage.

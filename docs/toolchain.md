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

The tracked artifact has 346,404 lines (12,099,521 bytes), SHA-256 `10f81d0f47f3627506bb8ccb6fdae7f257264e803b21e1ff6413bc503a44926b` as inspected on 2026-09-08. Regeneration reports the authoritative hash for its source tree.

The verifier reports elapsed time and exit status for each configure, build, QBE, link, and emission stage. Failed or interrupted runs retain their work directory for inspection. TERM/INT/HUP is forwarded through the update wrapper to the active stage's process group. The focused orchestration test is `bash tools/check_relay_scripts.sh`; it uses tiny tool substitutes and does not establish real compiler correctness.

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

Builds without an explicit configuration default to `RelWithDebInfo` (`-O2 -g` with the current toolchain). Explicit `Debug` and custom configurations remain available, for example `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug`. This default applies to host C components; QBE still compiles NPL-generated assembly.

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

## Fixed-Point Assessment

Assessment date: 2026-09-07. Retain the recovery artifact and self-compilation check; do not use it as a language-conformance or evaluator-consistency verdict.

- `build_nplc.sh` and `check_full_compiler.sh` build directly from `bootstrap/current.ssa`. Removing that artifact without a tested replacement breaks the documented recovery path. `check_bootstrap.sh` verifies only the frozen bootstrap compiler against its own source, not recovery of the evolving compiler.
- The relay compares gen1, gen2, and gen3; it does not compare the input current SSA with gen1. This supports updating the compiler from an older seed and establishes sampled self-compilation stability. It does not prove all programs are compiled correctly, exclude shared compiler/runtime defects, or compare evaluator semantics.
- Concrete counterexamples were reproduced before the first consistency repair: evaluator nodes 220/222 were missing, and both native and REPL returned `#t` for `(= 4294967296 0)` and `(< 2147483648 0)` where Guile returns `#f`. The current repair and its acceptance scope are tracked in the R7RS plan.
- Cost is principally repeated compilation work, not byte comparisons. The original runtime was unoptimized. A 2-second sample contained 97/185 samples in GC sweeping under binding lookup; this local sample motivated the controlled experiments below, not an immediate GC redesign.

Measured full relay on 2026-09-07 (default, unoptimized runtime build):

| Stage | Elapsed Seconds |
|---|---:|
| Configure and runtime build | 2 |
| Three QBE passes | 11 |
| Three links | 54 |
| gen1 / gen2 / gen3 emission | 287 / 262 / 286 |
| Sum of reported stages | 902 |

All three generated SSA files and the input current SSA were byte-identical at the then-current hash `1c8b7d6a965a090e2d89497f47da420835923b4de58ec4e690217d9168f2ec91`. Emission accounts for about 93% of measured stage time. This is one local run, not a portable benchmark or a before/after speedup claim. The full relay completed successfully; the prior 600-second budget was insufficient.

Recommended validation policy, pending user approval: keep the canonical artifact; run focused semantic tests during edits; run one real relay for an accepted compiler/code-generation or runtime-ABI change, recovery update, or release checkpoint. Consider replacing the unconditional third generation with an explicitly validated fixed-point criterion and recording source/runtime/toolchain fingerprints to avoid duplicate work. Neither reduced generation count nor caching has been implemented or approved here. Runtime-only changes that preserve emitted SSA still require relevant execution tests, even if a future policy skips a full relay.

The current repository requirement remains the full three-generation comparison. A timeout means verification is incomplete, not that the fixed point is invalid. No permanent bootstrap files or recovery guarantees are removed by this assessment.

## Build Performance

2026-09-07 controlled local measurements used the compiler input from commit `8d91a2d` and the same QBE assembly for the initial runtime comparison. Compiler output was redirected to SSA files, not a terminal. The lookup-only compiler was produced from that commit with only the `lookup-value-binding` change, then compiled the unchanged input. Both measured outputs compare byte-for-byte with the old canonical SSA.

| Compiler / Runtime | Wall Seconds | User CPU Seconds | Instructions Retired | Max RSS Bytes |
|---|---:|---:|---:|---:|
| Original compiler, `RelWithDebInfo` runtime | 193.15 | 162.20 | 706,121,708,222 | 3,323,117,568 |
| Lookup-only compiler, same optimized runtime | 144.77 | 102.62 | 446,986,092,685 | 3,365,306,368 |

Hoisting name/mark calculation out of the linear environment scan reduced user CPU by about 37% and retired instructions by about 37% in this pair. Wall time fell about 25%. The preceding unoptimized relay emitted the same compiler in 262-287 seconds per generation; its comparison with 193 seconds is historical, not a paired statistical benchmark. These are single local samples under a loaded host, not guaranteed speedups. macOS reported substantial system-wide swap usage; timing variability and page activity must not be attributed solely to source changes.

Reproducible inputs and raw measurements remain in ignored `build/compiler-perf/`: `input/` is `git archive 8d91a2d src/compiler`; `lookup-input/` differs only in `frontend/syntax.npl`; `seed.s` comes from QBE on the original current SSA. The two timed commands were:

```bash
/usr/bin/time -l build/compiler-perf/seed-o2 --emit-qbe build/compiler-perf/input/src/compiler/main.npl > build/compiler-perf/o2.ssa 2> build/compiler-perf/o2.time
/usr/bin/time -l build/compiler-perf/lookup-compiler --emit-qbe build/compiler-perf/input/src/compiler/main.npl > build/compiler-perf/lookup.ssa 2> build/compiler-perf/lookup.time
```

`seed-o2` is linked from `seed.s` and `build/perf-o2/runtime/libnpl_runtime.a`, configured with `cmake -S . -B build/perf-o2 -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo`. `lookup-compiler` is built from the SSA emitted by `seed-o2` for `lookup-input/src/compiler/main.npl`, using the same runtime. For new experiments, preserve the input snapshot and alternate run order; do not compare different source trees as though only execution speed changed.

Remaining cost has concrete source-level causes:

- `lookup-value-binding` traverses a list; the accepted optimization removes repeated key extraction but not linear lookup complexity.
- `emit-lambda` captures `visible-env` in full, rather than selecting free variables. CPS introduces many continuation lambdas, amplifying capture copying and object retention. Free-variable capture filtering needs separate closure/mutation/recursive-binding/continuation tests before adoption.
- GC begins at a minimum 1 GiB of allocated payload. `NplAllocMeta` adds 64 bytes per object on this host, so small cons/environment/closure objects reach a multi-GiB footprint before collection. Mapped chunks remain for the process lifetime. Lowering the threshold may reduce memory but increase sweep frequency; it needs a measured experiment, not a guessed constant.
- Driver and REPL build scripts restore the raw compiler and driver repeatedly. Dependency-aware reuse is a further opportunity, but no cache or reduced fixed-point criterion was introduced in this change.

The 2026-09-08 final consistency relay passed with emissions of 91/87/89 seconds and 314 seconds summed across reported stages. This is an end-to-end observation on a changed compiler, not the controlled optimization comparison above.

The first candidate rebuilt the REPL in 46.17 wall seconds (35.74 user seconds, including driver child processes). This is a current observation, not a paired REPL speedup measurement.

## Acceptance

Run the full repository gate with:

```bash
./tools/run_tests.sh
```

The gate includes permanent bootstrap recovery, current fixed-point recovery, full compiler language and contract suites, runtime GC, stage0 checks, the R7RS baseline, and property tests. After `check_current_fixed_point.sh` has already passed for the exact same tree, run `./tools/run_tests.sh --skip-current-fixed-point` to execute the remaining gate without repeating the three-generation relay. SIMD kernels and their standalone benchmark are deferred until the tensor runtime reaches the CPU-optimization phase; the current runtime does not expose or call them.

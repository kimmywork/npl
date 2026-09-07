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

The tracked artifact has 348,879 lines (12,191,859 bytes), SHA-256 `1c8b7d6a965a090e2d89497f47da420835923b4de58ec4e690217d9168f2ec91` as inspected on 2026-09-07. Regeneration reports the authoritative hash for its source tree.

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
- Concrete counterexamples exist even with a historical fixed point: the evaluator lacks core nodes 220/222, and both current native and REPL implementations return `#t` for `(= 4294967296 0)` and `(< 2147483648 0)` where Guile returns `#f`. These are tracked in the R7RS plan, not excused by fixed-point status.
- Cost is principally repeated compilation work, not the byte comparisons. The current artifact is about 12.2 MB; one sampled emission had a 2.8 GiB footprint. A 2-second sample contained 97/185 samples in GC sweeping under binding lookup. The default runtime build has no C optimization flag. These observations justify profiling allocation and lookup behavior; they are not a whole-run attribution or evidence to change GC policy immediately.

Measured full relay on 2026-09-07 (default, unoptimized runtime build):

| Stage | Elapsed Seconds |
|---|---:|
| Configure and runtime build | 2 |
| Three QBE passes | 11 |
| Three links | 54 |
| gen1 / gen2 / gen3 emission | 287 / 262 / 286 |
| Sum of reported stages | 902 |

All three generated SSA files and the input current SSA were byte-identical at the hash above. Emission accounts for about 93% of measured stage time. This is one local run, not a portable benchmark or a before/after speedup claim. The full relay completed successfully; the prior 600-second budget was insufficient.

Recommended validation policy, pending user approval: keep the canonical artifact; run focused semantic tests during edits; run one real relay for an accepted compiler/code-generation or runtime-ABI change, recovery update, or release checkpoint. Consider replacing the unconditional third generation with an explicitly validated fixed-point criterion and recording source/runtime/toolchain fingerprints to avoid duplicate work. Neither reduced generation count nor caching has been implemented or approved here. Runtime-only changes that preserve emitted SSA still require relevant execution tests, even if a future policy skips a full relay.

The current repository requirement remains the full three-generation comparison. A timeout means verification is incomplete, not that the fixed point is invalid. No permanent bootstrap files or recovery guarantees are removed by this assessment.

## Acceptance

Run the full repository gate with:

```bash
./tools/run_tests.sh
```

The gate includes permanent bootstrap recovery, current fixed-point recovery, full compiler language and contract suites, runtime GC, stage0 checks, the R7RS baseline, and property tests. After `check_current_fixed_point.sh` has already passed for the exact same tree, run `./tools/run_tests.sh --skip-current-fixed-point` to execute the remaining gate without repeating the three-generation relay. SIMD kernels and their standalone benchmark are deferred until the tensor runtime reaches the CPU-optimization phase; the current runtime does not expose or call them.

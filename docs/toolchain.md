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

`bootstrap/current.ssa` is the unified `nplc` (compiler and driver in one program). The tracked artifact has 195,239 lines, SHA-256 `b9a93cea565569eae9589cbeec14b636ff4db2b58352de9bdacd98b7b71b40dc` as accepted on 2026-10-08; macOS arm64 and Linux aarch64 relays both regenerate this hash. Regeneration reports the authoritative hash for its source tree.

On a memory-constrained or heavily loaded host, run the relay with `NPL_GC_THRESHOLD_BYTES=268435456`: on 2026-09-30, with system swap nearly full, a default-threshold (1 GiB) emission stalled for hours in paging, while the same compiler at 256 MiB emitted in 82 s with 0.99 GB RSS and byte-identical SSA.

Every emission succeeds with status 0. Seeds from before the unified `nplc` exit 44 on success; relay from such a seed once with `--seed-status 44` (also accepted by `update_current_ssa.sh`).

The verifier reports elapsed time and exit status for each configure, build, QBE, link, and emission stage. Failed or interrupted runs retain their work directory for inspection. TERM/INT/HUP is forwarded through the update wrapper to the active stage's process group. The focused orchestration test is `bash tools/check_relay_scripts.sh`; it uses tiny tool substitutes and does not establish real compiler correctness.

## Update Current SSA

After changing `src/compiler/`, regenerate and install the canonical artifact with:

```bash
./tools/update_current_ssa.sh
```

This first runs the complete gen1/gen2/gen3 comparison. Only after it succeeds does it replace `bootstrap/current.ssa`; no assembly, object, or binary is stored in Git.

## Build And Install

```bash
./tools/build_toolchain.sh               # build/bin/nplc and build/bin/npli (incremental)
cmake --install build --prefix PREFIX    # install
```

CMake builds `nplc` from `bootstrap/current.ssa` and then `npli` with that `nplc`; Ninja rebuilds each only when the seed, runtime, or NPL sources change. `tools/build_nplc.sh` and `tools/build_npli.sh` remain for building to a custom output path. Builds without an explicit configuration default to `RelWithDebInfo`.

Install layout, which the build tree mirrors with links so `build/bin/nplc` behaves exactly like an installed one:

```text
PREFIX/bin/nplc, PREFIX/bin/npli
PREFIX/libexec/npl/qbe
PREFIX/lib/npl/libnpl_runtime.a
PREFIX/share/npl/lib/            (scheme ...) and (npl ...) libraries
PREFIX/share/npl/src/compiler/   frontend sources included by (npl frontend)
```

`nplc` resolves each tool and library at run time: an explicit option first, then an environment variable, then this layout relative to its own executable. Nothing host-specific is compiled into the canonical SSA; emitting from a different working directory or from an installed `nplc` gives byte-identical output (`tools/check_install.sh`).

| Setting | Option | Environment | Fallback |
|---|---|---|---|
| standard library | `--stdlib-root DIR` | `NPL_STDLIB_ROOT` | layout, then `lib/` |
| QBE | `--qbe PATH` | `NPL_QBE` | layout, then `qbe` on `PATH` |
| C compiler/linker | `--cc PATH` | `NPL_CC`, then `CC` | `cc` on `PATH` |
| runtime archive | `--runtime PATH` | `NPL_RUNTIME` | layout, else error |

Output kinds:

```bash
nplc program.npl                     # executable ./program (default --emit=exe)
nplc program.npl -o out              # executable out
nplc -c program.npl                  # object program.o   (--emit=obj)
nplc -S program.npl                  # assembly program.s (--emit=asm)
nplc --emit=ssa program.npl          # QBE SSA on standard output
nplc --emit=ssa program.npl -o p.ssa # QBE SSA to a file
nplc --keep-temps program.npl        # keep and report intermediate files
```

`--emit-qbe` is an alias for `--emit=ssa`. `--emit=staticlib` and `--emit=dylib` are reserved and rejected until the deferred library work in [the toolchain plan](plan-toolchain.md) (D1). An object file contains `main` and links with the runtime archive: `cc -o program program.o libnpl_runtime.a`.

SSA to standard output is emitted in-process; this is the relay path. File-producing builds re-run `nplc --emit=ssa` as a child into an isolated temporary directory, then run QBE and the C compiler with argv-based processes. On failure the compiler diagnostic is relayed to standard error, partial outputs are removed, and the temporary directory is deleted. Option errors exit 2; a failed stage reports its name and status.

## npli

`npli` is the REPL and program runner, built by `nplc` from `src/repl/main.npl` over the shared evaluator:

```bash
npli                                  # interactive REPL (--no-prompt for transcripts)
npli program.scm arg ...              # run a program; (command-line) is (program.scm arg ...)
npli -e '(import (scheme base))' -e EXPR [program.scm ...]
```

A program's `(exit n)` sets the process status (`#t` is 0, `#f` is 1, as R7RS 6.14 and Guile). An evaluation error prints `error: ...` and exits 70; a missing program exits 66; a usage error exits 2. The standard library root is `--stdlib-root`, then `NPL_STDLIB_ROOT`, then the install layout, then the build-time checkout path. `npli` is not a bootstrap artifact.

`(command-line)` follows R7RS: the first element is the command name. Compiled programs report the executable path; `npli` reports the program file.

## Linux

```bash
tools/check_linux.sh                         # full gate in a Debian container
tools/check_linux.sh --skip-current-fixed-point
```

The script copies tracked and non-ignored files into `build/linux/src` and runs `tools/run_tests.sh` in the local colima VM (`DOCKER_CONTEXT`, default `colima`) using `tools/linux/Dockerfile` (Debian bookworm, clang, Guile 3). It sets `NPL_GC_THRESHOLD_BYTES=134217728` because the default 2 GiB VM cannot hold self-compilation at the 1 GiB native default (the kernel kills it with status 137). Executables link `-lm` explicitly; on macOS it is part of libSystem.

## Standard Libraries

R7RS library sources live under `lib/scheme/`; they are runtime compiler inputs, not embedded in `bootstrap/current.ssa`. `nplc` locates them as described above. User libraries remain relative to the source program.

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

Closure-conversion repair (2026-09-29). Profiles of the evaluator core test (237 s, 3 GB RSS) and the relay attributed 70-80% of samples to GC sweep and allocation. The allocation came from code generation, not from the collector:

- Every parameter and `let` binding was boxed in a heap cell, although only `set!` targets need one. `env-bind` now allocates a cell only for names in the top-level form's `set!` target set (`collect-assigned`); `letrec` and internal definitions keep cells because they are initialized after allocation.
- `emit-lambda` captured the whole visible environment. `lambda-captures` now captures only innermost bindings whose names the body may reference. Both analyses walk core node shapes, skip literal data (datum labels may be circular), and ignore shadowing, so they over-approximate in the safe direction.
- The capture change exposed a latent GC defect: `$gv_` global slots live in the data segment, which the collector never scanned. Values stored by `set!` or runtime initializers were reachable only by accident through conservatively scanned captured environments. `$npl_global_inits` now registers every slot with `npl_gc_root`, and the root table grows instead of failing at 1,024 entries. `src/compiler/tests/gc-roots.npl` fails with the previous compiler and passes now.

| Measurement (same host, single samples) | Before | After |
|---|---:|---:|
| Self-compilation instructions retired | 601 G | 338 G |
| Self-compilation wall seconds | 110-202 | 66-75 |
| Strict three-generation relay | 902 s (2026-09-07) | 224 s |
| `current.ssa` lines | 352,393 | 184,776 |
| `tools/run_tests.sh --skip-current-fixed-point` | 9 m 12 s | 8 m 0 s |
| `tools/run_tests.sh` (includes relay; properties parallelized) | not measured | 12 m 6 s |

`NPL_GC_THRESHOLD_BYTES` overrides the 1 GiB minimum collection threshold for tests. Lower thresholds were measured and rejected as a default: self-compilation took 100 s at 1 GiB, 113 s at 256 MiB, and 149 s at 64 MiB (RSS 3.0/0.97/0.46 GB), with byte-identical SSA. Sweep cost scales with the full object list, so a lower default needs a generational or mark-bit design, not a smaller constant.

Remaining measured costs:

- Evaluator execution allocates heavily per step (CPS continuations, `eval-frame-find` over a 196-binding global frame). The million-step `delay-force` test in `src/evaluator/core-tests.npl` still dominates `check_repl.sh` at roughly 70 s.
- Mapped chunks remain for the process lifetime, and `NplAllocMeta` adds 64 bytes per object.
- Driver and REPL build scripts rebuild the raw compiler and driver repeatedly. No cache or reduced fixed-point criterion has been introduced.

The 2026-09-08 final consistency relay passed with emissions of 91/87/89 seconds and 314 seconds summed across reported stages. This is an end-to-end observation on a changed compiler, not the controlled optimization comparison above.

The first candidate rebuilt the REPL in 46.17 wall seconds (35.74 user seconds, including driver child processes). This is a current observation, not a paired REPL speedup measurement.

## Acceptance

Run the full repository gate with:

```bash
./tools/run_tests.sh
```

The gate includes permanent bootstrap recovery, current fixed-point recovery, full compiler language and contract suites, runtime GC, stage0 checks, the R7RS baseline, and property tests. After `check_current_fixed_point.sh` has already passed for the exact same tree, run `./tools/run_tests.sh --skip-current-fixed-point` to execute the remaining gate without repeating the three-generation relay. SIMD kernels and their standalone benchmark are deferred until the tensor runtime reaches the CPU-optimization phase; the current runtime does not expose or call them.

# Compiler Toolchain

The repository stores compiler source and canonical QBE SSA, not a compiler binary. The permanent bootstrap compiler and the evolving full compiler have separate recovery roles.

## Verify The Relay

The permanent chain (stage0 `nplc0`, `bootstrap/compiler/`, `bootstrap/bootstrap.ssa`) is frozen together with its runtime, `bootstrap/runtime/`: a verbatim copy of `runtime/` at commit `43d2a14`, built as `npl_runtime_frozen`. `tools/check_bootstrap.sh`, stage0, and the stage0 checks link only against it, so the current runtime may change object layouts and entry points freely.

`bootstrap/current.ssa` is the current full-compiler recovery artifact. Verify the historical relay with:

```bash
./tools/check_current_fixed_point.sh
```

The command builds a compiler from `bootstrap/current.ssa`, emits `gen1.ssa`, rebuilds from gen1, emits gen2, rebuilds from gen2, emits gen3, and requires all three SSA files to be byte-identical. It prints the canonical line count and SHA-256. To retain relay artifacts:

```bash
./tools/check_current_fixed_point.sh --keep --work-dir /tmp/npl-fixed-point
```

`bootstrap/current.ssa` is the unified `nplc` (compiler and driver in one program). The tracked artifact's current line count and SHA-256 are recorded in `.agents/workstate.md` (Accepted Seed); a strict relay regenerates that hash. Regeneration reports the authoritative hash for its source tree.

Since 2026-10-21 the runtime collects a 1 MiB nursery and runs full collections from 128 MiB, so a self-compilation emission peaks near 0.27 GB RSS and no threshold override is needed on a loaded host. (With the earlier 1 GiB threshold an emission stalled for hours in paging on 2026-09-30 and needed `NPL_GC_THRESHOLD_BYTES=268435456`.)

Every emission succeeds with status 0. Seeds from before the unified `nplc` exit 44 on success; relay from such a seed once with `--seed-status 44` (also accepted by `update_current_ssa.sh`).

The verifier reports elapsed time and exit status for each configure, build, QBE, link, and emission stage. Failed or interrupted runs retain their work directory for inspection. TERM/INT/HUP is forwarded through the update wrapper to the active stage's process group. The focused orchestration test is `bash tools/check_relay_scripts.sh`; it uses tiny tool substitutes and does not establish real compiler correctness.

## Update Current SSA

After changing `src/compiler/`, regenerate and install the canonical artifact with:

```bash
./tools/update_current_ssa.sh
```

This first runs the complete gen1/gen2/gen3 comparison. Only after it succeeds does it replace `bootstrap/current.ssa`; no assembly, object, or binary is stored in Git.

## Version

`VERSION` holds the release version (also `project(npl VERSION ...)` in
`CMakeLists.txt`; keep the two equal). It is informational: no tool reads it,
and it does not enter `bootstrap/current.ssa`. Release notes are in
[CHANGELOG](../CHANGELOG.md); a release is a `v<version>` git tag on a commit
that passed the gate on macOS and Linux ([testing](testing.md)).

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
PREFIX/include/npl/npl.h         runtime ABI for C functions (docs/ffi.md)
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
nplc --emit=c-stubs program.npl      # define-c-function C stubs, for npli (docs/ffi.md)
nplc --keep-temps program.npl        # keep and report intermediate files
nplc program.npl -L lib -l clamp     # also link lib/libclamp (C functions, docs/ffi.md)
```

A program is its top-level forms, run in order as in `npli` (R7RS 5.1); `main` is an ordinary name. A program that reaches its end exits with status 0, and `(exit n)` ends it earlier with status `n`. `nplc` generates the C `main` (`src/compiler/frontend/control-program.npl`). Programs for the frozen stage0 `nplc0` still use `(define (main) ...)`.

`-lNAME`/`-l NAME` and `-LDIR`/`-L DIR` reach the executable link in command-line order, before the runtime archive; other kinds ignore them.

`--emit-qbe` is an alias for `--emit=ssa`. `--emit=staticlib` and `--emit=dylib` are reserved and rejected; see "Deferred: Libraries And Binary Interop". An object file contains `main` and links with the runtime archive: `cc -o program program.o libnpl_runtime.a`.

SSA to standard output is emitted in-process; this is the relay path. File-producing builds re-run `nplc --emit=ssa` as a child into an isolated temporary directory, then run QBE and the C compiler with argv-based processes. On failure the compiler diagnostic is relayed to standard error, partial outputs are removed, and the temporary directory is deleted. Option errors exit 2; a failed stage reports its name and status.

## npli

`npli` is the REPL and program runner, built by `nplc` from `src/repl/main.npl` over the shared evaluator:

```bash
npli                                  # interactive REPL (--no-prompt for transcripts)
npli program.scm arg ...              # run a program; (command-line) is (program.scm arg ...)
npli -e '(import (scheme base))' -e EXPR [program.scm ...]
npli -l ./libclamp.so program.scm     # load C functions first (docs/ffi.md)
```

`npli` links the whole runtime archive (`-Wl,-force_load` on macOS; `--whole-archive` and `-rdynamic` on Linux, `tools/build_npli.sh`) so a library loaded with `-l` can call every runtime function through it. A library that cannot be loaded exits 66.

A program's `(exit n)` sets the process status (`#t` is 0, `#f` is 1, as R7RS 6.14 and Guile). An evaluation error prints `error: ...` and exits 70; a missing program exits 66; a usage error exits 2. The standard library root is `--stdlib-root`, then `NPL_STDLIB_ROOT`, then the install layout, then the build-time checkout path. `npli` is not a bootstrap artifact.

`(command-line)` follows R7RS: the first element is the command name. Compiled programs report the executable path; `npli` reports the program file.

## Linux

```bash
tools/check_linux.sh                         # full gate in a Debian container
tools/check_linux.sh --skip-current-fixed-point
```

The script copies tracked and non-ignored files into `build/linux/src` and runs `tools/run_tests.sh` in the local colima VM (`DOCKER_CONTEXT`, default `colima`) using `tools/linux/Dockerfile` (Debian bookworm, clang, Guile 3). It passes `NPL_GC_THRESHOLD_BYTES=134217728`, now equal to the runtime default; the default 2 GiB VM could not hold self-compilation at the former 1 GiB default (the kernel killed it with status 137). Executables link `-lm` explicitly; on macOS it is part of libSystem.

## Standard Libraries

R7RS library sources live under `lib/scheme/`; they are runtime compiler inputs, not embedded in `bootstrap/current.ssa`. `nplc` locates them as described above. User libraries remain relative to the source program.

Libraries and programs can declare further runtime C functions with `define-c-primitive`; see [C Functions From NPL](ffi.md). Only `(npl primitive)` is compiler-defined. It maps the backend/runtime intrinsics and derived syntax needed by checked-in standard libraries. Changes to ordinary `(scheme ...)` implementation or exports do not require canonical compiler regeneration; changes to the primitive registry, loader, lowering, or runtime ABI do.

## Fixed Point Scope

The relay compares gen1, gen2, and gen3 (not the input seed with gen1), so a compiler can be updated from an older seed. It establishes self-compilation stability only: it does not prove programs compile correctly, exclude shared compiler/runtime defects, or compare evaluator semantics. Run it for every compiler, code-generation, or runtime-ABI change; library-only changes do not need it. A timeout means verification is incomplete, not that the fixed point is invalid.

## Performance Notes

Measured on macOS arm64; single samples on a loaded host.

| Measurement | Value |
|---|---:|
| Self-compilation instructions retired (2026-09-29) | 338 G (was 601 G) |
| Strict relay | ~4-5 min (was 902 s on 2026-09-07) |
| `tools/run_tests.sh`, with relay | 11 min |

Cell allocation is limited to `set!` targets (`collect-assigned`), and closures capture only referenced free variables (`lambda-captures`). Both analyses skip literal data and ignore shadowing, so they over-approximate safely. Data-segment globals are registered as GC roots by `$npl_global_inits`; `src/compiler/tests/gc-roots.npl` guards this.

The collector is generational and non-moving (`runtime/alloc.c`, `runtime/gc.c`): young objects are collected when `NPL_GC_NURSERY_BYTES` (default 1 MiB; 0 disables the nursery) have been allocated, and the whole heap when it reaches `NPL_GC_THRESHOLD_BYTES` (default 128 MiB, doubling with live data). Runtime stores into existing objects go through `npl_gc_write_barrier`; `NPL_GC_VERIFY=N` checks barriers and dangling references at every Nth collection. Measured 2026-10-21 (host load ~8): self-compilation 13.3-13.6 s CPU and 0.27 GB peak RSS, against 16.6-16.7 s and 2.65 GB without the nursery at the former 1 GiB threshold; all nine bench workloads take equal or less CPU, with identical output.

npli compiles each expanded form to native closures (`lib/npl/evaluator-compile.scm`, .agents/archive/design/design-evaluator.md): lexical addresses, hashed global cells, native control and error objects. Measured 2026-10-23 (host load ~8-11): a 2M-iteration import-free loop 3.9 s user (about 20x the former CPS interpreter); with `(import (scheme base))` the same loop took 48.8 s while the standard library was evaluated from source, and 3.95 s since E2b binds it to npli's compiled procedures (`(%library-definitions ...)` with a source checksum; an edited library falls back to evaluation from source). Two-operand calls to `(scheme base)`'s numeric operators take the compiled fixnum fast path while their binding is unchanged (E3); all nine bench workloads then finish under npli in 0.5-18.4 s user. Most of the remaining time is allocation for native non-tail calls. Mapped chunks are never returned, and allocator metadata adds 64 bytes per object.

## Deferred: Libraries And Binary Interop

Not implemented by decision (2026-09-29): `staticlib`/`dylib` output with an explicit C export list and init entry, and loading binary libraries from NPL. Constraints for that work:

- Compilation is whole-program; library-internal symbols carry load-order ids (`_npl_lib_7_...`) that are not stable across units. Stable mangling from library name and identifier comes first.
- The runtime holds process-global state (GC, roots, trampoline, dynamic state). Several NPL shared objects in one process need one shared `libnpl_runtime`, not embedded copies.
- C callers holding NPL values across GC need root registration and a trampoline boundary at each exported entry point.
- Separate compilation of NPL `define-library` units (interface files, exported macro serialization) is a larger, separate decision.

## Acceptance

Run the full repository gate with:

```bash
./tools/run_tests.sh
```

The gate builds the toolchain once, then runs permanent bootstrap recovery, the current fixed point, compiler language and contract suites, the driver, install layout, `npli`, shared R7RS fixtures (native/`npli`/Guile), runtime GC and numeric checks, stage0 checks, and property tests. After the relay has passed for the exact same tree, `./tools/run_tests.sh --skip-current-fixed-point` runs the rest. `tools/check_linux.sh` runs the same gate on Linux.

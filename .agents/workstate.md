# Workstate

## Current

The toolchain plan ([plan-toolchain](../docs/plan-toolchain.md)) T0-T5 is
complete: `bootstrap/current.ssa` is the unified `nplc` (ssa/asm/obj/exe,
run-time tool resolution, status 0); `npli` is the REPL and program runner;
`cmake --install` produces a relocatable layout the build tree mirrors; macOS
arm64 and Linux aarch64 (colima) both pass the full gate and regenerate the
same seed. Library/FFI outputs (D1) remain deferred by decision.

The next stage is standard libraries, then Guile performance comparison.
History: [worklog](worklog/2026-10-08.md).

## Accepted Seed And Evidence

- `bootstrap/current.ssa`: 195,239 lines; SHA-256
  `b9a93cea565569eae9589cbeec14b636ff4db2b58352de9bdacd98b7b71b40dc`.
  Strict relay passes on macOS and in Linux (same hash).
- `tools/run_tests.sh` (with relay): all PASS, macOS 10 m 59 s, Linux
  (`tools/check_linux.sh`) 11 m 48 s. On a swap-bound host use
  `NPL_GC_THRESHOLD_BYTES=268435456`.
- Guards added 2026-09-29..10-08: `gc-roots.npl`, shared fixtures `mutable-bindings` and
  `named-let`, `char-basics`, named-let rejection rows, `check_driver.sh`,
  `check_install.sh`, npli script checks, `check_linux.sh`.
- Earlier seeds retained: `build/pre-closure-current.ssa` (`ebc4b2f2...`),
  `build/pre-t1-current.ssa` (`9b002197...`, status-44 era).
- Permanent `bootstrap/bootstrap.ssa` and `bootstrap/compiler/` unchanged.

## Next

1. Standard libraries in dependency order (see the R7RS gap list in
   [plan-toolchain](../docs/plan-toolchain.md) "Decisions" and the
   [conformance plan](../docs/plan-r7rs-conformance-guile.md)): pure-NPL
   `(scheme base)` list/string/vector/char procedures (`map`, `for-each`,
   `apply`, `member`, `assoc`, `equal?`, ...); records and error objects;
   Unicode strings/chars (runtime is ASCII/byte-based today); datum labels in
   runtime `read` and `write-shared`; `(scheme complex)`, `(scheme time)`,
   remaining `(scheme file)`; `(scheme r5rs)` last. Evaluator registry still
   lacks ~40 native primitives; native primitive type errors terminate instead
   of raising.
2. Guile performance baseline (conformance plan section 7); evaluator hot
   paths (global-frame linear lookup, CPS allocation) with profiles.
3. Oversized files to split when touched: `lib/npl/evaluator-core.scm`,
   `frontend/control.npl`, `expand.npl`, `library.npl`, `runtime/io.c`,
   `runtime/control.c`.
4. Keep compiler/runtime changes behind the strict relay on both platforms.

The workspace still contains uncommitted numeric work plus all toolchain
increments; no commit was requested.

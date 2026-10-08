# Toolchain Plan: Unified `nplc` And `npli`

Status: T0-T5 complete (2026-10-08). D1 remains deferred; the next stage is
standard libraries and Guile performance (section "Next Stage"). Current progress lives in
[workstate](../.agents/workstate.md).

## Decisions

- Two user entry points only:
  - `nplc`: one NPL program (compiler + driver) that is itself the canonical
    `bootstrap/current.ssa`. `nplc-raw` and the special success status 44 are
    retired; success is status 0.
  - `npli`: the persistent REPL (formerly `npl-repl`). It also runs programs:
    `npli file.scm [args...]`. It is built by `nplc` and is not a bootstrap
    artifact; `current.ssa` stays the single recovery seed.
- `nplc` output kinds: QBE SSA, assembly, object, executable now. Static
  library, shared library, C-callable exports and loading binary libraries are
  deferred (D1); T2 keeps the option surface and module boundaries for them.
- Linux validation uses the local colima VM (aarch64 Docker) when the Linux
  path is exercised; macOS arm64 remains the primary host.
- R7RS items found while probing stay tracked for the library stage: named let
  (fixed in T0), byte-based strings (Unicode), runtime `read` datum labels,
  `(scheme char)` in the REPL, `(scheme complex)`, `(scheme time)`, missing
  `(scheme base)` list/string/vector/record/error-object procedures.

## Invariants

- Permanent `bootstrap/bootstrap.ssa` and `bootstrap/compiler/` stay frozen.
- Every compiler/runtime-ABI increment passes the strict three-generation relay
  and the full `tools/run_tests.sh` before `current.ssa` is replaced.
- A compiler fix ships with a test that fails on the previous seed.
- Canonical SSA contains no host-specific absolute paths: generation from two
  different checkout/build directories must give the same hash.
- Policy (option parsing, path resolution, stage orchestration) is NPL; C only
  wraps OS facilities.

## Increments

### T0. Named let in the shared frontend

- Result: `(let name ((var init) ...) body ...)` works in native and REPL.
- Action: expand named let in the shared frontend (used by both compiler and
  evaluator) to `((letrec ((name (lambda (var ...) body ...))) name) init ...)`
  per R7RS 4.2.4; keep malformed-binding diagnostics for the ordinary form.
- Verify: shared fixture (native/REPL/Guile), a malformed named-let rejection,
  relay, full gate.

### T1. Unified `nplc` as the canonical seed

- Result: `bootstrap/current.ssa` is the unified compiler+driver; `nplc-raw`
  removed from scripts; status 0 on success.
- Actions:
  - Merge driver orchestration into the compiler program; in-process frontend
    and backend, external `qbe` and `cc` via the existing process wrapper.
  - Resolve tool/runtime/stdlib paths at run time: command-line option, then
    `NPL_HOME`/per-tool environment variables, then the install layout relative
    to the executable. Add the minimal C wrapper for the executable path.
  - Bootstrap scripts (`build_nplc.sh`, relay, update) build `nplc` from
    `current.ssa` with explicit paths; `--emit=ssa` is the relay emission.
- Verify: relay byte-identical; same SSA hash from two build directories;
  existing driver/compiler suites updated to status 0; full gate.

### T2. Output kinds

- Result: `nplc --emit=ssa|asm|obj|exe` (aliases `-S`, `-c`), `-o`,
  `--keep-temps`; default `exe`.
- Deferred-kind surface: `--emit=staticlib|dylib` is parsed and rejected with a
  "not yet supported" diagnostic; stage pipeline is a list of named stages so
  the archive/shared-link stages slot in without restructuring.
- Verify: each kind checked with `file`/`nm`; `.o` links with `cc` into a
  running program; bad combinations rejected.

### T3. `npli`

- Result: `npli` replaces `npl-repl`; `npli file.scm args...` runs a program
  through `(scheme load)` semantics with `command-line` set; no argument starts
  the REPL.
- Verify: existing REPL transcripts under the new name; shared fixtures' REPL
  lane uses `npli`; script mode exit status and `command-line` checks.

### T4. Install layout and build reuse

- Result: `cmake --install` produces `bin/nplc`, `bin/npli`,
  `lib/npl/runtime/libnpl_runtime.a`, `share/npl/lib/...`; `npli` is built by
  the installed `nplc`.
- Test scripts consume one built toolchain instead of rebuilding driver/REPL
  per script.
- Verify: install to a temporary prefix, compile and run hello outside the
  repository; gate time recorded before/after.

### T5. Linux parity (colima)

- Result: the same build and gate run in an aarch64 Linux container.
- Verify: gate passes in the container; SSA hash matches macOS (SSA is
  target-independent) or the difference is explained.

## Deferred D1: Libraries And Binary Interop

Not implemented now; recorded so T1-T2 do not block it.

- Kinds: `staticlib` (`.a`), `dylib` (`.dylib`/`.so`) with an explicit C export
  list and an init entry; loading binary libraries from NPL (FFI).
- Known constraints from the current design:
  - Whole-program compilation; library-internal symbols carry load-order ids
    (`_npl_lib_7_...`), not stable across units. Stable mangling from library
    name + identifier is required before separate compilation.
  - The runtime holds process-global state (GC, roots, trampoline, dynamic
    state). Several NPL shared objects in one process require one shared
    `libnpl_runtime`, not one embedded copy each.
  - C callers must hold NPL values across GC: exported entry points need root
    registration and a trampoline boundary.
- Separate compilation of NPL `define-library` units (interface files, exported
  macro serialization) is a larger, separate decision.

## Next Stage (after T5)

1. Standard libraries in dependency order with three-way fixtures: pure-NPL
   `(scheme base)` list/vector/string/char procedures; records and error
   objects; Unicode string representation; datum-label read and
   `write-shared`; `(scheme complex)`, `(scheme time)`, remaining `(scheme file)`;
   `(scheme r5rs)` last.
2. Performance against Guile: benchmark harness under the fairness rules in
   [the conformance plan](plan-r7rs-conformance-guile.md) section 7; native and
   `npli` measured separately; evaluator hot paths (global-frame linear lookup,
   CPS allocation) addressed with profiles. Racket and Chez later.

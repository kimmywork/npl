# Workstate

## Current

The permanent bootstrap language and compiler are frozen. The evolving full compiler has completed Dynamic Core, Reader/Datum, hygienic macros and derived forms, explicit-control execution, multiple values, unlimited-extent multi-shot continuations, `dynamic-wind`, exceptions, parameters, promises, and the minimal R7RS library system.

- `stage0/`: untyped C99 compiler with QBE and executable output.
- `bootstrap/compiler/`: permanent NPL bootstrap compiler.
- `src/compiler/`: full compiler split into `common/`, `reader/`, `frontend/`, and `backend/`.
- `bootstrap/bootstrap.ssa`: permanent 6,753-line bootstrap fixed point.
- `bootstrap/current.ssa`: current 340,925-line full-compiler fixed point, SHA-256 `7f60cf9501a997ee1df137783639afd5a42248e4c05cce9a45344c325507e945`.

The library system supports deterministic `.sld` resolution, `define-library`, top-level `import`, `export`, `only`, `except`, `prefix`, `rename`, `begin`, `include`, `include-ci`, `include-library-declarations`, and `cond-expand`. Per-compilation library caching and circular dependency detection are included. Package management, versions, dynamic loading, precompiled caches, and cyclic libraries are intentionally out of scope.

Step 6.1 now provides the initial runtime substrate for standard input/output/error ports, file ports, character and byte I/O, EOF objects, flushing, closing, and port predicates. The allocator uses explicit runtime initialization, a 1 GiB minimum GC threshold, 1 MiB chunks, direct chunk ownership, indexed conservative marking, and size-class reuse so self-hosting remains bounded. `(scheme base)` now has an explicit manifest for the procedures, control operations, and derived syntax currently implemented; import-set modifiers no longer treat it as an empty special case.

## Evidence

`./tools/run_tests.sh` reports `all suites: PASS`. This includes stage0, permanent bootstrap recovery, current gen1/gen2/gen3 byte-identical fixed-point recovery, full compiler suites and contracts, runtime GC, R7RS baseline, and property tests.

Stable toolchain entry points:

- `tools/check_current_fixed_point.sh` verifies current -> gen1 -> gen2 -> gen3.
- `tools/update_current_ssa.sh` installs gen3 as `bootstrap/current.ssa` only after verification.
- `tools/build_nplc.sh` builds `build/bin/nplc` from `bootstrap/current.ssa`.

## Next

Execute the approved execution plan in [`docs/plan-stdlib-driver-repl.md`](../docs/plan-stdlib-driver-repl.md). Step 6.1's initial port substrate is complete; next define explicit `(scheme base)`, `(scheme write)`, and `(scheme read)` export surfaces and add the remaining port/string-port operations. Then build the one-command driver and the persistent evaluator-backed REPL. Opportunistic compiler rewrites remain module-local and must pass the same semantic and fixed-point gates.

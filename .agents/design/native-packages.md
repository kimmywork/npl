# Native Packages

Status: proposal, 2026-11-23. Not yet an approved 0.1.3 increment.

## Problem

`(npl time)` currently keeps its C wrappers in `runtime/clock.c` and exposes
them through `(npl internal primitive)` rows. That works, but every library
with C logic grows the runtime, the primitive table, and the seed. Optional
packages should own their C code instead of modifying the core.

## Goal

A package can contain NPL, C, headers, and system dependencies, and be built
into a native library that NPL calls through the FFI. The runtime keeps only
generic mechanisms: allocation, GC, `npl.h`, pointer boxes, `npl_ffi_*`
memory/struct helpers, and error raising.

## Proposed Layout

```
lib/npl/time/
  time.sld                 public library
  native/time.c            NplVal-ABI entry points
  native/time.h
  native/CMakeLists.txt    builds libnpl_time.{a,dylib,so}
  package.meta             optional: system libraries, include dirs
```

`(npl time)` remains the import name. C may use `npl.h` for layout and the
F1/F2 helpers, but must not link a second `libnpl_runtime`.

## Binding Models

### A. Linked native library

`(npl time)` uses `define-c-primitive` / `define-c-function`; `nplc` links
`-lnpl_time`, and `npli` loads the shared library with `-l`.

- Fast direct calls.
- Needs driver/install support for per-package `-L`/`-l` flags and a manifest.
- Keeps declarations in the `.sld`, but the symbols must be present at load.

### B. Runtime-loaded plugin (recommended for optional packages)

The package exports a small NplVal ABI, e.g.

```c
NplVal npl_time_now(NplVal which, NplVal timespec_view);
```

`(npl time)` loads `libnpl_time` with `npl_ffi_load_library` and binds each
entry with `npl_ffi_procedure`. The functions take struct views as NplVal,
use the generic struct-pointer helper, write into the supplied views, and
return fixnums/status values. No compiler table or seed change is needed.

- The library is discovered under `NPL_STDLIB_ROOT` / install prefix; a small
  `(npl ffi)` path helper picks `.dylib` vs `.so`.
- The C library must not link the runtime; it may use only `npl.h` inline
  layout access and must not allocate unless it resolves the allocator from
  the host.
- Slightly slower (indirect call) and startup dlopen.
- Best fit for third-party packages and for moving `time.c` out of core.

Use A for code that needs tight integration or static deployment; use B for
the default package path.

## Suggested First Migration: Time

1. Move the wrappers from `runtime/clock.c` to `lib/npl/time/native/time.c`
   (`npl_time_now`, `resolution`, `gmtime`, `localtime`, `mktime`,
   `timegm`), keeping NplVal arguments and struct-view extraction.
2. Build `libnpl_time` and install it beside the time library.
3. Replace the six primitive-table rows with `npl_ffi_load_library` plus
   `npl_ffi_procedure` bindings in `(npl time)`.
4. Keep `define-c-struct` for `timespec`/`tm`; the probe already compiles a
   tiny C program and needs no runtime change.
5. Verify the `time` fixture on nplc/npli without `-l` flags.

## Core Boundary

Keep in the runtime:

- `npl.h` object layout and inline constructors.
- GC, allocator, pointer boxes.
- `foreign-alloc` / typed access / `c-array` / `c-struct` helpers.
- `npl_ffi_load_library` / `npl_ffi_procedure`.

Move out:

- Library-specific clock, POSIX, image, database, and network wrappers.
- Constants expressed only for one library.
- Formatting/parsing policy that can live in NPL.

## Open Questions

- Error raising from a plugin that must not call the runtime: return status
  values and let NPL raise, or export a runtime error callback.
- GC safety for plugins that allocate: require an explicit allocator hook or
  forbid allocation and pass caller-owned buffers.
- Build/install manifest: `package.meta` fields for C sources, system libs,
  include dirs, and shared-library name.
- Cross-compilation: a package's C must match the target, unlike the current
  host-side `define-c-struct` probe.
- Windows naming and loader differences are out of scope for 0.1.3.

## Test Plan

- Package builds in the normal CMake/Ninja flow.
- nplc and npli run the same package behavior without extra user flags.
- The gate checks that importing the package does not add primitive rows.
- A missing native library raises a clear error naming the package and path.

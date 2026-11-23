# Native Packages

Status: accepted direction, 2026-11-23. Core packages use linked (A) mode;
external packages use loaded (B) mode. Both share one NplVal ABI and one
binding manifest. Not yet implemented.

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

## Deployment Modes And Tiering

A and B are two deployment modes of the same native package, not two C APIs.
Both use the same exported `NplVal` entry points and the same binding list.

| Tier | Mode | Who builds/loads it | Why |
|---|---|---|---|
| Core | A: linked | the toolchain build links `libnpl_<name>` into `nplc`/`npli`; the driver auto-links programs importing it | version lock with the runtime, predictable startup, common imports, no path ambiguity |
| External | B: loaded | the package build produces the shared library; `(npl <name>)` resolves it through `npl_ffi_load_library` | optional/separate install, no toolchain relink, platform-specific packages |

### Shared ABI

A package exports NplVal functions, e.g.

```c
NplVal npl_time_now(NplVal which, NplVal timespec_view);
```

`package.meta` (or the `.sld` native declaration) records the name, C symbol,
arity, and result kind once. Core and external builds read the same record.

### A. Core Linked Mode

- `nplc` links `libnpl_<name>.a` (or the shared object) and emits direct
  primitive calls; the native symbols need not be exported for `dlsym`.
- `npli` links the same core libraries, so a core import works in the REPL.
- The driver adds the link request when the library is loaded; the user does
  not pass `-l` for core packages.
- A core package may call `npl_*` runtime helpers, because it is part of the
  same build and runtime version.

### B. External Loaded Mode

- `(npl <name>)` loads `libnpl_<name>` with `npl_ffi_load_library` and binds
  each entry with `npl_ffi_procedure`.
- The package C must not link a second runtime. It may use `npl.h` inline
  layout access and caller-provided buffers; allocation and error raising go
  through a small host callback table (or return status values to NPL).
- The package path is resolved under `NPL_STDLIB_ROOT` / install prefix; the
  loader picks `.dylib` vs `.so`.
- Slightly slower indirect calls and startup dlopen are acceptable for
  optional packages.

### Boundary Rules

- Core must not depend on external packages.
- External packages import core behavior through NPL APIs, not by calling
  core C symbols directly (the host need not export them).
- One C source can produce both an archive for core and a shared object for
  external use; the mode is metadata, not a source fork.
- `NPL_NATIVE_MODE=linked|loaded` overrides the default for tests.

## Suggested First Migration: Time

1. Move the wrappers from `runtime/clock.c` to `lib/npl/time/native/time.c`
   (`npl_time_now`, `resolution`, `gmtime`, `localtime`, `mktime`,
   `timegm`), keeping NplVal arguments and struct-view extraction.
2. Mark the package `tier=core`; build `libnpl_time.a` and the shared object.
3. The toolchain links the core archive into `nplc`/`npli`; the `.sld` uses
   the shared binding metadata, and the six primitive-table rows disappear.
4. Keep `define-c-struct` for `timespec`/`tm`; the probe already compiles a
   tiny C program and needs no runtime change.
5. Verify the `time` fixture on nplc/npli without user `-l` flags, then test
   the same library in loaded mode with `NPL_NATIVE_MODE=loaded`.

## Sample External Package

The first B sample should prove the loader/ABI without adding a system
dependency or a large API. Recommended: `(npl demo crc32)`.

```
lib/npl/demo/crc32/
  crc32.sld
  native/crc32.c
  native/crc32.h
  native/CMakeLists.txt
  package.meta
```

C entry point:

```c
NplVal npl_demo_crc32(NplVal bytes);
```

It reads the bytevector layout directly from `npl.h` (inline tag/length/data
access), returns a fixnum, and links no runtime. The NPL side loads the
shared library and binds the symbol with `npl_ffi_procedure`. Tests are
deterministic:

- `(crc32 #u8())` => 0
- bytes of `"123456789"` => `#xCBF43926`
- a one-byte vector => `#xD202EF8D`

Why this package:

- one stateless function, no allocation, no cleanup;
- bytevector input exercises the NplVal ABI and `npl.h` layout;
- no system library, so it runs on every gate host;
- clearly external (`npl demo`), so it cannot be confused with core.

Second sample: `(npl demo zlib)` wrapping `zlibVersion` and
`compress`/`uncompress` with `-lz`. It exercises a system dependency,
caller-provided output buffers, and status/error handling. Build it only when
zlib development files are present. Do not start with sqlite/curl: their API
surface and lifecycle would obscure the package mechanics.

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

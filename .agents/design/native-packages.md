# Native Packages

Public documentation: `docs/native-packages.md` (pure NPL packages, core A, external B, `package.meta`, and testing).

Status: implemented, 2026-11-23. Core packages use linked (A) mode; external
packages use loaded (B) mode. Both share one NplVal ABI and one binding
manifest. `define-c-struct` layouts are generated once at build/install time.

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
lib/npl/time.sld          public library
lib/npl/time/native/time.c
lib/npl/time/package.meta
lib/npl/demo/crc32.sld
lib/npl/demo/crc32/native/crc32.c
lib/npl/demo/crc32/package.meta
```

`(npl time)` remains the import name. C may use `npl.h` for layout and the
F1/F2 helpers, but must not link a second `libnpl_runtime`.

Implemented packages:

- `(npl time)` -> `libnpl_time`, core A.
- `(npl unix)` / `(srfi 170)` -> `libnpl_unix`, core A.
- `(npl demo crc32)` -> `libnpl_demo_crc32`, external B.
- `(npl demo zlib)` -> `libnpl_demo_zlib`, external B over system zlib.

`(npl platform)` is a thin NPL wrapper over the runtime feature hook, not a
native package. The driver reads `lib/npl/native/core.meta` and links every
listed core archive, so a new core package does not need a driver change.

## Deployment Modes And Tiering

A and B are two deployment modes of the same native package, not two C APIs.
Both use the same exported `NplVal` entry points and the same binding list.

Artifact form (static archive vs shared object) is orthogonal to tier and to
consumption mode (linked vs loaded). Every native package builds and installs
both forms, so `nplc` can produce one static executable or an executable plus
shared objects, while `npli` stays dynamic and never runs a C compiler at
import time. Implementation details are in [link-modes](link-modes.md).

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
4. Keep `define-c-struct` for `timespec`/`tm`; generate its layout cache at
   build time with `nplc --emit=c-structs` so imports do not need a C compiler.
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

Second sample: `(npl demo zlib)` wraps `zlibVersion`, `compressBound`,
`compress`, and `uncompress` with `-lz`. It exercises a system dependency,
caller-provided output buffers, and status/error handling. CMake builds it
only when zlib development files are present. Implemented 2026-11-23:
`lib/npl/demo/zlib.sld`, `lib/npl/demo/zlib/native/zlib.c`, and
`native-zlib` tests. Do not start with sqlite/curl: their API surface and
lifecycle would obscure the package mechanics.

## Core Boundary

Keep in the runtime:

- `npl.h` object layout and inline constructors.
- GC, allocator, pointer boxes.
- `foreign-alloc` / typed access / `c-array` / `c-struct` helpers.
- `npl_ffi_load_library` / `npl_ffi_procedure`.
- The minimal target/feature bootstrap hook used by `cond-expand` (platform
  identity is compiler semantics, not package business logic).

Move out:

- Library-specific clock, POSIX, image, database, and network wrappers.
- Constants expressed only for one library.
- Formatting/parsing policy that can live in NPL.

## Platform Features

NPL already has `cond-expand` as a library declaration and as an expression,
and `(features)` returns a symbol list. The current list is generic: `r7rs`,
`npl`, `full-unicode`, `ratios`, `exact-closed`, `exact-complex`,
`ieee-float`, `posix`, `unix`, the SRFI features, and endianness. It does not
expose the OS, architecture, or a target triple.

Proposed additions (same vocabulary for `cond-expand`, `(features)`, and
`package.meta`):

- OS: `darwin`, `linux` (keep `posix`/`unix`)
- architecture: `aarch64`, `x86_64` (optionally `arm64`/`amd64` aliases)
- width: `64-bit`
- endianness: existing `little-endian` / `big-endian`
- `(npl platform)` library: `platform-triple` and `platform-features`
- triple examples: `aarch64-apple-darwin`, `x86_64-unknown-linux-gnu`

`cond-expand` becomes:

```scheme
(cond-expand
  ((and darwin aarch64) ...)
  (linux ...)
  (else ...))
```

`cond-expand` runs before libraries load, so its feature predicate is a
**bootstrap hook**, not a package. Keep one minimal source for it (for
example `npl_platform_features`/`npl_platform_triple`) so `(features)` and
the frontend predicate cannot drift. `(npl platform)` is then a thin NPL-only
wrapper over that hook; it is not a native package and adds no C of its own.

The alternative is a generated frontend include produced by the build
system. That avoids a primitive but makes the seed relay and cross-target
builds platform-specific, and `(features)` still needs the same data. The
small bootstrap primitive is the cleaner boundary; the rule is “primitive
only for the bootstrap entry point, NPL for the user-facing library”.

The triple is for diagnostics and build metadata; `cond-expand` should use
feature symbols, not string parsing. C code keeps `#ifdef`; `package.meta`
uses the same platform symbols.

Cross-compilation caveat: the triple must describe the target, not the build
host. Today `nplc` is a native compiler, so the two coincide; a future
`--target` option must feed both the triple and the feature list.

## GC And Error Handling

### GC

- B packages should be allocation-free first: read NplVal bytevectors/views,
  write into caller-provided buffers, return immediates or status codes, and
  let NPL construct strings/lists/records.
- Never store an `NplVal` in C globals or foreign memory. If a B package
  must allocate or retain values, the host passes a small callback table
  (`alloc`, `cons`, `string`, `gc-root`, `gc-unroot`); do not link a second
  runtime.
- Core A packages may use runtime allocation and GC helpers, but must follow
  the normal root/write-barrier rules and should still avoid hidden global
  state.
- Opaque handles are explicit resources: return a pointer, release with a
  package function, and use `with-resource`. There are no implicit
  finalizers.

### Errors

- B package C returns status codes (0 success, negative errno-like values)
  and writes results through caller-provided views/buffers; the NPL wrapper
  raises `error` or `file-error` with package/symbol context.
- Do not call `npl_primitive_error` from a B package; the symbol need not be
  exported by the host.
- Loader failures (missing library, missing symbol, wrong ABI version) raise
  at import time with the package name, path, and symbol.
- Validate lengths, null pointers, alignment, and arithmetic overflow in C
  before touching memory. Do not `longjmp` across the FFI boundary or invoke
  NPL callbacks in the first iteration.
- Core A packages may raise directly, but package APIs should use the same
  status convention so A and B behavior stays identical.

## Provisional `package.meta`

Use one Scheme datum so the NPL reader can parse it and build tools can read
the same file. Draft:

```scheme
(package
  (name npl-demo-crc32)
  (library (npl demo crc32))
  (tier external)                 ; core | external
  (version "0.1.0")
  (abi-version 1)
  (abi nplval)
  (native
    (sources ("native/common.c")
             ("native/darwin.c" (platform darwin))
             ("native/linux.c" (platform linux)))
    (headers "native")
    (include-dirs)
    (system-libraries ((darwin "z") (linux "z")))
    (shared-name "npl_demo_crc32")
    (static-name "npl_demo_crc32")
    (bindings
      (crc32 "npl_demo_crc32" 1)))
  (platforms (or darwin linux))
  (dependencies ()))
```

- `tier=core` lets the driver auto-link the static/shared name; `external`
  makes the loader bind the shared name.
- `sources` entries may carry a `(platform feature-requirement)` condition.
- `bindings` maps the NPL name, C symbol, and arity; both modes read it.
- `system-libraries` is conditional so one manifest covers macOS and Linux.
- The format is provisional; keep unknown keys ignored and version it with
  `abi-version`.

Remaining open questions: Windows loader/naming, callback table shape,
target-vs-host triple plumbing, and how a manifest declares non-CMake build
steps.

## Test Plan

- Package builds in the normal CMake/Ninja flow.
- nplc and npli run the same package behavior without extra user flags.
- The gate checks that importing the package does not add primitive rows.
- A missing native library raises a clear error naming the package and path.

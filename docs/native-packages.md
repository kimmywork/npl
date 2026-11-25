# Packages

An NPL package is a library plus whatever implementation it needs. A package
without C is just a `.sld` file (and optional `.scm` includes); C is an
implementation detail, not a requirement.

## Pure NPL packages

```scheme
; lib/npl/demo/hello.sld
(define-library (npl demo hello)
  (export greet)
  (import (scheme base))
  (begin
    (define (greet name)
      (string-append "hello, " name))))
```

Import it as `(npl demo hello)`. The library path is the import name:
`lib/npl/demo/hello.sld` for `(npl demo hello)`. Nested collections use the
same rule (`lib/npl/data/box.sld` for `(npl data box)`).

A pure package is installed by the normal `install(DIRECTORY lib/ ...)` rule.
It needs no `package.meta`, no CMake target, and no compiler change. If a
package grows a native part later, keep the `.sld` import name and add the
native directory and manifest beside it.

An optional `package.meta` may describe even a pure package:

```scheme
(package
  (name npl-demo-hello)
  (library (npl demo hello))
  (tier core)
  (version "0.1.0")
  (platforms (or darwin linux)))
```

The current build reads `package.meta` for native targets; pure packages are
discovered by the normal library loader.

## Native packages

```
lib/npl/time.sld
lib/npl/time/native/time.c
lib/npl/time/package.meta

lib/npl/demo/crc32.sld
lib/npl/demo/crc32/native/crc32.c
lib/npl/demo/crc32/package.meta
```

There are two deployment modes over one `NplVal` ABI:

- **Core (linked, A).** CMake builds `libnpl_<name>.a` and a shared object.
  `nplc --native=static` (the default) links the archive; `nplc
  --native=shared` links the shared object with a relocatable rpath; `npli`
  links the shared object. Importing the library needs no `-l`/`-L` flags.
  Examples: `(npl time)`, `(npl unix)`.
- **External (same artifacts, optional package).** CMake builds both an
  archive and a shared object. The `.sld` declares `(native-package
  "npl_<name>")`; `nplc` links the requested form, while `npli` loads the
  shared object through `(npl native)` and binds exported functions with
  `ffi-load-library` / `ffi-procedure`. Examples: `(npl demo crc32)`,
  `(npl demo zlib)`.

`native.meta` beside the runtime lists each package, its tier, both artifact
names, and any system libraries. The driver links only packages the program
actually imports, so a narrow import set produces a smaller executable.

Artifact form and tier are separate axes. A native package can install both
an archive and a shared object; `nplc` defaults to static linking, while
`npli` uses shared objects and loads external packages dynamically. Both
deliverables are supported for core and third-party packages:

- one self-contained executable linked from archives;
- an executable plus a directory of shared objects (`@loader_path/../lib/npl`
  and `@loader_path/../lib/npl/native` on Darwin, `$ORIGIN/...` on Linux).

`npli` is the dynamic runtime and never invokes a C compiler at run time:
layouts come from `c-struct-layouts.meta`, and `define-c-function` uses a
prebuilt stub shared object.

### `package.meta`

The provisional manifest is a Scheme datum:

```scheme
(package
  (name npl-demo-crc32)
  (library (npl demo crc32))
  (tier external)                 ; core | external
  (version "0.1.0")
  (abi-version 1)
  (abi nplval)
  (native
    (sources ("native/crc32.c"))
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

- `tier=core` packages are linked by the toolchain; `external` packages are
  loaded at run time.
- `sources` may carry per-source platform conditions; `system-libraries` may
  differ by platform.
- `bindings` maps the NPL name, C symbol, and arity for both modes.
- Unknown keys are ignored; `abi-version` versions the format.

CMake currently builds the checked-in native packages from
`lib/CMakeLists.txt`, writes `core.meta` for compatibility, and writes the
full `native.meta` index. It also generates and installs
`c-struct-layouts.meta` with `nplc --emit=c-structs`, so `define-c-struct`
imports do not run a C compiler; `NPL_C_STRUCT_LAYOUTS` adds a
colon-separated list of package cache files ahead of the core cache. The
zlib sample is built only when `find_package(ZLIB)`
succeeds; its static archive carries `-lz` through `native.meta`.

### ABI and safety

- Every exported function has the form `NplVal f(NplVal, ...)` with the
  declared arity. `nplc` calls it directly; `npli` resolves it with `dlsym`.
- Core packages may call runtime helpers and use the normal GC
  roots/write-barriers. External packages must not link a second runtime:
  read bytevectors/struct views, write caller-owned buffers, and return
  status codes; let NPL raise the error.
- `npl.h` is installed at `<prefix>/include/npl/npl.h`. In a checkout it is
  `runtime/npl.h`.
- `NPL_NATIVE_PATH` is a colon-separated search path for external packages.
  Without it, `(npl native)` checks the executable's `../lib/npl/native`
  directory and `./lib/npl/native`.

## Testing packages

- Pure and core packages use `(npl test)` / `(srfi 64)` in
  `src/compiler/tests/`.
- External packages get a `native-*` compiler suite and a
  `tests/fixtures/native-*` expect fixture; optional system
  dependencies are skipped when absent.
- Add the fixture name to `tests/fixtures/manifest.scm`; the NPL runner
  discovers it there.

See [testing](testing.md), [FFI](ffi.md), and [POSIX](posix.md).

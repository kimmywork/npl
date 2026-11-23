# Native Link Modes And Packaging

Status: proposed, 2026-11-23. This decouples the artifact form (static vs
shared) from the package tier (core vs external) and the consumption mode
(linked vs loaded).

## Terms

- **Artifact form**: the file a native package installs, either a static
  archive (`libnpl_<name>.a`) or a shared object (`libnpl_<name>.dylib` /
  `.so`).
- **Consumption mode**: how a program binds the package's C symbols:
  `linked` (the linker records the dependency, or copies the archive into
  the executable) or `loaded` (`dlopen`/`dlsym` at run time).
- **Tier**: where the package is distributed and how it is discovered:
  `core` ships with the toolchain and is auto-linked; `external` is an
  optional package found through `NPL_NATIVE_PATH` / an explicit path.

Tier must not imply artifact form or consumption mode. A core package can
be consumed statically by `nplc` and dynamically by `npli`; an external
package should be able to do the same when both artifacts are installed.

## Product Requirements

1. `nplc` defaults to static native linking and can produce one
   self-contained executable when every imported native package provides an
   archive.
2. `nplc --link=shared` (name to be fixed) produces an executable plus a set
   of shared objects with a relocatable rpath. The executable must run from
   an install prefix without `NPL_NATIVE_PATH`.
3. `npli` is dynamically linked: core packages are shared objects linked at
   build time or loaded from the native directory; external packages are
   loaded through `(npl native)`. It must not require an archive.
4. Every native package declaration names both artifacts. Building both is
   the default; a package may declare only one, and a consumer asking for a
   missing form gets a diagnostic naming the package and the missing form.
5. Pure NPL packages have no artifacts and behave the same in every mode.
6. One `NplVal` ABI and one binding list serve both artifact forms and both
   consumption modes.

## Package Metadata

`package.meta` keeps `tier` for discovery and gains explicit artifacts:

```scheme
(package
  (name npl-demo-crc32)
  (library (npl demo crc32))
  (tier external)
  (native
    (artifacts
      (static "npl_demo_crc32")
      (shared "npl_demo_crc32"))
    (bindings
      (crc32 "npl_demo_crc32" 1))
    ...))
```

`static-name` / `shared-name` may remain as compatibility keys. The build
writes one index (extend `core.meta` or add `native.index`) with each
package's tier, library name, and both artifact names. The driver resolves
an imported package to the form requested by the link mode.

## Consumption Matrix

| Program | Core package | External package |
|---|---|---|
| `nplc` default (static) | link `libnpl_<name>.a` | link the package archive from its index |
| `nplc --link=shared` | link shared object, record rpath | link shared object, record rpath |
| `npli` | shared object / loaded | loaded shared object |

The key gap is external packages: the current `.sld` calls
`load-native-library` directly, so static linking cannot replace that call.
The package declaration must become compiler-visible. The `.sld` should use
one declaration for both modes, for example a `native-package` form that the
driver records; the runtime loader path is only emitted when the selected
mode is `loaded`. `cond-expand` on a compiler feature
(`npl-native-static` / `npl-native-linked`) is the fallback while the
declaration is introduced.

## `npli` And The C Compiler

`npli` is the dynamic runtime. It must not invoke a C compiler while
starting, importing a library, or running a program:

- Native packages are built into shared objects before install.
- `define-c-struct` reads `c-struct-layouts.meta`; the build-time generator
  is the only place that runs a probe.
- `define-c-function` uses a package's prebuilt stub shared object, or
  reports that the stub is missing. It never compiles one on demand.
- `(npl native)` only locates and `dlopen`s a shared object.

Building the `npli` executable itself still uses the host linker, exactly
as building any executable does. That is a toolchain build step, not an
`npli` runtime dependency. A package that ships native code without a
layout cache is incomplete for dynamic use; the cache is part of the
package payload.

`npli` currently records its core native packages as `DT_NEEDED` shared
objects at build time. That is valid dynamic packaging: the delivery
contains `npli` plus those shared objects. Loading core packages on demand
instead is an optional refinement, not a requirement for the no-`cc`
guarantee.

## Packaging A Dynamic Program

A dynamic delivery contains:

```
bin/program
lib/npl/native/libnpl_<package>.dylib
share/npl/lib/...            NPL libraries imported by the program
```

`nplc --link=shared` writes an rpath relative to the executable
(`@loader_path/../lib/npl/native` on Darwin, `$ORIGIN/../lib/npl/native` on
Linux). NPL source libraries remain data in `share/npl/lib`; compiling them
to a separate library format is a later optimization, not a prerequisite
for dynamic native linking.

A static delivery is the executable plus whatever system libraries the
archives need. `otool -L` / `ldd` must show no `libnpl_*` dependency.

## Invariants And Tests

- Static and shared builds of a package export the same symbols and behave
  identically. Test `(npl demo crc32)` and `(npl demo zlib)` through both
  `nplc` modes and through `npli`.
- `nplc` static output must not contain a `libnpl_*` dynamic dependency.
- `nplc --link=shared` output must not contain the package archive and must
  run after moving the install prefix, provided the shared objects and NPL
  sources move with it.
- A package with only a shared artifact fails a static request with a clear
  message, and vice versa.
- Core imports stay compiler-cache based (`c-struct-layouts.meta`); a link
  mode must not reintroduce a C compiler at import time.

## Increments

1. **L1: both artifacts.** Build archives and shared objects for the
   external crc32/zlib samples; extend the index/metadata. No driver change.
2. **L2: static/dynamic selection for core.** Add the `nplc` link-mode
   option and choose archive vs shared for `core.meta` entries. Verify a
   single static binary and a relocatable dynamic bundle.
3. **L3: compiler-visible external packages.** Replace the unconditional
   `load-native-library` path with a declaration the driver records; make
   static linking of external packages work.
4. **L4: packaging and tests.** Install script/manifest for the dynamic
   bundle, `otool`/`ldd` assertions, and a mixed program (core + external)
   in both modes.

# Changelog

All notable changes to NPL. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versions follow
[Semantic Versioning](https://semver.org/) (pre-1.0: minor versions may change
behavior).

## [Unreleased] - target 0.1.2

### Added

- Canonical `(npl ...)` extension libraries with thin `(srfi N)`
  compatibility aliases. Libraries are imported individually; there is no
  aggregate import syntax.
- SRFI 1 lists as `(npl list)` / `(srfi 1)`.
- SRFI 13 strings as `(npl string)` / `(srfi 13)`, with a shared string
  kernel; character-set criteria are not supported because NPL has no
  SRFI 14 library, so a criterion is a character or a predicate.
- SRFI 130 string cursors as `(npl string cursor)` / `(srfi 130)`.
- SRFI 140 immutable strings as `(npl string immutable)` / `(srfi 140)`,
  including UTF-16 conversion. An istring is a frozen NPL string, so the
  base mutators reject it.
- SRFI 133 vectors as `(npl vector)` / `(srfi 133)`.
- SRFI 41 streams as `(npl data stream)` / `(srfi 41)`.
- SRFI 158 generators as `(npl data generator)` / `(srfi 158)`.
- SRFI 161 unifiable boxes as `(npl data unifiable-box)` / `(srfi 161)`.
- SRFI 195 multiple-value boxes as `(npl data multiple-value-box)` /
  `(srfi 195)`; they share the `box`/`unbox`/`set-box!` bindings with
  `(npl data box)`.
- SRFI 47 arrays as `(npl data array)` / `(srfi 47)`, including
  `#nA(...)` literals. Rank-1 arrays are vectors and strings; literals read
  as nested vectors; `make-shared-array` and rank-0/prototype values use an
  array record. The uniform-array prototypes are fill-value descriptors, not
  separate byte-array storage.
- SRFI 111 boxes now hold one or more values, which SRFI 195 requires; the
  one-value behavior is unchanged.
- SRFI 111 boxes as `(npl data box)` / `(srfi 111)`.
- SRFI 196 ranges as `(npl data range)` / `(srfi 196)`.
- SRFI 189 Maybe/Either as `(npl data maybe)`, `(npl data either)`, and
  `(npl data option)` / `(srfi 189)`.
- `(npl hashtable)` as the canonical SRFI 125 implementation; `(srfi 125)`
  is a thin re-export.
- Compiler suites for the new libraries and a native/npli `srfi-core`
  fixture, plus `tools/check_srfi_exports.py` for alias export parity.

### Changed

- Internal libraries moved under `(npl internal ...)`: `(npl primitive)`,
  `(npl frontend)`, and `(npl repl-io)`. The evaluator and eval/load adapter
  moved to `(scheme internal evaluator)` and `(scheme internal eval-runtime)`
  because they implement the standard `(scheme eval/load/repl)` libraries and
  depend on `(scheme base)`. `(npl internal *)` no longer depends on
  `(scheme base)`.
- `(npl list)` is a complete SRFI 1 surface; base bindings are re-exported
  with their original identity so `(scheme base)`, `(npl list)`, and
  `(srfi 1)` can be imported together.
- `(npl string)` re-exports base `string-map`/`string-for-each`, so their
  SRFI 13 optional [start end] arguments are not available on those two
  names; use the other SRFI 13 procedures for substring work.
- Both readers accept `#nA(...)` array literals: the NPL reader used by
  `nplc` and `(npl internal frontend)`, and the C reader used by
  `(scheme read)`, `load`, and the `npli` REPL. `#nA` reads to nested
  vectors. The compiler changed, so `bootstrap/current.ssa` was relayed.

### Fixed

- SRFI 196 `range-first`, `range-split-at`, and `range-reverse` now preserve
  a range's nonzero start offset.
- SRFI 196/189 `assume` is a real precondition check again, shared through
  `lib/npl/data/assume.scm`, instead of a no-op.
- SRFI 189 `either->list` returns `'()` for a Left, as specified.
- Added the missing `range?` export to `(srfi 196)`, and `maybe-if` /
  `two-values->maybe` to `(npl data maybe)`.
- Added `memq`, `memv`, `assq`, and `assv` to the SRFI 1 export surfaces.

### Removed

- The separate `(srfi 69)` compatibility library and its fixture. The SRFI
  125 library remains available as `(npl hashtable)` / `(srfi 125)`.

## [0.1.1] - 2026-11-12

Release-preparation patch for the 0.1.x series. It changes toolchain
behavior and feature detection, not evaluation semantics.

### Added

- A root `README.md`, a GitHub Pages `docs/README.md`, and an MIT `LICENSE`.
- Linux CI (`.github/workflows/ci.yml`) running the gate without the seed
  relay.
- `nplc --version` and `npli --version`; `VERSION` and the CMake project
  version move to 0.1.1.
- Standard feature identifiers in `features` (`full-unicode`, `ratios`,
  `exact-closed`, `exact-complex`, `ieee-float`, `posix`, `unix`), and
  `cond-expand` recognition of installed `(srfi ...)` and user libraries.

### Changed

- Compiler diagnostics go to standard error; standard output is reserved for
  SSA. A failed build no longer relays the partial SSA file, and the stale
  `nplc1:` prefix is now `nplc:`.
- The test gate selects a UTF-8 locale instead of inheriting the caller's
  locale (Guile escapes non-ASCII output under a non-UTF-8 locale).
- `docs/testing.md` stage/suite counts and `docs/toolchain.md` performance
  figures now match the current tree.

### Fixed

- Two Unicode fixtures no longer fail on a host with an empty `LANG`.
- `cond-expand (library ...)` returns true for an importable `(srfi ...)` or
  user library whose file exists but has not been loaded yet.
- Strict C99 builds on glibc define `_DEFAULT_SOURCE`, so the runtime and
  frozen stage0 find `strdup`; the Linux gate builds with Ubuntu 24.04's
  clang 18.
- The `strings` fixture marks the Guile 3.0.8 and 3.0.9 `string-for-each`
  defect as a known oracle defect, so the Ubuntu 24.04 CI gate does not
  report it as an NPL failure.

[0.1.1]: https://github.com/ng-lang/npl/releases/tag/v0.1.1

## [0.1.0] - 2026-11-12

First tagged milestone: the language stage is complete. NPL is a
self-hosting R7RS-small implementation with a native compiler (`nplc`, via
QBE) and an evaluator (`npli`) that share one frontend. Conformance claim and
evidence: [docs/r7rs-conformance.md](docs/r7rs-conformance.md).

### Language and frontend

- R7RS-small core syntax and semantics, with hygienic `syntax-rules`
  (ellipsis depth, custom ellipsis, literal matching by binding,
  definition-producing macros, `let-syntax`/`letrec-syntax`).
- Proper tail calls, first-class continuations, `dynamic-wind`,
  `with-exception-handler`/`raise`/`guard`, multiple values, parameter
  objects, promises, `define-record-type`, R7RS `define-library`.
- Records: transparent, generative, structural `equal?`, printed as
  `#<point x: 1 y: 2>`, functional update with `record-update`.
- Pattern matching: `match`, `match-lambda`, `match-lambda*`, `match-let`,
  `match-let*`, `match-letrec`, `match-define` (Shinn's matcher), available
  without imports and as `(npl match)`.
- Unicode strings and characters; datum labels in `read` and cycle-aware
  output.
- Catchable error objects for every safe primitive and library argument
  check, identical on native and npli.

### Standard library

- All R7RS-small libraries, every exported name (556/556), including
  `(scheme r5rs)`; behavior checked by chibi's `r7rs-tests.scm` (1208 passed,
  4 classified differences) and 41 three-way fixtures against Guile 3.
- SRFI 125 hash tables (runtime-backed: eq, eqv, equal, string, general) and
  SRFI 69; `(npl record)`, `(npl match)`, `(npl ffi)`, `(npl process)`.

### Compiler and seed

- Self-hosting compiler; the canonical compiler is checked in as QBE SSA
  (`bootstrap/current.ssa`) and must reproduce itself (gen1 == gen2 == gen3).
  A permanent, frozen bootstrap chain (stage0, `bootstrap/bootstrap.ssa`)
  recovers it from C.
- Native code: inline fixnum arithmetic and comparisons, inline
  `car`/`cdr`/vector and record access, direct self tail calls as jumps,
  direct entries for direct-safe functions and lambdas, flat closures.
- Control conversion keeps simple `if` tests and `let`s with simple inits in
  place; self-compile went from 98.2 G to 11.1 G instructions during the
  final stage, and the seed from 316,843 to 261,586 lines.
- Hashed lookups in the frontend (predeclared globals, syntax marks) and the
  backend (function, arity, global tables).

### Runtime

- Generational, non-moving collector with a 1 MiB nursery, write barrier,
  conservative stack scan, and `NPL_GC_VERIFY`; exact integers (bignums),
  rationals, inexact and rectangular complex numbers.

### Evaluator

- `npli`: closure-compiling evaluator over the shared core AST, with the
  standard library bound to compiled procedures; REPL and script mode.

### Toolchain and FFI

- Unified `nplc` driver (SSA, assembly, object, executable, C stubs) and an
  install layout usable from any prefix; Linux parity in a Debian container.
- C extensions: `define-c-primitive`, `%c-call`, `%c-procedure`
  (`NplVal` ABI), and `define-c-function` with generated stubs for plain C
  types, pointers, `with-resource`; `npli -l` loads shared libraries.

### Documentation

- Live: [toolchain](docs/toolchain.md), [testing](docs/testing.md),
  [extending](docs/extending.md), [ffi](docs/ffi.md),
  [r7rs-conformance](docs/r7rs-conformance.md).
- Completed designs and plans are archived under `.agents/archive/`; the
  per-day history is `.agents/worklog/`.

### Known limitations

- Not implemented by decision: `syntax-case`, an object system, keyword
  arguments, record inheritance; SRFI 1 / 13 / 113 list, string, and set
  libraries. See the conformance document for routes.
- Native and npli differ in operand evaluation order when an operand has a
  side effect seen by another (R7RS leaves the order unspecified).

[0.1.0]: https://github.com/ng-lang/npl/releases/tag/v0.1.0

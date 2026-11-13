# Extending NPL

Status: frozen at the language milestone, 2026-11-12. The four ways to extend
the language and its libraries, in increasing order of cost. "Relay needed"
means `tools/update_current_ssa.sh` (see [testing](testing.md#the-seed-and-the-relay)).

| You want to add | Touch | Relay |
|---|---|---|
| a library (most SRFIs, helpers) | `lib/` | no |
| a C primitive used by a library | `lib/` + a C file | no |
| a binding to an existing C library | program source + C | no |
| derived syntax available without imports | `src/compiler/frontend/` | yes |
| a runtime primitive needed by the compiler or `(scheme base)` | `runtime/` + `primitive-table.npl` | yes |

## 1. A library

A library is one file, `lib/<prefix>/<name>.sld`, containing exactly one
`define-library` whose name matches the path:

```scheme
; lib/srfi/42.sld
(define-library (srfi 42)
  (export every fold)
  (import (scheme base))
  (include "42.scm"))       ; helpers, relative to this file
```

Rules:

- **Prefix chooses the search root.** `(scheme …)`, `(npl …)`, and `(srfi …)`
  resolve against the standard library root (`--stdlib-root`,
  `NPL_STDLIB_ROOT`, or the install layout); every other name resolves
  relative to the importing program's directory
  (`library-installed-key?`, `library-path-for-key` in
  `src/compiler/frontend/library.npl`).
- **Exactly one `define-library` per file**, and its name must equal the path
  key (parts joined by `/`): the loader rejects zero or two forms and a
  mismatched name.
- **Helpers** go in a sibling `.scm` pulled in by `(include "x.scm")`, or in
  the `begin` of the library. `include-library-declarations` and
  `cond-expand` (with `library`, `and`, `or`, `not`, `else`, features
  `r7rs`, `npl`, `full-unicode`, `ratios`, `exact-closed`, `exact-complex`,
  `ieee-float`, `posix`, and `unix`) work.
- **A new `(scheme …)` name must be listed in `lib/scheme/libraries.scm`.**
  That file is the index used by `cond-expand (library ...)` and
  `library-standard-available?`. `(npl …)` and `(srfi …)` names do not need
  an index entry: `cond-expand (library ...)` also checks whether the
  library file exists. The feature list belongs in
  `lib/scheme/text-io.scm` (`features`) and `library-feature-name?` in
  `src/compiler/frontend/library.npl`; keep the two in sync.
- **NPL extension names are canonical; SRFI numbers are aliases.** Put the
  implementation in `lib/npl/…` and keep the `(srfi N)` library as a thin
  re-export. For example, `(npl list)` owns SRFI 1 and `(srfi 1)` imports and
  re-exports it; `(npl data range)` owns SRFI 196. Do not duplicate the
  procedure bodies. A canonical library is complete for its standard: names
  shared with `(scheme base)` are re-exported from the same binding, not
  redefined, so importing `(scheme base)` and `(npl list)` together is valid.
  The SRFI 189 split is the exception: `(npl data maybe)` and
  `(npl data either)` are disjoint subsets; use `(npl data option)` or
  `(srfi 189)` for the complete surface.
- **Import libraries individually.** R7RS import sets do not support
  grouping inside a library name, so `(npl data (set range))` is not valid.
  Use `(import (npl data set) (npl data range))`; there are no aggregate
  libraries in 0.1.2.

### Library layers and dependency direction

Use the library graph, not directory labels, to keep dependencies acyclic:

| Layer | Libraries | May import |
|---|---|---|
| L0 runtime | `(npl internal primitive)` | nothing |
| L1 core | `(npl internal frontend)`, `(npl internal repl-io)` | L0 |
| L2 standard | `(scheme *)` and `(scheme internal *)` | L0, L1, lower `(scheme *)` |
| L3 extensions | `(npl list)`, `(npl string)`, `(npl string cursor)`, `(npl string immutable)`, `(npl vector)`, `(npl hashtable)`, `(npl data ...)`, `(npl ffi)`, `(npl match)`, `(npl record)`, `(npl process)` | L0-L2, lower L3 |
| L4 SRFI aliases | `(srfi *)` | L3 only |

- `(npl internal *)` must not import `(scheme *)`. `(npl internal primitive)`,
  `(npl internal frontend)`, and `(npl internal repl-io)` satisfy this.
- The evaluator and its eval/load adapter implement `(scheme eval)`,
  `(scheme load)`, and `(scheme repl)`, so they live under
  `(scheme internal evaluator)` and `(scheme internal eval-runtime)`. They may
  import `(scheme base)`; that is why they are not `(npl internal *)`.
- `(scheme *)` must not import `(npl list)`, `(npl hashtable)`, or another
  extension library. If a standard library needs a utility, put it in L0/L1
  or implement it in the standard library.
- Extension libraries may import a lower L3 library, but must not form a
  mutual or circular dependency. For example, `(npl data range)` may import
  `(npl list)`; `(npl list)` must not import `(npl data range)`. If two
  libraries need each other, extract their shared code into a lower-level
  library.
- `(srfi *)` is a leaf: it re-exports `(npl *)` and is not imported by other
  libraries.
- All library nodes must form a DAG. A period or library rename must not
  introduce a back-edge.
- Export with `export` or `(rename internal external)`; `(scheme base)` maps
  `integer-add` to `+` this way.

Cover the behavior with a fixture or a compiler suite ([testing](testing.md)).
A library change needs no relay.

## 2. A C primitive (a new runtime function for a library)

```scheme
(define-c-primitive (string-trim-left (s string) (chars string)) string)
```

- Names the C function by the runtime rule unless a symbol is given:
  `npl_` is kept or added, `->` becomes `_to_`, a final `?` becomes `_p`,
  `!` is dropped, `-` becomes `_` (`string-trim-left` →
  `npl_string_trim_left`).
- Types: `any pair fixnum index integer number real char string symbol
  vector bytevector procedure port boolean`, plus `values` as a result type
  (`npl_make_values(count, array)`). Types are recorded, not checked; the C
  function validates its arguments with `npl_primitive_error(who, message,
  irritant)`.
- Top level only (program body or library `begin`), must precede uses, must
  not rebind a name.
- `(%c-call "sym" (types ...) args ...)` and `(%c-procedure "sym" (types ...))`
  call or take a procedure value without binding a name.
- Under npli a declared primitive resolves through `C-PRIMITIVE-HOOK`
  (`lib/scheme/internal/evaluator-session.scm`); under nplc it is a direct C call.

The C side and the full type list are in [C Functions From NPL](ffi.md).

### When a primitive belongs in the shared table instead

`src/compiler/frontend/primitive-table.npl` is the compiler's own registry.
Add a row there only when the primitive is needed **before any import**: by
the compiler itself, by `(scheme base)`'s implementation, by derived syntax,
records, or hash tables. Those rows are exported by `(npl internal primitive)`,
participate in control conversion (every row is synchronous), and changing
them changes the seed, so they need a relay. Everything else — anything a
user program or an ordinary library can declare — belongs in
`define-c-primitive`.

## 3. A binding to an existing C library

Use level A (`NplVal` ABI, hand-written against `include/npl/npl.h`) for full
control, or level B (`define-c-function`) for plain C types:

```scheme
(define-c-function strlen "strlen" (string) -> size_t)
(nplc prog.scm -L . -l mylib -o prog)
```

Stubs are generated by `nplc` for executables, and by
`nplc --emit=c-stubs prog.scm -o stubs.c` for `npli`; `npli -l ./libmylib.so`
loads a shared library at run time. Pointers are boxed addresses the collector
never follows or frees; `(npl ffi)` provides `with-resource` for release
(exactly once, including on raise, continuation escape, and `exit`).

This needs no compiler change and no relay. Complete reference, including
`pointer-out`, multiple values, and the portability limits:
[C Functions From NPL](ffi.md).

## 4. Derived syntax (macros available without imports)

Two levels, both in `src/compiler/frontend/`:

**A `syntax-rules` macro** — add a `define-syntax` to the string
`DERIVED-SYNTAX-SOURCE` in `derived.npl`, or to `MATCH-SYNTAX_SOURCE` in
`derived-match.npl` (both are parsed at expansion startup by
`expand-derived-syntax`). Keywords are pre-bound before any rule is parsed,
so a macro may use one defined later in the string. This is how `cond`,
`guard`, `define-record-type`, `parameterize`, and `match` are defined.

**An expansion-time core form** — when `syntax-rules` cannot do the work
(count items, classify a datum, or produce a definition that must reach the
enclosing body), add a handler in the `expand-keyword-or-application` chain
in `expand.npl`, implement it beside the existing ones
(`expand-record.npl`, `expand-match.npl`), and have the macro call it.
Existing forms: `%record-index`, `%record-make`, `%match-length`,
`%match-literal`, `%match-identifier?`, `%match-member?`,
`%match-ellipsis?`, `%c-call`, `%c-procedure`.

**Export it from `(npl internal primitive)`** — add the keyword to
`PRIMITIVE-MACRO-SOURCE` in `library-primitives.npl`, otherwise
`(import (npl internal primitive))` and any `(npl …)` shim fails with
`missing primitive syntax binding`. Optionally add a shim library
(`lib/npl/match.sld`) and a fixture.

**Ordering constraint.** npli compiles the frontend as the `(npl internal frontend)`
library, and in a library body a name produced by macro output is not bound
before the macro use. So a record type — or anything a macro defines — must
appear **before its first use** in the frontend sources. This is why
`syntax-transformer` is defined above `parse-syntax-rules`
(`syntax.npl`); nplc's self-compile passes either way, but
`tools/build_toolchain.sh` fails if the order is wrong.

Derived-syntax changes are compiler-source changes: relay, and a test that
fails on the previous seed ([testing](testing.md)).

## What not to do

- Do not add a second implementation of a facility that exists: check
  `lib/scheme/`, `lib/srfi/`, the derived syntax, and the runtime tables
  first (AGENTS.md "Reuse before reinvention").
- Do not implement language or library policy in C; C is for runtime
  mechanisms, OS and libc wrappers, and FFI boundaries (AGENTS.md
  "NPL-first implementation").
- Do not make `bootstrap/bootstrap.ssa` or `bootstrap/compiler/` compatible
  with the current runtime; they are frozen together with
  `bootstrap/runtime/`.

# NPL

NPL is a self-hosting R7RS-small Scheme implementation with a native
compiler (`nplc`, through QBE), an evaluator and REPL (`npli`), and one
shared frontend for both execution paths.

Status: **0.1.1**. The 0.1.0 milestone completed the R7RS-small language
stage; 0.1.x is release hardening. See the
[conformance statement](docs/r7rs-conformance.md) for exactly what is
claimed and how it is checked.

## Highlights

- R7RS-small core and all standard libraries; 556/556 exported names, with
  behavior checked by 41 three-way fixtures and chibi's independent suite.
- Hygienic `syntax-rules` (ellipsis depth, custom ellipsis, definition-
  producing macros), proper tail calls, first-class continuations,
  `dynamic-wind`, exceptions, multiple values, promises, and parameters.
- Native code: inline fixnum arithmetic, direct self tail calls, direct
  function entries, flat closures, and a generational non-moving collector.
- Exact integers (bignums), rationals, inexact and rectangular complex
  numbers; Unicode 15.1 strings, characters, ports, `read`, and `write`.
- Records, pattern matching, hash tables as `(npl hashtable)` / `(srfi 125)`, and a C FFI
  with typed `define-c-function` stubs and `(npl ffi)` resources.
- The compiler is self-hosting: `bootstrap/current.ssa` is the checked-in
  recovery artifact and must reproduce itself in a gen1/gen2/gen3 relay.

## Libraries

Beyond R7RS-small, extension libraries use canonical `(npl ...)` names and
keep the SRFI number as a compatibility alias. Import each library
individually; R7RS import sets do not support grouping inside a library name.

```scheme
(import (npl list) (npl data box) (npl data range) (npl data maybe))
;; portable aliases
(import (srfi 1) (srfi 111) (srfi 196) (srfi 189))
```

Current extensions include `(npl hashtable)` / `(srfi 125)`,
`(npl list)` / `(srfi 1)`, `(npl string)` / `(srfi 13)`,
`(npl string cursor)` / `(srfi 130)`, `(npl string immutable)` / `(srfi 140)`,
`(npl vector)` / `(srfi 133)`, `(npl data stream)` / `(srfi 41)`,
`(npl data generator)` / `(srfi 158)`, `(npl data array)` / `(srfi 47)`, `(npl data unifiable-box)` / `(srfi 161)`,
`(npl data multiple-value-box)` / `(srfi 195)`, `(npl data box)` / `(srfi 111)`,
`(npl data range)` / `(srfi 196)`, and the SRFI 189 family:
`(npl data maybe)` and `(npl data either)` are disjoint subsets, while
`(npl data option)` or `(srfi 189)` provides the complete surface. Canonical
libraries re-export shared bindings rather than redefining them, so they can
be imported together with `(scheme base)` without conflicts. Rank-1 array
literals such as `#1A(1 2)` read as ordinary vectors. Rank-n literals such as
`#2A((1 2) (3 4))` read as tagged flat vectors
`#(npl-array-literal-5f8d shape data)` in both `nplc` and `npli`. The tag
is reserved for literals: a user vector that exactly matches the
tag/shape/data layout is treated as an array. Arrays made by `make-array`
are opaque records and therefore are not `vector?`; use `array-ref` to
compare their contents with a literal. SRFI 140 is the exception: it deliberately gives the R7RS string names istring-returning
behavior, so import it with `(except (scheme base) ...)` for the names it
redefines. SRFI 13 and SRFI 130 also overlap, as do SRFI 13 and
`(scheme char)` for `string-upcase`/`string-downcase`; import one of the
overlapping libraries, or select the needed names with `only`/`except`.

## Quick start

Prerequisites: CMake 3.20+, Ninja, a C99 compiler (Apple clang, Clang, or
GCC), and `make` (for the vendored QBE). Python 3 and Guile 3 are needed for
parts of the test gate and benchmarks, not for the toolchain itself. QBE is
vendored under `external/qbe-1.3`.

```bash
./tools/build_toolchain.sh          # build build/bin/nplc and build/bin/npli

./build/bin/npli                    # interactive REPL
./build/bin/nplc hello.scm -o hello # compile a program
./hello
./build/bin/nplc --version
```

A program is a sequence of top-level forms; imports are ordinary R7RS
libraries:

```scheme
(import (scheme base) (scheme write))

(define (fact n)
  (if (= n 0) 1 (* n (fact (- n 1)))))

(display (fact 20))
(newline)
```

Install to a prefix with:

```bash
cmake --install build --prefix /path/to/prefix
```

The installed `nplc` and `npli` locate QBE, the runtime, and the standard
libraries relative to themselves; see the [toolchain guide](docs/toolchain.md).

## Platforms

macOS arm64 and Linux (x86_64 and aarch64) are supported. Windows is not
supported, and there is no cross-compilation path at this stage.

## Documentation

- [Toolchain and install layout](docs/toolchain.md)
- [Verification and the gate](docs/testing.md)
- [R7RS-small conformance](docs/r7rs-conformance.md)
- [Extending NPL](docs/extending.md)
- [C functions and FFI](docs/ffi.md)
- [R7RS-small reference](docs/reference/r7rs.html) and
  [SRFI 125 reference](docs/reference/srfi-125.html)

Deliberate omissions (R7RS-large features, `syntax-case`, keyword arguments,
record inheritance, SRFI 113, and separate compilation) are listed in
the [conformance statement](docs/r7rs-conformance.md#deliberately-outside-the-language-stage).

## Testing

```bash
tools/run_tests.sh --skip-current-fixed-point  # gate without the seed relay
tools/run_tests.sh                             # gate with the seed relay
tools/check_linux.sh                           # the same gate in a Debian container
tools/run_chibi_suite.sh --native              # external conformance suite
```

The gate builds the toolchain, checks the permanent bootstrap chain, runs the
compiler suites and contracts, exercises the shared native/`npli`/Guile
fixtures, and checks the runtime and GC. See
[Verification](docs/testing.md) for stage details and fixture rules.

## License

MIT. See [LICENSE](LICENSE).

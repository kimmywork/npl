# NPL

NPL is a self-hosting R7RS-small Scheme implementation with a native
compiler (`nplc`, through QBE), an evaluator and REPL (`npli`), and one
shared frontend for both execution paths.

Status: **0.1.1**. The 0.1.0 milestone completed the R7RS-small language
stage; 0.1.x is release hardening. See the
[conformance statement](/docs/r7rs-conformance.md) for exactly what is
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
- Records, pattern matching, SRFI 125 and SRFI 69 hash tables, and a C FFI
  with typed `define-c-function` stubs and `(npl ffi)` resources.
- The compiler is self-hosting: `bootstrap/current.ssa` is the checked-in
  recovery artifact and must reproduce itself in a gen1/gen2/gen3 relay.

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
libraries relative to themselves; see the [toolchain guide](/docs/toolchain.md).

## Platforms

macOS arm64 and Linux (x86_64 and aarch64) are supported. Windows is not
supported, and there is no cross-compilation path at this stage.

## Documentation

- [Toolchain and install layout](/docs/toolchain.md)
- [Verification and the gate](/docs/testing.md)
- [R7RS-small conformance](/docs/r7rs-conformance.md)
- [Extending NPL](/docs/extending.md)
- [C functions and FFI](/docs/ffi.md)
- [R7RS-small reference](/docs/reference/r7rs.html) and
  [SRFI 125 reference](/docs/reference/srfi-125.html)

Deliberate omissions (R7RS-large features, `syntax-case`, keyword arguments,
record inheritance, SRFI 1/13/113, and separate compilation) are listed in
the [conformance statement](/docs/r7rs-conformance.md#deliberately-outside-the-language-stage).

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
[Verification](/docs/testing.md) for stage details and fixture rules.

## License

MIT. See [LICENSE](/LICENSE).

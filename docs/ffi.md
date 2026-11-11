# C Functions From NPL

NPL code can call C functions written against the runtime ABI: each takes
and returns `NplVal`. A declaration in a program or library makes such a
function an ordinary NPL procedure, with no compiler change and no seed
relay. Under `nplc` calls compile to a direct C call, as for built-in
primitives; `npli` looks the function up when the declaration is evaluated.

An existing C library with plain C types (`int`, `double`, `const char *`,
pointers) is declared with `define-c-function` instead; see "Typed C
Functions".

Supported by `nplc` since 2026-10-22 (P2b); `nplc -l`/`-L` and `npli -l`
since 2026-10-23; pointers and `define-c-function` since 2026-10-27. Not yet
supported: callbacks into NPL, structs, arrays. See "Limitations" and the
plan in [design-primitives](../.agents/archive/design/design-primitives.md).

## Declaring A Function

```scheme
(define-c-primitive (name (param type) ...) result-type ["c-symbol"])
```

- `name` becomes a procedure taking one argument per parameter. It can be
  called, passed as a value (`(map clamp xs los his)`), and exported from a
  library; `import` with `only`, `prefix`, `rename`, and `except` treat it
  like any other binding.
- `c-symbol` is the C function's name. Without it, the name follows the
  runtime naming rule: an `npl_` prefix is kept as is; otherwise the symbol
  is `npl_` plus the name with `->` written `_to_`, a final `?` written `_p`,
  `!` dropped, and `-` written `_`. So `string-trim` is `npl_string_trim` and
  `char-upcase?` is `npl_char_upcase_p`. The symbol must be a C identifier.
- Types (both parameter and result) come from this set: `any`, `pair`,
  `fixnum`, `index`, `integer`, `number`, `real`, `char`, `string`, `symbol`,
  `vector`, `bytevector`, `procedure`, `port`, `boolean`. They are recorded
  for later use and not checked; the C function validates its arguments.
- The result type `values` declares a function that may return several
  values (`npl_make_values(count, array)`). Such a call is received by
  `call-with-values` or `let-values`; used where one value is required, it
  raises "value context: expected 1 value", as in npli. Other result types
  must return exactly one value. Example: `values.npl` and `values.c` in
  `src/compiler/tests/c-extension/`.
- The declaration is valid at top level only (program body or library
  `begin`) and must precede uses of its name. The name must not already be
  bound in that scope.
- Declaring a symbol that is already registered, by the runtime's built-in
  table or an earlier declaration, reuses it. The operand count must match.

Two expression forms do the same without binding a name:

```scheme
(%c-call "demo_clamp" (fixnum fixnum fixnum) x 0 10)  ; call
(%c-procedure "demo_clamp" (fixnum fixnum fixnum))    ; procedure value
```

## Writing The C Side

```c
#include "npl.h"

/* (clamp x lo hi) on fixnums. */
NplVal demo_clamp(NplVal x, NplVal lo, NplVal hi)
{
    if (!npl_is_fixnum(x)) npl_primitive_error("clamp", "expected a fixnum", x);
    if (!npl_is_fixnum(lo)) npl_primitive_error("clamp", "expected a fixnum", lo);
    if (!npl_is_fixnum(hi)) npl_primitive_error("clamp", "expected a fixnum", hi);
    int64_t value = npl_fixnum_value(x);
    if (value < npl_fixnum_value(lo)) return lo;
    if (value > npl_fixnum_value(hi)) return hi;
    return x;
}
```

Rules, all from `npl.h` (installed as `PREFIX/include/npl/npl.h`; in a
checkout, `runtime/npl.h`):

- **Signature.** `NplVal f(NplVal, ...)` with exactly the declared number of
  arguments, returning one `NplVal`. Return `npl_nil()` when there is no
  useful result. Values: `npl_fixnum(n)`/`npl_fixnum_value(v)`,
  `npl_true()`, `npl_false()`, `npl_char(c)`, `npl_string(bytes, length)`,
  `npl_cons(a, d)`, and the predicates `npl_is_fixnum`, `npl_is_char`,
  `npl_is_ptr`, `npl_is_nil`. Other runtime entry points declared in
  `npl.h` (for example `npl_string_append`, `npl_vector_ref`) can be called
  directly.
- **Errors.** Reject a bad argument with
  `npl_primitive_error(who, message, irritant)`. It does not return: it
  raises an R7RS error object that `guard` and `with-exception-handler`
  catch, with message `"who: message"` and irritants `(irritant)`. Do not
  `exit` or `abort` for argument errors. `npl_primitive_file_error` raises a
  `file-error?` object the same way.
- **Garbage collection.** The collector is non-moving and scans the C stack
  conservatively, so `NplVal` locals and arguments stay valid across
  allocation (`npl_cons`, `npl_string`, ...). Two cases need care:
  - A value kept beyond the call, in a C global or a `malloc` block, must be
    registered with `npl_gc_root(&slot)` (and `npl_gc_unroot` when done).
  - A store of an `NplVal` into an existing heap object must be followed by
    `npl_gc_write_barrier(object, value)`, as the runtime's own mutators do.
    Without it a minor collection can free the stored value. Running the
    program with `NPL_GC_VERIFY=1` checks for missing barriers.
- **C pointers.** `npl_pointer(address)` boxes a C address as an NPL
  pointer value and `npl_pointer_address(value, who)` returns it, raising
  "who: expected a pointer" for any other value. The collector never
  follows or frees the address; see "Pointers And Release".
- **Synchronous only.** The function must return normally or raise through
  `npl_primitive_error`. It must not call NPL procedures, capture
  continuations, or `longjmp` itself.

## Building

Build the C functions into a library and name it with `-l`; `-L` adds a
search directory. Both take the value attached (`-lclamp`) or as the next
argument (`-l clamp`), and are passed to the linker in order, before the
runtime archive:

```bash
cc -std=c99 -I path/to/npl/runtime -c clamp.c      # clamp.o
ar rcs libclamp.a clamp.o
nplc main.npl -L . -l clamp -o main
```

A library the C functions themselves use is linked the same way
(`-l sqlite3`). Alternatively, compile the program to an object and link it
yourself:

```bash
nplc -c main.npl                                   # main.o
cc -o main main.o clamp.o PREFIX/lib/npl/libnpl_runtime.a -lm
```

Use `-I PREFIX/include/npl` for an installed toolchain, or the checkout's
`runtime/`. On macOS `-lm` is part of libSystem and may be omitted.

### Running Under npli

Build the same C file as a shared library and load it with `npli -l`
before the program:

```bash
cc -std=c99 -I path/to/npl/runtime -dynamiclib -undefined dynamic_lookup \
    -o libclamp.dylib clamp.c                      # macOS
cc -std=c99 -I path/to/npl/runtime -shared -fPIC -o libclamp.so clamp.c   # Linux
npli -l ./libclamp.dylib main.scm
```

- The path goes to `dlopen` unchanged: a name without a slash
  (`-l libclamp.so`) is searched for by the system loader
  (`LD_LIBRARY_PATH`, `DYLD_LIBRARY_PATH`), unlike `nplc -l clamp`.
- `-l` may be repeated; each library is loaded with global symbols in
  order, so a later library can use an earlier one's functions.
- The library calls runtime functions (`npl_cons`, `npl_primitive_error`)
  through npli itself, which exports the whole runtime. Do not link the
  library against `libnpl_runtime.a`: it would get a second runtime with
  its own heap.
- A declaration binds its name when evaluated, so the function must be
  loaded by then. `%c-call` and `%c-procedure` also work in `eval`.
- npli calls functions of at most 8 arguments.

The complete example is `src/compiler/tests/c-extension/` (`main.npl`,
`clamp.c`, `expected.out`). `tools/check_full_compiler.sh` links it by hand,
`tools/check_driver.sh` through `-L`/`-l`, and `tools/check_repl.sh` runs it
under `npli -l`.

## Typed C Functions

```scheme
(define-c-function name "c_symbol" (type ...) -> result)
```

`name` becomes a procedure calling the C function `c_symbol`, declared with
plain C types; the C code needs no `npl.h`. Like `define-c-primitive`, the
declaration is top level only, and `name` can be called, passed as a value,
and exported.

| Type | C type | NPL argument | NPL result |
|---|---|---|---|
| `int`, `long`, `int64` | `int`, `long`, `int64_t` | exact integer in range | exact integer |
| `size_t` | `size_t` | non-negative exact integer in range | exact integer |
| `double` | `double` | real number | flonum |
| `bool` | `bool` | any value; only `#f` is false | `#t` or `#f` |
| `string` | `const char *` | string without a null character, passed as a UTF-8 copy | a copy of the text; `NULL` is `#f` |
| `pointer` | `void *` | pointer value | pointer value |
| `pointer-out` | `void **` | none: the stub passes a local slot | the slot's pointer, after the result |
| `void` | `void` (result only) | | the unspecified value |

A function with `pointer-out` arguments, or with a `void` result and two or
more of them, returns several values: the C result unless `void`, then each
output in order.

```scheme
(define-c-function sqlite-open "sqlite3_open" (string pointer-out) -> int)
(define-c-function sqlite-close "sqlite3_close" (pointer) -> int)

(let-values (((status db) (sqlite-open "x.db")))
  (with-resource (db db sqlite-close)
    ...))
```

Each argument is checked before the C function runs; a bad one raises an
error object such as `add: expected an exact integer that fits C int`.
String copies are freed after the call, so a C function must not keep a
`const char *` argument. A `string` result is copied; ownership of the C
memory stays with the library.

### Building With Stubs

A generated C stub, `npl_cfn_<c_symbol>`, converts the arguments, calls the
function, and boxes the results; one file holds the stubs of a program and
the libraries it imports.

- `nplc` builds an executable's stubs itself: it generates them, compiles
  them with `-I PREFIX/include/npl` (or `NPL_INCLUDE`), and links them
  before the `-l` libraries. Only the C library is named on the command
  line: `nplc main.npl -l sqlite3`.
- `npli` loads them from a shared library built from
  `nplc --emit=c-stubs`:

```bash
nplc --emit=c-stubs main.scm -o stubs.c
cc -std=c99 -I PREFIX/include/npl -shared -fPIC -o libstubs.so stubs.c -lsqlite3
npli -l ./libstubs.so main.scm
```

On macOS use `-dynamiclib -undefined dynamic_lookup` instead of `-shared
-fPIC`. Rebuild the stub library after changing a declaration. A program
without `define-c-function` has an empty stub file.

The example built by the gate is `src/compiler/tests/c-extension/typed.npl`
with the plain C functions in `typed.c` and libc's `strlen` and `abs`.

## Pointers And Release

`(npl ffi)` exports pointer operations and `with-resource`:

| Procedure | Result |
|---|---|
| `(pointer? x)` | `#t` for a pointer value |
| `(pointer-null? p)` | `#t` for the null address |
| `(null-pointer)` | a pointer to address 0 |
| `(pointer->integer p)` | the address as an exact integer |

`eqv?` and `equal?` compare pointers by address; `write` shows
`#<pointer 0x...>`. There is no finalizer: the collector frees the pointer
box, never what it points to. Release a C resource by calling the C
library's own function, deterministically with `with-resource`:

```scheme
(import (scheme base) (npl ffi))
(define-c-primitive (db-open (path string)) any "demo_db_open")
(define-c-primitive (db-close (db any)) any "demo_db_close")

(with-resource (db (db-open "x.db") db-close)
  (query db))
```

`(with-resource (name init release) body ...)` binds `name` to `init`, runs
`body`, and calls `(release name)` exactly once when control leaves `body`:
normal return, a raise caught outside it, an escape through a
continuation, or `exit` (R7RS 6.14; `emergency-exit` runs nothing).
Re-entering `body` through a continuation does not acquire or release the
resource again, so a body re-entered after its release must not use it.
`src/compiler/tests/c-extension/pointers.npl` covers each case.

## Diagnostics

Errors from `nplc` at compile time, and from `npli` (status 70) when the
form is evaluated:

| Message | Cause |
|---|---|
| `define-c-primitive is valid at top level only` | declaration inside a procedure body or expression |
| `unknown C primitive type: T` | type outside the set above |
| `invalid C symbol: S` | symbol is not a C identifier |
| `define-c-primitive: already bound: N` | name already defined or imported in that scope |
| `C primitive declaration conflicts with registered S` | symbol is a built-in or earlier declaration with another operand count, kind, or `values` result |
| `%c-call: argument count differs from its types` | type list and arguments differ in length |

`define-c-function` adds:

| Message | Cause |
|---|---|
| `define-c-function is valid at top level only` | declaration inside a body or expression |
| `malformed define-c-function: ...` | wrong shape, or no `->` before the result |
| `unknown C function type: T` | type outside the table above |
| `void is valid only as a define-c-function result` | `void` argument |
| `pointer-out is valid only as a define-c-function argument` | `pointer-out` result |
| `conflicting define-c-function declarations of S` | one C symbol declared with two signatures |

A symbol with no definition is reported by the `nplc` linker as an undefined
symbol. Under `npli`:

| Message | Cause |
|---|---|
| `C function not found: S (load its library with npli -l PATH)` | no loaded code defines `S` |
| `C stub not found: npl_cfn_S (build stubs with nplc --emit=c-stubs ...)` | a `define-c-function` whose stub library is not loaded |
| `npli calls C functions of at most 8 arguments: S` | more than 8 parameters |
| `npli: cannot load PATH: ...` (status 66) | `dlopen` failed; the loader's reason follows |

## Limitations

- **npli** calls at most 8 arguments, and the declaration must be evaluated
  after its library is loaded (see "Running Under npli").
- **`define-c-function` types** are the table in "Typed C Functions": no
  structs, arrays, `float`, or unsigned types other than `size_t`, no
  callbacks into NPL, and no variadic C functions (`printf`). Write an
  `NplVal` wrapper with `define-c-primitive` for anything else.
- **Fixed arity.** No optional or rest parameters.
- **Types are unchecked.** A declared type documents intent. A later step
  may use the cheap ones (`pair`, `fixnum`, `char`, `index`) for inline
  checks, keeping the C checks.

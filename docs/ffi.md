# C Functions From NPL

NPL code can call C functions written against the runtime ABI: each takes
and returns `NplVal`. A declaration in a program or library makes such a
function an ordinary NPL procedure, with no compiler change and no seed
relay. Calls compile to a direct C call, as for built-in primitives.

Supported by `nplc` since 2026-10-22 (P2b); `nplc -l`/`-L` since
2026-10-23. Not yet supported: `npli`, C types and pointers, callbacks into
NPL. See
"Limitations" and the plan in [design-primitives](design-primitives.md).

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

Rules, all from `runtime/npl.h`:

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

`npl.h` is in the source checkout's `runtime/` and is not installed yet. On
macOS `-lm` is part of libSystem and may be omitted.

The complete example is `src/compiler/tests/c-extension/` (`main.npl`,
`clamp.c`, `expected.out`). `tools/check_full_compiler.sh` links it by hand
and `tools/check_driver.sh` through `-L`/`-l`.

## Diagnostics

Compile-time errors from `nplc`:

| Message | Cause |
|---|---|
| `define-c-primitive is valid at top level only` | declaration inside a procedure body or expression |
| `unknown C primitive type: T` | type outside the set above |
| `invalid C symbol: S` | symbol is not a C identifier |
| `define-c-primitive: already bound: N` | name already defined or imported in that scope |
| `C primitive declaration conflicts with registered S` | symbol is a built-in or earlier declaration with another operand count or kind |
| `%c-call: argument count differs from its types` | type list and arguments differ in length |

A symbol with no definition at link time is reported by the linker as an
undefined symbol.

## Limitations

- **npli** rejects these forms (`error: C primitives need nplc: npli cannot
  call C functions yet`, status 70). Running C code in the evaluator needs
  run-time symbol lookup (`dlsym`), planned as P5.
- **Values only.** Arguments and results are `NplVal`. Calling an existing
  C library with C types (`int`, `double`, `char *`, pointers) needs a
  small `NplVal` wrapper like the one above. Typed C declarations with
  generated wrappers are planned as P6.
- **Fixed arity.** No optional or rest parameters.
- **Types are unchecked.** A declared type documents intent. A later step
  may use the cheap ones (`pair`, `fixnum`, `char`, `index`) for inline
  checks, keeping the C checks.

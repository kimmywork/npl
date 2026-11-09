# Design: Records, Hash Tables, Pattern Matching, Inline Primitives

Status: user-approved direction 2026-11-07 (records with structural
`equal?` and functional update, SRFI 125 hash tables, `match`, inline
primitives; `syntax-case` deferred). The user waived review of this
document; it records the decisions made while implementing.

## Goal

Give NPL programs, the standard library, and the compiler itself the data
abstractions that the compiler now writes by hand: named record fields
instead of `car`/`cdr` chains, hashed lookup instead of association lists,
and pattern matching instead of nested `cond` over tags. Every facility is
usable without imports, so the compiler (import-free, see "Constraints")
can adopt it, and every hot operation compiles to a few instructions.

## Current State

Checked 2026-11-07.

- `define-record-type` exists only in `(scheme base)` (`lib/scheme/
  record.scm`): a `syntax-rules` macro over three runtime primitives.
  Import-free programs, including the compiler, cannot use it. A field read
  is a global load, an unknown closure call, and a C call
  (`npl_record_ref`): 133 instructions per read in a loop, against 83 for
  `vector-ref` and 85 for `car`+`cdr`. The accessor also makes its caller
  ineligible for a direct entry (`global-call`). Records print as
  `#<(point)>` (the descriptor pair is printed whole) and `equal?`
  compares them by identity; Guile prints `#<point x: 1 y: 2>` and Racket
  transparent structs compare by field.
- No hash tables. The compiler keeps its symbol, function, and global
  tables in association lists (`backend/state.npl lk-s`,
  `frontend/syntax.npl lookup-*`); `frontend/direct-calls.npl` has one
  hand-written bucket table.
- No `match`. The compiler destructures its core AST with 271
  `(car (cdr …))` chains and dispatches on tags with 40 `(= (car x) 2xx)`
  tests.
- `car`, `cdr`, `vector-ref`, `vector-set!`, `vector-length` compile to C
  calls. `pair?`, `null?`, `eq?`, `not` are already inline.

## Constraints

- **The compiler stays import-free.** Its source is assembled with
  `include` and resolves names through the primitive table, so a change to
  `lib/` never changes the seed and never needs a relay, and a library bug
  cannot stop the compiler from rebuilding itself. New facilities the
  compiler uses are therefore core forms or primitive rows, and libraries
  build on the same core, not on copies.
- Permanent bootstrap (stage0, `bootstrap/bootstrap.ssa`,
  `bootstrap/runtime/`) is untouched.
- The collector is conservative and non-moving: an object's address is
  stable, so address-based (`eq?`) hashing needs no rehash after GC.
- npli must give the same results and errors as native code (shared
  fixtures).

## 1. Inline Primitives

The backend emits the type test and the memory access inline; the C
function is called only to raise the error, so error objects and messages
are unchanged.

| Operation | Fast path | Slow path (raises) |
|---|---|---|
| `(car x)`, `(cdr x)` | `x & 3 == 1`, header tag `== BOX_PAIR`, load | `npl_car(x)` |
| `(vector-ref v i)` | `v` is a vector, `i` fixnum, `0 <= i < length` (one unsigned compare), load | `npl_vector_ref(v, i)` |
| `(vector-length v)` | `v` is a vector, load length, tag | `npl_vector_length(v)` |
| `(vector-set! v i x)` | as `vector-ref`, plus not a literal; store, then the write barrier | `npl_vector_set(v, i, x)` |
| record accessor | `x` is a record whose slot 0 `eq?` the type; load slot | `npl_record_ref` |
| record modifier | as the accessor; store, barrier | `npl_record_set` |
| record predicate | the same test, boolean result | none |

The write barrier stays a C call (`npl_gc_write_barrier`): it is a no-op
fast check inside, and inlining it ties compiled code to the generation
bits. The slow-path call is emitted in a separate block that QBE places
after the hot code.

Acceptance: instruction counts for the three loops above (record read
target: at most the inline `vector-ref` path), SSA check that `car` in a
compiled loop is not a call, gates, relay.

## 2. Records

### Surface

R7RS `define-record-type` (5.5), plus:

- `(record-update record-name instance (field value) ...)`: a new instance
  with the named fields replaced; the original is unchanged. Field names
  are the field names of the definition, checked at expansion time. This is
  Racket's `struct-copy` under an R7RS-flavoured name; NPL has no keyword
  arguments, and the type is named explicitly because the instance's type
  is only known at run time.
- `equal?` compares two records structurally: same type and `equal?`
  fields, with the existing cycle handling.
- `write`/`display` print `#<point x: 1 y: 2>` (Guile's format: field
  names, then values), with datum labels for cycles through fields.

Not in this increment: inheritance, opaque/transparent options (all records
are transparent), struct properties, `#:guard`, prefab. A record type is
generative (each evaluation of the definition makes a new type), as in
R7RS.

### Representation

Unchanged: `BOX_RECORD` with the `NplVector` layout, slot 0 the type
descriptor, fields from slot 1. The descriptor becomes a record of a
built-in descriptor type holding the type name and the field-name list,
instead of `(name)`, so printing and `equal?` can name the fields. The
descriptor itself prints as `#<record-type point>`.

### Implementation

`define-record-type` becomes a frontend core form, recognized by name like
`define-values` (`frontend/expand.npl expand-keyword-or-application`,
`frontend/toplevel.npl` and body splicing), so import-free programs have
it. Its expansion is plain definitions:

- `type-name`: `(npl_record_type 'point '(x y))`;
- constructor: a lambda over the constructor's fields calling
  `npl_make_record` with `#f` for fields it does not take;
- predicate and accessors: lambdas whose bodies are the special forms
  `(%record? type x)`, `(%record-ref type x i)`, and
  `(%record-set! type x i v)`, with `i` a literal known at expansion time.

The backend emits those special forms inline (section 1). Because the
accessor is an ordinary small top-level function, C2 makes it direct-safe
and a caller calls its direct entry; a later inlining pass is not needed for
correctness. A small-function inliner for accessors is listed below as a
follow-up if the direct call still shows in the counts.

`(scheme base)` exports the core form; `lib/scheme/record.scm` is deleted
(no second implementation). npli's evaluator gets the expanded definitions
from the shared frontend, so it needs only the three special forms as
evaluator nodes.

`record-update` is a core form too: it expands to a constructor-style
`npl_make_record` call that copies every slot from the instance except the
replaced ones, after a type check.

Acceptance: shared fixture (native, npli; Guile for the R7RS subset):
construction, access, mutation, predicate across types, wrong-type error
object, generativity, `equal?` on equal, unequal, nested and cyclic
records, printing including cycles, `record-update`; the compiler can
define and use a record without imports; read benchmark; gates; relay.

## 3. Hash Tables (SRFI 125)

### Runtime

A new box type `BOX_HASHTABLE` (`runtime/hashtable.c`): open addressing
with linear probing over a keys/values vector pair, load factor at most
0.5, power-of-two capacity. Kinds, chosen at construction:

| Kind | Equivalence | Hash |
|---|---|---|
| `eq` | `eq?` | address (stable: the GC does not move objects) |
| `eqv` | `eqv?` | address, or the number's value for numbers |
| `equal` | `equal?` | structural over pairs, vectors, strings, records, numbers, chars, depth bounded |
| `string` | `string=?` | FNV-1a over characters |
| `custom` | user procedures | user hash procedure |

The four built-in kinds run entirely in C. The custom kind calls the NPL
equality and hash procedures through `npl_call_sync`; its operations are
not direct-safe. Deletion uses tombstones; a rehash drops them. The table
holds its keys and values strongly; weak tables are out of scope.

Mutation goes through the write barrier like vectors. Iteration order is
the slot order and is unspecified, as SRFI 125 allows; fixtures sort before
comparing.

### Primitive rows (import-free)

`npl_make_hash_table kind capacity`, `hash-table?`, `hash-table-ref/default`
(as `npl_hash_table_ref table key default`), `hash-table-set!`,
`hash-table-delete!`, `hash-table-contains?`, `hash-table-size`,
`npl_hash_table_keys`, `npl_hash_table_values`, `hash-table-clear!`,
`hash-table-copy`. The compiler uses these directly.

### Library

`(srfi 125)` (`lib/srfi/125.sld`) implements the full SRFI 125 interface
over the rows: constructors (`make-hash-table` with a comparator-free
equality-procedure argument, `hash-table`, `hash-table-unfold`,
`alist->hash-table`), predicates, accessors (`hash-table-ref` with failure
and success procedures), mutators (`hash-table-update!`,
`hash-table-update!/default`, `hash-table-intern!`, `hash-table-pop!`),
the whole-table procedures (`hash-table-map`, `-for-each`, `-fold`,
`-prune!`, `-count`, `-find`, `->alist`, `hash-table-union!` and the other
set operations), and the deprecated SRFI 69 names that SRFI 125 keeps.
`make-hash-table` maps the standard equality procedures (`eq?`, `eqv?`,
`equal?`, `string=?`) to the built-in kinds and anything else to the custom
kind. SRFI 128 comparators are accepted only as far as SRFI 125 requires
without SRFI 128 itself: a comparator argument is rejected with an error
object until NPL has SRFI 128.

`(srfi 69)` re-exports the subset with SRFI 69 names.

Acceptance: fixture against Guile's SRFI 69 where names overlap, and
against expected output for SRFI 125-only procedures; GC stress with many
young keys in an old table under `NPL_GC_VERIFY`; `equal` hashing of
cyclic keys terminates; benchmark: 1,000,000 inserts and lookups against an
association list and against Guile.

## 4. Pattern Matching

`match` (the Wright/Cartwright/Shinn interface used by Chibi, Chicken, and
Guile's `(ice-9 match)`), written in `syntax-rules` as Alex Shinn's
portable implementation is, and loaded as part of the derived-syntax
source (`frontend/derived.npl`) so import-free programs have it.

Patterns: literals (numbers, strings, characters, booleans), `_`,
variables, `'datum`, `(p ...)` lists with `...` and `..1` at any position,
improper lists `(p . rest)`, vectors `#(p ...)`, `(and p ...)`,
`(or p ...)`, `(not p)`, `(? pred p ...)`, `(= proc p)`,
`(record-name field-pattern ...)` for NPL records (by position), and
quasi-patterns with `unquote`. Forms: `match`, `match-lambda`,
`match-lambda*`, `match-let`, `match-let*`, `match-letrec`,
`match-define`. A failed match raises an error object
`match: no matching pattern` with the value as irritant.

The expansion is ordinary `if`/`let` over `pair?`, `car`, `cdr`,
`vector-ref`, and the record forms, so it benefits from section 1 and needs
no runtime support. Generated code size is watched: the compiler adopting
`match` must not grow the seed by more than the code it removes.

Acceptance: fixture against Guile `(ice-9 match)` for the shared pattern
language, error cases, a record pattern; expansion-time check that a
pattern variable used twice is an error (as in Shinn's `match`, the second
use is an equality test only with `(? ...)`).

## 5. Adopting In the Compiler

After 1-4 land, in this order, each a separate increment with gen2 == gen3,
seed size, and self-compile time recorded:

1. Symbol, function, and global tables (`backend/state.npl`), the
   direct-call table, and the frontend binding environments as hash tables
   where measurement shows lookup cost.
2. Compiler-internal state structures (direct-call entries, SELF records,
   transformers, captures) as records.
3. AST destructuring with `match`. The core AST stays tagged lists unless
   a measured record representation is faster (user decision 2026-11-07:
   keep whichever performs best).

## Order

1. Inline primitives (section 1): independent and benefits every later step.
2. Records (section 2).
3. Hash tables (section 3).
4. `match` (section 4).
5. Compiler adoption (section 5).

## Deferred

`syntax-case` and other procedural macros (they need compile-time
evaluation in the compiler); record inheritance and properties; weak hash
tables; SRFI 128 comparators.

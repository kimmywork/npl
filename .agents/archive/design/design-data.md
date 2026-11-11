# Design: Records, Hash Tables, Pattern Matching, Inline Primitives

Status: user-approved direction 2026-11-07 (records with structural
`equal?` and functional update, SRFI 125 hash tables, `match`, inline
primitives; `syntax-case` deferred). Sections 1-4 are built. The user
waived review of this document; it records the decisions made while
implementing.

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
  are the field names of the definition, checked when it runs. This is
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
descriptor, fields from slot 1. The descriptor becomes a record
`(npl-record-type name (field ...))` (slot 0 the interned symbol
`npl-record-type`), instead of the pair `(name)`, so printing and `equal?`
can name the fields. The descriptor itself prints as
`#<record-type point>`.

### Implementation

As built (2026-11-09). `define-record-type` and `record-update` are
`syntax-rules` macros in the derived-syntax source (`frontend/derived.npl`)
and are exported by `(npl primitive)` like `guard`, so import-free programs,
`(scheme base)`, and the new `(npl record)` library all have the one
definition. syntax-rules cannot count, so two expansion-time core forms in
`frontend/expand-record.npl` supply field positions:

- `(%record-index field (field1 ...))` expands to the slot number of
  `field` (1 for `field1`);
- `(%record-make (head ...) (field1 ...) (argument ...))` expands to
  `(head ... slot ...)` with each field's constructor argument or `#f`, and
  rejects a constructor argument that is not a field.

The expansion is plain definitions:

- `type-name`: `(npl_record_type 'point '(x y))`;
- constructor: `(define (make-point x y) (npl_make_record point x y))`;
- predicate, accessors, modifiers: one-line functions over
  `npl_record_p`, `npl_record_ref`, `npl_record_set` with a literal index.
  Those three are `special` primitive rows emitted inline (section 1); the
  C functions are the failing branch. An accessor is direct-safe, so a
  caller calls its direct entry.

`lib/scheme/record.scm` is deleted. npli gets the same expansion from the
shared frontend and calls the same C functions.

`record-update` checks field names at run time (`npl_record_update`), not
at expansion time as planned: the type name is an ordinary variable, which
may come from another library, so its fields are not known while expanding
the use. An unknown field raises `record-update: unknown field`.

`equal?` (`lib/scheme/list.scm`) compares two instances of the same
define-record-type type field by field, in both the bounded pass and the
union-find pass. Error objects and record types themselves compare by
identity. Field values print in write syntax under `display` too, as in
Guile.

Writing the macros exposed a hygiene bug that the old `record.scm` avoided
by accident: in definition-producing macro output, each occurrence of a
template identifier got its own alias, so a lambda formal and its uses
became different identifiers whenever a global of the same name existed at
the macro's definition site (`(define (name value) value)` returned the
global `value`). `resolve-marked-free` now gives every occurrence of one
name in one transcription the same alias.

Measured: a record field read in a loop costs 85 instructions per
iteration (128 with `record.scm`), against 69 for inline `vector-ref`.
The remaining difference is the direct call to the accessor; inlining small
accessors at their call sites would remove it and is a follow-up.

Fixtures: `tests/r7rs/fixtures/records` (native, npli, Guile 3.0.11) and
`record-extensions` (no oracle: Guile has no `record-update` and prints a
cycle through a record field without its label);
`src/compiler/tests/records.npl` (import-free); two expansion-error
contracts.

## 3. Hash Tables (SRFI 125)

As built (2026-11-09). Reference: [srfi-125.html](../../../docs/reference/srfi-125.html).

### Runtime

`BOX_HASHTABLE` (`runtime/hashtable.c`, `NplHashTable` in `npl.h`): open
addressing with linear probing over one entries vector of
`(hash key value)` triples; `#f` marks an empty slot, `#t` a deleted one
(tombstone). Capacity is a power of two; a resize (to four times the live
count) drops tombstones and happens when used slots would pass half.

| Kind | Equivalence | Hash |
|---|---|---|
| 0 `eq` | `eq?` | address (stable: the GC does not move objects) |
| 1 `eqv` | `eqv?` | address, or the value for numbers (flonum bits, bignum limbs) |
| 2 `equal` | `equal?` | structure over pairs, vectors, records, strings, bytevectors; 64-node budget |
| 3 `string` | `string=?` | FNV-1a over characters (the symbol table's hash) |
| 4 `general` | user procedure | user hash procedure |

An equal table compares keys in C with a walk bounded at 10,000 nodes; a
key too deep or cyclic for it is compared by the library's `equal?` (the
procedure stored in the table), so cyclic keys work. The general kind calls
its procedures through `npl_apply_sync` (new in `trampoline.c`). Mutation
goes through the write barrier on the entries vector. A table copied
without `mutable?` carries `NPL_FLAG_LITERAL`; mutators then raise
`cannot mutate an immutable hash table`.

### Primitive rows (import-free)

`npl_make_hash_table kind equivalence hash capacity`, `npl_hash_table_p`,
`_ref table key missing`, `_set`, `_delete` (returns 0 or 1),
`_contains_p`, `_size`, `_keys`, `_values`, `_items` (a
`#(key value ...)` snapshot for walking), `_clear`, `_copy table mutable?`,
`_pop` (a `(key . value)` pair), `_info table field`, `npl_hash_value kind
value`, and `npl_same_procedure_p`. The compiler can use these directly
with `#f` for the procedure slots of the built-in kinds.

### Library

`(srfi 125)` (`lib/srfi/125.sld`, `125.scm`) implements the full interface,
including the deprecated names. Decisions:

- Without SRFI 128, constructors take an equality predicate and an optional
  hash function; a non-procedure (a comparator) is an error object.
  `hash-table=?` and `hash-table-map` take an equality predicate in the
  comparator position.
- `make-hash-table` recognizes `eq?`, `eqv?`, `equal?`, and `string=?` and
  picks the built-in kind, ignoring a given hash function (allowed for
  refinements of `equal?`); `string-ci=?` gets `string-ci-hash`; any other
  predicate needs a hash function. With no arguments it makes an `equal?`
  table (SRFI 69). A primitive used as a value is a fresh closure at each
  use, so recognition compares the closure's code
  (`npl_same_procedure_p`), not identity.
- `hash-table` returns an immutable table; `hash-table-copy` without a true
  second argument too.
- `hash-table-pop!` on an empty table and `hash-table-ref` of a missing key
  without `failure` raise error objects.
- The walking procedures iterate a snapshot, so a procedure that mutates
  the table cannot break the walk (SRFI 125 makes that an error anyway).

`(srfi 69)` (`lib/srfi/69.sld`) re-exports the SRFI 69 names. Two differ
from their SRFI 125 synonyms, following SRFI 69 and Guile: `hash-table-copy`
returns a mutable table, and `hash-table-merge!` lets the second table's
values win.

`srfi/` joins `scheme/` and `npl/` as an installed library prefix
(`frontend/library.npl`).

Fixtures: `hash-tables` (no oracle: Guile 3.0 has no SRFI 125; checked by
hand against the SRFI text, including cyclic, deep, and record keys),
`srfi-69` (native, npli, and Guile's `(srfi srfi-69)`);
`src/compiler/tests/hash-tables.npl` (import-free rows, also run with a
64 KiB nursery under `NPL_GC_VERIFY`).

Measured (1,000,000 eqv inserts and lookups plus 100,000 string keys):
native 0.33 s CPU; Guile 3.0.11 compiled `(srfi srfi-69)` 1.5 s.

## 4. Pattern Matching

As built (2026-11-10). `match`, `match-lambda`, `match-lambda*`,
`match-let` (also named), `match-let*`, `match-letrec`, and `match-define`:
Alex Shinn's public-domain `syntax-rules` matcher as Guile 3.0 ships it
(`ice-9/match.upstream.scm`), in `frontend/derived-match.npl`
(`MATCH-SYNTAX-SOURCE`, parsed with the derived syntax at expansion
startup). Import-free programs have it; `(npl primitive)` and the new
`(npl match)` export it.

Patterns: `_`, variables, literals (numbers, strings, characters, booleans,
bytevectors), `'datum`, `(p ...)` lists with `...` or `___` followed by a
fixed tail, `(p ..1)`, improper lists, vectors `#(p ...)` (with a trailing
`...`), `(and p ...)`, `(or p ...)`, `(not p)`, `(? pred p ...)`,
`(= proc p)`, quasi-patterns with `unquote`, `(p *** q)` tree search,
`(set! name)`/`(get! name)`, `($ type p ...)` for define-record-type
records by field position, and `(=> next)` failure continuations. Guile
writes record patterns with `$` too; the planned `(type p ...)` form would
be ambiguous with list patterns.

Changes from Shinn's code, all because the compiler must stay import-free:

- No `equal?`, `list?`, `length`, `reverse`, or `error` at run time. A
  literal or quoted atom is compared by the test its kind needs, chosen at
  expansion by the core form `%match-literal` (`frontend/expand-match.npl`):
  `eq?` for symbols, booleans, characters, `()`, and fixnums (inline),
  `eqv?` for other numbers, `string=?` and a byte loop for strings and
  bytevectors. A quoted pair or vector is matched part by part. List length
  (with cycle detection), reversal, and `vector->list` are inline loops;
  `%match-length` counts at expansion (ellipsis tail, record slots).
- A repeated pattern variable is an expansion error (`match: repeated
  pattern variable`). Shinn's and Guile's matcher compare the repeats with
  `equal?`; NPL has no import-free `equal?`, so `(? (lambda (y) (equal? x
  y)))` spells that out.
- The `let-syntax` tricks that classify pattern atoms (identifier?, member
  of the bound identifiers, ellipsis?) are the core forms
  `%match-identifier?`, `%match-member?`, `%match-ellipsis?`, with the same
  semantics (`literal-identifier?`, name `...`). NPL's `let-syntax` body is
  not spliced into the enclosing body (R7RS), so with the tricks
  `match-define`'s definitions stayed local; the core forms also avoid
  parsing a transformer for every pattern variable.
- A failed match raises an error object `match: no matching pattern` with
  the value as irritant. `($ type p ...)` with more patterns than `type` has
  fields raises `match: too many field patterns` (the inline record access
  is not range-checked); fewer patterns match a prefix.

The expansion is `if`/`let` over `pair?`, `car`, `cdr`, `vector-ref`,
`npl_record_p`, and `npl_record_ref`, all inline (section 1).

Frontend defects found on the way, fixed with it:

- Alias environments (`frontend/transcription.npl`) copied the whole
  definition environment onto the use environment at every expression
  transcription, so nested continuation-passing macros made every failed
  lookup scan the program's globals once per nesting level: one `or`
  pattern took 86 s to compile. An alias layer is now one entry that lookup
  descends into for identifiers with its mark (0.1 s; compiler self-compile
  also faster). The copy also reversed the definition environment, so a
  template saw the outermost of two shadowed bindings at its definition
  site; layers keep innermost-first order.
- Derived syntax was expanded without its keywords prebound, so a derived
  macro could only use macros defined before it. `expand-derived-syntax`
  (`toplevel.npl`) now binds them first, for programs, libraries, and npli
  sessions alike.
- A literal identifier compared bindings literally: `=` from `(scheme
  base)` (its validating procedure) did not match the primitive `=`, and in
  npli a free `not` did not match `not` bound by `(npl primitive)`. Both
  denote the same primitive now (`literal-binding-key`, `syntax.npl`).
- `define-values` returned expanded code that the enclosing body or top
  level expanded a second time, so in a program with imports its
  initializer failed (`unbound identifier: _npl_lib_1_list`); it now
  returns source. Macro output that is a `define-values` is spliced like
  other definitions.
- Spliced definition output copied datum atoms as plain pairs, which broke
  every string, character, vector, or boolean literal in a macro that
  expands to definitions.
- syntax-rules matched `()` against a `(p ...)` subpattern only at the top
  level of a list; the lexer did not accept `$ & ^ ~` in identifiers.

Fixtures: `tests/r7rs/fixtures/match` (native, npli, Guile 3.0.11
`(ice-9 match)`), `match-extensions` (no oracle: `match-define`, error
objects), `src/compiler/tests/match.npl` (import-free, 17 tests, also under
`NPL_GC_VERIFY`), the contract `match-repeated-variable`; the frontend
fixes in `definition-macros` and `symbols`.

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

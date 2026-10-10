# L6 Design: Unicode Strings and Datum Labels

Status: draft for review (2026-10-10). Plan item L6 of
[plan-r7rs](plan-r7rs.md). No code is written until this design is accepted.

## Scope

Two independent parts, delivered separately:

- **U: Unicode characters and strings.** Characters cover all Unicode scalar
  values. Strings hold any sequence of them. The `(scheme char)` predicates and
  case mappings follow R7RS 6.6 and 6.7. Textual ports read and write UTF-8.
- **D: datum labels and cycle-safe output.** `read` accepts `#n=` and `#n#`.
  `write` labels cycles, `write-shared` labels all sharing, `write-simple`
  never labels, and `equal?` terminates on circular data (R7RS 6.1, 6.13.3).

Out of scope: grapheme clusters, normalization, locale-sensitive casing
(R7RS excludes it), and Unicode identifiers in compiler source (symbols in
programs may contain them, but the compiler's own source stays ASCII).

## Current State

Checked 2026-10-10 against the code.

- Characters are immediates holding a code point (`npl_char`, 24 bits are
  available). `integer->char` rejects values above 127.
- `NplString` is a byte array (`length`, `capacity`, `char data[]`), and one
  byte is one character. `string-ref` returns `npl_char(byte)`. Symbols share
  this layout.
- The compiler's lexer and helpers read source text through the private
  `string-byte-ref` (`npl_string_ref`), which has 50 uses in `src/compiler`.
- Stage0 and the frozen `bootstrap/bootstrap.ssa` call `npl_string_ref`,
  `npl_string_length`, `npl_substring` and `npl_int_to_string_char` with byte
  semantics. These four entry points must keep their behavior.
- `(scheme char)` is ASCII-only. `string->utf8` is the identity on ASCII.
- The compiler reader already supports datum labels in source
  (`reader/datum.npl`). The runtime `read` (`io.c`) rejects `#`-labels.
  `write` and `write-shared` are the same function and loop on cycles.
  `equal?` (library) does not terminate on cycles.

Guile 3.0 behavior used as the reference (measured):

- Full Unicode: `(string-length "héllo λ 😀")` is 9, and `string-upcase` of
  `"straße"` is `"STRASSE"`. `char-numeric?` and `digit-value` work on
  non-ASCII decimal digits. `utf8->string` raises on invalid input.
- No datum labels in `read`: `#1=` is read as array syntax.
- `write-shared` labels sharing and cycles, numbering from 1:
  `#1=(1 2 . #1#)` and `(#1=(1) #2=#(2) #1# #2#)`.
- Plain `write` and `display` do not label cycles correctly. They print
  `(1 2 . #-1#)` for a cyclic list and `#(#0# 2)` for a self-containing
  vector, which is not valid datum syntax.
- Racket reads datum labels and writes cycles as `#0=(1 2 . #0#)`, numbering
  from 0.
- For D, the oracle is therefore Guile for `write-shared` only. For `read`
  and for cyclic `write`/`display`, the references are R7RS 2.4 and 6.13.3
  plus native/npli agreement, with Racket as a cross-check.
- Invalid UTF-8 on textual input reads as U+FFFD in both Guile and Racket.

## Part U: Representation

### Option U1: UTF-8 bytes (recommended)

`NplString` stays a byte array, but its contents are UTF-8. Two header fields
are added:

- the character count, so `string-length` is O(1);
- an ASCII flag (`NPL_FLAG_ASCII`), meaning byte index equals character index.

Indexing works as follows:

- When the ASCII flag is set, `string-ref` and `string-set!` are O(1), exactly
  as today. Almost all strings, including all compiler source, are ASCII.
- Otherwise `string-ref` is O(n), unless it hits a one-entry cache in the
  string (the last character index and its byte offset). The cache makes
  sequential loops (`string-for-each`, `string->list`, index loops) amortized
  O(1).
- `string-set!` with a character of a different encoded width rebuilds the
  byte array. Strings already have `capacity`, so this needs no new indirection.

Pros: the byte layout, the static string data the compiler emits, symbol
boxes, port I/O, `string->utf8`, and C interop (`fopen`, `getenv`) are
unchanged or become trivial. The compiler, stage0 and bootstrap keep working
on byte entry points.

Cons: non-ASCII random access is not O(1) (R7RS does not require it).
Mutation can change byte length.

### Option U2: UTF-32 code units

Each character is 4 bytes, so indexing is O(1) everywhere.

Cons:
- 4x memory for every string, including the compiler's.
- Every C boundary (paths, environment, ports, symbol interning, static data)
  needs a transcoding step.
- The frozen bootstrap's `npl_string_ref` would need a byte view of a UTF-32
  string.

### Option U3: adaptive width (Latin-1 / UCS-2 / UTF-32, like Python)

O(1) indexing with compact ASCII. It has the transcoding costs of U2 plus
widening on mutation. It is the largest change of the three.

### Decision requested

U1. The rest of this section assumes it.

## Part U: Changes

Runtime (C, mechanisms only):

- `NplString` gains `uint32_t chars` and uses the `NPL_FLAG_ASCII` flag. Every
  constructor sets them: `npl_string`, `npl_box_bytes_internal`, memory ports
  and `get-output-string`, `read-file`, the compiler-emitted static strings and
  symbols. The static data layout gains one word, which changes the emitted
  SSA, so a seed transition is needed (see Delivery).
- New character-level primitives replace the current byte-based
  implementations behind the R7RS names. All of them validate scalar values.
  - `npl_string_char_ref` (already the `string-ref` entry) decodes UTF-8.
  - `string-set!`, `string-fill!` and `make-string` encode UTF-8.
  - `substring` and `string-copy` use character indices.
  - `string-length` returns the character count. The `string-length` compiler
    special form switches from `npl_string_length` to a new
    `npl_string_char_length`, and `npl_string_length` keeps its byte meaning.
- `integer->char` accepts 0..#x10FFFF excluding #xD800..#xDFFF.
- Textual ports decode and encode UTF-8 in `read-char`, `peek-char`,
  `write-char`, `display` and `write`. `char-ready?` stays byte-based, as R7RS
  permits. Invalid UTF-8 input reads as U+FFFD; R7RS leaves this unspecified,
  and Guile and Racket both substitute (measured).
- `write` escapes characters per R7RS 6.13.3. Character names follow R7RS
  (`#\alarm` and the rest), and other non-graphic characters use `#\xHH`.
- Symbols with non-ASCII characters are written with vertical bars, as R7RS
  requires.

Unicode data (generated, not hand-written):

- A generator script, `tools/gen_unicode.py`, reads `UnicodeData.txt`,
  `CaseFolding.txt`, `SpecialCasing.txt` and `PropList.txt` from a pinned
  Unicode version, using the version Python's `unicodedata` reports when the
  data files are absent. It emits compact range tables as an NPL source file,
  `lib/scheme/unicode-tables.scm`. Lookups are binary searches over sorted
  vectors.
- Keeping the tables in NPL keeps `(scheme char)` policy NPL-first. If the
  tables are too slow, a C lookup primitive over the same generated data is
  the fallback; that needs a measurement first.

Library (NPL):

- `(scheme char)` predicates use the tables: Alphabetic, Numeric_Type=Decimal,
  White_Space, Uppercase, Lowercase.
- `digit-value` uses Numeric_Type=Decimal.
- `char-upcase`, `char-downcase` and `char-foldcase` use simple mappings.
- `string-upcase`, `string-downcase` and `string-foldcase` use full mappings
  (SpecialCasing; `ß` becomes `SS`), plus the final-sigma rule for downcase,
  which Guile applies: `"ΣΑΣ"` becomes `"σας"`.
- `string->utf8` and `utf8->string` copy bytes with ranges in character
  indices. `utf8->string` raises an error object on invalid input, matching
  Guile.

Compiler:

- No change to its own text handling. Lexing stays byte-based through
  `string-byte-ref`. The lexer treats bytes >= 128 as symbol constituents, so
  string and symbol literals may contain UTF-8.
- The new string header word changes static string and symbol data emission
  (`backend/data.npl`).

Evaluator: it uses the host string primitives, so no separate semantics are
needed. Its checks that require `eval-ascii-char?` widen to scalar values.

## Part D: Datum Labels

- Runtime `read` (`io.c`): `#n=datum` and `#n#` using a per-read label table.
  Placeholders are patched after the labelled datum completes, which is the
  same algorithm as the compiler reader's `fill-datum-placeholder`. The C
  reader cannot share the NPL code; this duplication is justified by the
  C-runtime boundary and will be noted in the change. An undefined label or a
  bare self-reference (`#0=#0#`) raises a read error.
- Output: one cycle-detection pass shared by the three writers.
  - `write-shared` labels every pair or vector reached twice.
  - `write` labels only objects that lie on a cycle, using DFS with an
    on-stack mark.
  - `write-simple` skips the pass.
  - `display` uses `write`'s labelling so it always terminates; R7RS requires
    termination.

  Labels are numbered in output order starting at 1, matching Guile's
  `write-shared`, so those fixtures compare directly with Guile. The pass uses
  a temporary hash table keyed by object address and is GC-safe because it
  does not allocate managed objects.
- `equal?` on cycles: R7RS requires termination. Implement it in NPL as a
  bounded-recursion fast path, then fall back to a union-find over visited
  pairs and vectors (the Adams–Dybvig "efficient nondestructive equality"
  approach). Acyclic inputs keep their current cost.
- Separate primitives: `write-shared`/`write-simple` get their own runtime
  entries instead of the current `npl_write_port` alias.

## Delivery

Each item is one commit, with a fixture identical across native, npli and
Guile unless noted.

| Inc | Content | Relay | Oracle |
|---|---|---|---|
| L6a | String header (`chars`, ASCII flag) and static data emission; behavior still ASCII | seed transition (two relays) | existing fixtures unchanged |
| L6b | UTF-8 string/char primitives, `integer->char` range, UTF-8 ports, `write` escapes | relay | Guile |
| L6c | Unicode tables and `(scheme char)` | library only | Guile (Unicode 15.1 tables vs Guile's; differences classified) |
| L6d | Runtime `read` datum labels | relay (runtime) | Racket for input; native/npli agreement |
| L6e | `write`/`write-shared`/`write-simple`/`display` labelling, cycle-safe `equal?` | relay (runtime) | Guile for `write-shared`; R7RS and native/npli agreement for cyclic `write`/`display`/`equal?` |

Verification on top of fixtures:

- string-heavy self-compilation time before and after L6a/L6b (ASCII fast
  path must keep the relay within noise);
- GC stress (`NPL_GC_THRESHOLD_BYTES=1048576`) over non-ASCII mutation;
- a UTF-8 round-trip property test (random scalar strings through ports,
  `string->utf8`, and `read`/`write`).

## Open Questions

1. Representation: accept U1 (UTF-8 with ASCII fast path and an index cache)?
2. Invalid UTF-8 on textual input: substitute U+FFFD (Guile and Racket) or
   raise a read error?
3. `char-ready?` on a stream port with a partial UTF-8 sequence buffered:
   return `#t` (byte-based, simple) or check for a complete character?
4. Unicode version: pin 15.1 (matches the host's Python data) or the newest
   release?

# L6 Design: Unicode Strings and Datum Labels

Status: accepted 2026-10-10 (decisions below). Plan item L6 of
[plan-r7rs](plan-r7rs.md).

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

Measured 2026-10-10. In one self-compilation (current seed, 256 MiB GC
threshold), the compiler allocated 15.1 GB in 409M objects. Strings were
558K of those objects, 314 MB (2.1% of bytes), and none contained non-ASCII.
The run made 794K `string-byte-ref` calls, 632K `string-length` calls, and no
character `string-ref` calls. Strings are a small part of the heap, and no
string path is hot.

### Chosen: adaptive width (narrow 8-bit, wide 32-bit), O(1) indexing

- A string holds characters, not encoded bytes. The header gains a width
  flag, `NPL_FLAG_WIDE`.
  - A narrow string stores one byte per character, for code points
    0..255 (Latin-1, which includes all ASCII).
  - A wide string stores one 32-bit code point per character.
- `string-ref`, `string-set!`, `string-length`, and `substring` are O(1) at
  either width. `length` always counts characters.
- Storing a character above 255 into a narrow string promotes it to wide.
  Promotion must keep `eq?` identity, but the data lives inline in the
  object, whose size is fixed at allocation. So promotion moves the data to
  a separately allocated wide buffer and marks the string
  `NPL_FLAG_OUT_OF_LINE`, with the buffer pointer in the first data word.
  Promotion happens at most once per string, and only for mutable strings.
  Literals are immutable and never promoted.
- Strings created wide (from a wide source or a scalar above 255) are inline
  and wide.
- Symbols use the same layout and are immutable.
- C boundaries (paths, environment, `fopen`, `getenv`, process arguments)
  encode to UTF-8 at the call. Ports encode and decode UTF-8.
  `string->utf8` and `utf8->string` transcode. A narrow string whose bytes
  are all ASCII is passed to C directly.
- Compiler-emitted static strings and symbols are narrow, with flag 0 or
  `NPL_FLAG_LITERAL`. Their emitted layout is unchanged, and the compiler's
  own source is ASCII.

Fallback, if adaptive width proves too complex: Chez Scheme's layout, where
every runtime string is 32-bit, with no promotion and no out-of-line case.
Measured cost: about 0.9 GB more allocation per self-compilation (+6%).

### Frozen bootstrap and stage0

`bootstrap/bootstrap.ssa` and stage0 stay frozen, together with a runtime
pinned to one commit. `tools/check_bootstrap.sh` builds that pinned runtime
from `git archive` of the commit before L6a, and links the frozen artifacts
against it. The current runtime is then free to change layout.
`bootstrap/current.ssa` needs no pin: each relay emits it in the current
layout, and runtime entry points that only old seeds used are removed once
the new seed is installed.
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
  `write-char`, `display` and `write`. Invalid UTF-8 input reads as U+FFFD;
  R7RS leaves this unspecified, and Guile and Racket both substitute
  (measured). `char-ready?` is character-based (Decision 3).
- `write` escapes characters per R7RS 6.13.3. Character names follow R7RS
  (`#\alarm` and the rest), except U+0000, which is written `#\nul` as in
  Guile and Racket (`read` accepts `#\null` and `#\nul`). Other non-graphic
  characters use `#\xHH`.
- `write` follows Guile and Racket (Decision 2):
  - Graphic non-ASCII characters are written as is, in strings, in
    characters (`#\λ`), and in symbols (`λx`).
  - Non-graphic characters use hex escapes: `\x200b;` in strings and
    `#\x200b` for characters.
  - Symbols that would not read back (spaces, delimiters, empty) are written
    with vertical bars, `|a b|`. Guile writes `#{a b}#` and Racket `|a b|`;
    R7RS 2.1 defines `|...|`.

  R7RS 6.13.3 asks for vertical bars around any symbol containing non-ASCII
  characters. Guile and Racket do not do this, and NPL follows them; this is a
  recorded deviation.

Unicode data (generated, not hand-written):

- `tools/gen_unicode.py` writes `runtime/unicode_tables.h` at the host
  Python's `unicodedata` version (Decision 4). Properties unicodedata does
  not expose (Alphabetic, Uppercase, Lowercase, White_Space, Cased,
  Case_Ignorable) and the full case mappings come from the UCD files of the
  same version (`--ucd DIR` or downloaded; the version is checked).
- The tables are C arrays behind four lookup primitives
  (`runtime/unicode.c`): binary search over sorted ranges and maps. The
  `(scheme char)` policy (which mapping each procedure uses, Final_Sigma)
  stays in NPL (`lib/scheme/char.sld`). Table data in NPL would be about
  6,000 elements of static data per program, so C is the cheaper place for
  data; behavior remains in NPL.
- Guile and Racket differ on a few cases (Racket uses full folding for
  `char-foldcase` of capital sharp s and the Other_* properties of
  letterlike numbers and symbols; Guile does not), so the shared fixture
  leaves them out. NPL follows the Unicode properties R7RS names.

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
| L6a+b | Adaptive-width strings (`runtime/string.c`), static wide string data, scalar-value characters, UTF-8 ports with byte pushback, character-based `char-ready?` and byte `u8-ready?`, `string->utf8`/`utf8->string`, `read` and `write` syntax (Guile's non-graphic set, generated from `unicodedata`) | relays | Guile (done 2026-10-11, fixture `unicode-strings`) |
| L6c | Unicode tables and `(scheme char)` | relay (table-lookup primitives) | Guile (done 2026-10-11, fixture `unicode-char`) |
| L6d | Runtime `read` datum labels | relay (runtime) | Racket for input; native/npli agreement |
| L6e | `write`/`write-shared`/`write-simple`/`display` labelling, cycle-safe `equal?` | relay (runtime) | Guile for `write-shared`; R7RS and native/npli agreement for cyclic `write`/`display`/`equal?` |

Verification on top of fixtures:

- string-heavy self-compilation time before and after L6a/L6b (ASCII fast
  path must keep the relay within noise);
- GC stress (`NPL_GC_THRESHOLD_BYTES=1048576`) over non-ASCII mutation;
- a UTF-8 round-trip property test (random scalar strings through ports,
  `string->utf8`, and `read`/`write`).

## Decisions (2026-10-10)

1. Representation: adaptive width (narrow 8-bit or wide 32-bit, O(1)
   indexing, promotion to an out-of-line wide buffer). Revised the same day
   after review: the first draft's UTF-8 option (U1) was dropped in favor of
   O(1) indexing. The fallback is Chez's all-32-bit layout.
2. Invalid UTF-8 on textual input reads as U+FFFD, as Guile and Racket do.
3. `char-ready?` is character-based: it returns `#t` only when a complete
   character (or end of file) can be read without blocking. A partial UTF-8
   sequence is not ready. The byte-level query is `u8-ready?` (R7RS 6.13.2).
   Stream ports therefore need a small per-port lookahead buffer, so that
   bytes `char-ready?` reads without blocking are not lost; the current
   `poll`-only check also misses data already in the stdio buffer.
4. Unicode version: the host Python's `unicodedata` (15.1.0), recorded in the
   generated tables.

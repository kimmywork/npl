# Reader Directives and Reader Extensions

Status: **design proposal**. The generalized registry, parameterized
directives, and `#!lang`-style profiles are not implemented yet.

## Why this document

NPL has accumulated several `#!` markers:

- R7RS `#!fold-case` / `#!no-fold-case`
- SRFI 105 `#!curly-infix`
- SRFI 88 `#!srfi-88` / `#!no-srfi-88`

They started as one-off lexer switches. That is enough for the primer, but
it is not a good long-term reader-extension design. In particular:

- `#!curly-infix` currently does not actually switch anything: `{...}` is
  always accepted.
- `#!srfi-88` is named after a standard but is really a source-level
  feature flag, and the trailing-colon spelling breaks R7RS identifiers
  such as the common import prefix `b:`.
- The compiler reader and the runtime reader have separate directive
  implementations and have already diverged in error handling.
- A global lexer switch is not the right state model for a reader that can
  read multiple files or ports in one process.

The goal is a small, explicit reader-options system: directives are
composable, per port/source, validated, and can grow into a Racket-style
`#lang`-like extension point without changing the `#!` surface each time.

## Current behavior (interim)

The current implementation, before the generalized registry:

- recognizes `#!fold-case`, `#!no-fold-case`, `#!curly-infix`,
  `#!srfi-88`, and `#!no-srfi-88`;
- accepts consecutive directives separated by whitespace, for example:

  ```scheme
  #!curly-infix #!srfi-88
  ```

- stores runtime reader options on the input port, so a directive persists
  across the forms read from that port;
- resets compiler reader options at the start of each source file.

The remaining rough edges are intentional inputs to this design:

- unknown directives: the runtime reader reports an error, while the
  compiler reader currently ignores them;
- `#!curly-infix` is currently a no-op for the core `{...}` syntax;
- `#!srfi-88` is a provisional name;
- there is no plugin/`#lang` registry;
- default profiles are implicit.

## Core rule: presence enables, absence does not

For NPL reader features, a directive is a **one-way activation**, not a
toggle:

- `#!colon-keyword` means “this source/port reads trailing-colon
  keywords.”
- Without it, trailing-colon tokens stay ordinary symbols.
- `#!curly-infix` means “this source/port reads curly-infix lists.”
- Without it, the default reader behavior applies.
- There is no `#!no-colon-keyword` or `#!no-curly-infix` in this design.

This keeps the mental model simple: a file lists the reader features it
uses. Separate files and separate ports start from their profile defaults.

**Exception:** R7RS itself specifies the pair `#!fold-case` and
`#!no-fold-case`. Those remain a standardized toggle pair because dropping
`#!no-fold-case` would break R7RS source.

## Shebang and `#!` do not conflict

A shebang is a Unix convention, not a Scheme datum. The two forms are
distinguished by the **first character after `#!`**:

- `#!` followed by `/` or whitespace at the very start of a file is a
  shebang. `npli` removes that first line before reading; `nplc` rejects it
  with a line-1 diagnostic because compiled source files are not scripts.
- `#!` followed by a letter, `(`, or `[` is a reader directive. It may
  appear at the start of a file, because it is not a shebang.

Therefore this is valid for `npli`:

```scheme
#!/usr/bin/env npli
#!colon-keyword
(import (scheme base))
```

and this is not a shebang:

```scheme
#!curly-infix
```

`nplc` continues to reject shebang lines, but accepts the same directive
line.

Directives are recognized only at datum boundaries (where the reader would
skip whitespace). They are not recognized inside strings, character
literals, bar-quoted symbols, or block comments.

A shebang consumes the rest of its line, so a directive placed after a
shebang on the **same line** is lost. Put directives on the following
line(s).

## Proposed directive syntax

### Simple form

```text
#!<name>
```

where `<name>` is one or more ASCII letters, digits, `-`, or `_`, followed
by whitespace or a delimiter. The directive consumes no datum.

Examples:

```scheme
#!curly-infix
#!colon-keyword
```

### Parameterized form

A directive that needs arguments uses one parenthesized list:

```text
#!(<name> <argument> ...)
```

Examples:

```scheme
#!(lang npl)
#!(lang r7rs)
#!(curly-infix neoteric)
```

Rules:

- The first element is a directive name.
- Arguments are reader datums, not evaluated expressions.
- Arity and argument types are supplied by the directive registry.
- The parenthesized form is allowed for zero-argument directives too, so
  `#!(colon-keyword)` is equivalent to `#!colon-keyword`.
- No whitespace is needed between `#!` and `(`; `#! (lang npl)` should be
  rejected because `#! ` at the start of a line is shebang syntax.

The parenthesized form is the recommended parameterized syntax. It is
already a list datum, so it composes naturally with the reader and needs no
new delimiter.

### Why not `#![directive1, directive2, ...]`?

The group form is unnecessary:

- Consecutive directives already compose:

  ```scheme
  #!colon-keyword
  #!curly-infix
  #!(lang npl)
  ```

- Comma is not a separator in Scheme; it is unquote syntax. Writing
  `#![colon-keyword, curly-infix]` would be misleading and would require a
  separate comma-aware parser inside the bracket group.
- A whitespace-separated `#![colon-keyword curly-infix]` is possible, but it
  only saves a few `#!` prefixes while adding a second grammar and a second
  place for validation errors.

This design therefore **does not propose** the bracket group form. If it is
ever added, it should use whitespace-separated names, not commas, and should
expand to the same ordered directive sequence as consecutive directives.

## Proposed feature names

| Feature | Form | Semantics |
|---|---|---|
| case folding | `#!fold-case`, `#!no-fold-case` | R7RS standardized toggle pair |
| curly infix | `#!curly-infix` | enable SRFI 105 core `{...}`; absence uses the profile default |
| curly neoteric | `#!(curly-infix neoteric)` | enable `f(x)`, `e{}`, `e[...]` where supported |
| colon keywords | `#!colon-keyword` | enable trailing-colon keyword objects |
| language profile | `#!(lang <name>)` | select the reader/language profile; once per source/port |

`#!srfi-88` remains a compatibility alias for `#!colon-keyword`. The
canonical descriptive name is singular `#!colon-keyword` (not
`post-column-keyword`; “post-colon keyword” is clearer as
`colon-keyword`).

The trailing-colon feature is **off by default**. This preserves R7RS
identifiers such as `b:` and `foo:`. `#:foo` is always available as the
Guile-compatible keyword spelling, independent of this directive.

## Directive semantics

- **Presence enables.** Repeating an activation is idempotent.
- **No NPL feature toggle.** There is no `#!no-colon-keyword` or
  `#!no-curly-infix`. A fresh file/port starts from its profile default.
- **Order independent for independent features.** `#!colon-keyword` before
  or after `#!curly-infix` gives the same result.
- **Validated.** Unknown directives, wrong arity, and malformed arguments
  are errors in both readers, with line and column information.
- **Per port/source.** Runtime options belong to the input port; compiler
  options belong to the source being parsed. They do not leak between files
  or independent ports.
- **No evaluation.** Directives change how data is read; they do not run
  user code and do not grant capabilities.
- **Shebang excluded.** `#!/...` and `#! ...` at line 1 are shebangs, not
  directives.

## `#!lang` / reader profiles

A parameterized language directive selects a profile:

```scheme
#!(lang npl)
```

or:

```scheme
#!(lang r7rs)
```

Semantics:

- `lang` may appear only once per source/port, and must be the first
  non-shebang directive.
- A profile supplies default feature flags and may add a language-level
  prelude or a datum post-read hook.
- Feature directives after `#!(lang ...)` may only **add** reader features
  allowed by that profile; they cannot replace the language.
- Two `lang` directives in one file are an error, not “last one wins”.
  `#!(lang npl)` and `#!(lang r7rs)` describe different readers and cannot
  both apply to one datum stream.
- Unknown profiles are errors.
- Loading a profile is subject to the same trust boundary as importing a
  library; it must not grant ambient filesystem, network, or FFI authority
  by itself.
- The compiler and evaluator must use the same profile registry, so the
  same source reads identically under `nplc` and `npli`.

Possible profiles:

| Profile | Defaults |
|---|---|
| `r7rs` | no colon keywords; no NPL curly-infix; R7RS directives |
| `npl` | current NPL defaults (core `{}` policy decided separately) |
| `guile` | `#:` always; optional colon keywords; useful for Guile source |

A smaller first step is a profile directive without code loading:

```scheme
#!(profile r7rs)
```

with a fixed table of flags. `#!lang` can then select a profile plus a
prelude. This avoids executing arbitrary code at read time and is easier to
audit.

## Curly-infix scope

The current `{...}` core reader is always on. The generalized directive
should eventually define:

- `#!curly-infix`: enable `{...}`, nested curly lists, and the mapping rules;
- `#!(curly-infix neoteric)`: additionally enable `f(x)`, `e{}`, `e[...]`;
- absence: use the language profile default.

Neoteric forms depend on whitespace/adjacency and `e[...]` conflicts with
NPL's bracket lists, so they should not be forced on by the basic flag. A
separate argument or a separate profile is safer.

## Architecture

### Runtime reader

- `NplPort` owns the reader-options record: case folding, colon keywords,
  curly-infix flags, and (later) profile id.
- `npl_read_datum` initializes its `NplDatumReader` from the port.
- `read_directive` updates the port, so the option persists across `read`
  calls.
- A fresh port starts from its default profile.

### Compiler reader

- Replace the global directive cells with a `READER-OPTIONS` record passed
  through the lexer/datum parser (or stored per `parse-source-text`
  invocation).
- Reset options at the start of each source file.
- Use the same directive-name table and error wording as the runtime
  reader.
- The compiler reader is the source of truth for `nplc`; the runtime reader
  is the source of truth for `read`, `npli`, and `load`. Differential tests
  must compare both.

### Shared registry

The directive table should be data, not duplicated `cond` chains:

```text
(name, arity, handler)
```

The handler may:

- set a one-way feature flag;
- validate arguments;
- install a datum post-read hook;
- select a language profile (only for `lang`, first non-shebang directive).

The table can live in NPL source and be mirrored by a small C table for the
runtime reader, with a consistency test that both readers accept exactly
the same names and arities.

### Diagnostics

Every directive error should include:

- the directive text;
- line and column;
- whether it was unknown, malformed, has wrong arity, is disallowed by the
  profile, or appears in an invalid position.

## State and scoping

- Runtime: per input port, persistent across calls to `read`.
- Compiler: per source file, reset before lexing.
- `load`/`include`: each included source gets its own fresh options;
  directives do not leak into the includer.
- REPL: the standard input port keeps its options until replaced. There is
  no in-place disable; start a fresh port for a fresh profile.
- Multiple files and independent ports do not share feature state.
- `#!(lang ...)` is fixed for the lifetime of that source/port.

## Interaction matrix

| Combination | Expected result |
|---|---|
| `#!fold-case` then `#!colon-keyword` | symbols fold; `foo:` is a keyword |
| `#!colon-keyword` then `#!fold-case` | same; order independent |
| `#!colon-keyword` repeated | still enabled; idempotent |
| `#!curly-infix` then `#!colon-keyword` | `{}` and `foo:` both active |
| `#!curly-infix` repeated | still enabled; idempotent |
| `#!(lang npl)` then `#!colon-keyword` | profile defaults plus colon keywords (if allowed) |
| `#!(lang npl)` then `#!(lang r7rs)` | error: one language per source |
| `#!colon-keyword` then `#!(lang npl)` | error: `lang` must be first |
| shebang then directives | `npli` strips shebang; directives apply to the rest |
| `nplc` shebang | rejected with line-1 shebang diagnostic |
| unknown `#!foo` | error in both readers |
| `#!(lang unknown)` | error in both readers |

## Migration plan

1. Add `#!colon-keyword` as the canonical spelling and keep `#!srfi-88` as
   an alias.
2. Remove `#!no-srfi-88` from the NPL feature model; R7RS
   `#!no-fold-case` stays.
3. Move compiler options from global cells to a per-source record.
4. Keep runtime options on the port (already true in the interim fix).
5. Add arity/argument validation and identical diagnostics in both readers.
6. Add `#!(lang ...)`/`#!(profile ...)` only after the flag registry is
   stable and tested.
7. Decide the default curly-infix profile and whether neoteric is an
   argument or a separate profile.

## Tests

- Consecutive directives, same line and separate lines, both orders.
- Parameterized `#!(lang npl)` and wrong arity/unknown language.
- Multiple `#!(lang ...)` in one source is an error.
- Shebang plus directives under `npli`; shebang rejection under `nplc`.
- Runtime directives persist across forms read from one port; a new port
  starts clean.
- Compiler directives reset between files and between includes.
- Unknown directives fail in both readers with line/column.
- `fold-case` + keyword interactions.
- `#!colon-keyword` enables `foo:`; absence keeps it a symbol and keeps
  `b:` usable as an import prefix.
- `#!curly-infix` with and without `{}` syntax, and neoteric argument when
  implemented.
- Differential tests: the same source read by `nplc` and `npli` produces
  the same datum stream and observable output.

## Open questions

- Should basic `{...}` be on by default, or only under `#!curly-infix`?
- Should neoteric forms be an argument (`#!(curly-infix neoteric)`) or a
  separate directive/profile?
- Is `#!(profile r7rs)` useful enough to add before full `#!lang`?
- How should a `#!lang` profile interact with library imports and
  `cond-expand` feature identifiers?
- Should `#!srfi-88` be deprecated in documentation immediately or kept as
  a compatibility spelling indefinitely?

## Interim state after the first implementation

The current source implements the feature flags as one-off lexer/runtime
switches, with per-port persistence for the runtime and per-file reset for
the compiler. It still accepts the provisional `#!srfi-88` / `#!no-srfi-88`
pair. It deliberately does **not** implement the registry,
`#!colon-keyword` alias, parameterized directives, unknown-directive
parity, or `#!lang`. This document is the contract for the follow-up
refactor.

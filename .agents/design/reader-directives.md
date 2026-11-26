# Reader Directives and Reader Extensions

Status: **design proposal**, except where this document describes the
current behavior. The generalized registry and `#!lang`-style extension
point are not implemented yet.

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

## Shebang and `#!` do not have to conflict

A shebang is a Unix convention, not a Scheme datum. The two forms are
distinguished by the **first character after `#!`**:

- `#!` followed by `/` or whitespace at the very start of a file is a
  shebang. `npli` removes that first line before reading; `nplc` rejects it
  with a line-1 diagnostic because compiled source files are not scripts.
- `#!` followed by a letter is a reader directive. It may appear at the
  start of a file, because it is not a shebang.

Therefore this is valid for `npli`:

```scheme
#!/usr/bin/env npli
#!colon-keywords
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

## Proposed directive syntax

A directive is:

```text
#!<name>
```

where `<name>` is one or more ASCII letters, digits, `-`, or `_`, followed
by whitespace or a delimiter. The directive consumes no datum.

Properties:

- **Composable.** Any number of directives may appear, separated by
  whitespace or newlines. Order is independent except where a toggle is
  explicitly last-wins.
- **Idempotent.** Repeating an enable directive is harmless.
- **Paired toggles.** Every feature has an enable and a `no-` form, for
  example `#!colon-keywords` and `#!no-colon-keywords`.
- **Validated.** An unknown directive is an error in both readers, with
  line and column information.
- **Per port/source.** Runtime options belong to the input port; compiler
  options belong to the source being parsed. They do not leak between
  files or independent ports.
- **No evaluation.** Directives change how data is read; they do not run
  user code and do not grant capabilities.

## Proposed feature names

| Feature | Enable | Disable | Notes |
|---|---|---|---|
| case folding | `#!fold-case` | `#!no-fold-case` | R7RS |
| curly infix | `#!curly-infix` | `#!no-curly-infix` | SRFI 105; see scope below |
| colon keywords | `#!colon-keywords` | `#!no-colon-keywords` | canonical name for trailing-colon keyword objects |

`#!srfi-88` remains an alias for `#!colon-keywords` for source
compatibility. The canonical descriptive name is `#!colon-keywords`
(the user-facing spelling; the hyphenated phrase is clearer than
"post-column-keyword"). A future release can deprecate the SRFI-numbered
spelling in documentation while keeping the alias.

The trailing-colon feature is **off by default**. This preserves R7RS
identifiers such as `b:` and `foo:`. `#:foo` is always available as the
Guile-compatible keyword spelling, independent of this directive.

## Curly-infix scope

The current `{...}` core reader is always on. The generalized directive
should eventually define:

- `#!no-curly-infix`: `{`/`}` are reserved-but-rejected (or left to a
  language profile);
- `#!curly-infix`: enable `{...}`, nested curly lists, and the mapping
  rules;
- neoteric forms (`f(x)`, `e{}`, `e[...]`) should be enabled by a separate
  feature or by a language profile, because they depend on
  whitespace/adjacency and `e[...]` conflicts with NPL's bracket lists.

Possible names:

```text
#!curly-infix          basic { ... } and $nfx$ mapping
#!curly-neoteric       f(x), e{}, e[...] and bracket-apply
```

or a single `#!curly-infix` that enables everything, with
`#!no-curly-neoteric` as an override. This document recommends two flags so
that existing code that uses `[...]` as list brackets is not forced to
opt into a changing token grammar.

## Future: `#!lang`-style reader/language plugins

After the feature registry is stable, the same mechanism can support a
Racket-like entry:

```scheme
#!lang npl/typed
```

or:

```scheme
#!lang npl/infix
```

Semantics:

- `#!lang` must be the first non-shebang directive and may appear only
  once.
- The name selects a **reader profile** and, optionally, a library prelude
  or expansion profile. It is not a general “run this file” permission.
- A profile supplies:
  - default feature flags (for example, colon keywords off);
  - a reader directive table (which directives are legal);
  - optionally a datum post-read hook or a language-level prelude.
- The compiler and evaluator must use the same profile registry, so the
  same source reads identically under `nplc` and `npli`.
- Unknown profiles are errors. Loading a profile is subject to the same
  trust boundary as importing a library; it must not grant ambient
  filesystem, network, or FFI authority by itself.

A smaller first step is a **reader profile** without a `#!lang` name:
`#!profile r7rs` / `#!profile npl` with a fixed table of flags. That does
not require loading code at read time and is easier to audit.

## Architecture

### Runtime reader

- `NplPort` owns the reader-options record: case folding, colon keywords,
  curly-infix flags, and (later) profile id.
- `npl_read_datum` initializes its `NplDatumReader` from the port.
- `read_directive` updates both the active reader and the port, so the
  option persists across `read` calls.
- `read` on a fresh port starts from the default profile.

### Compiler reader

- Replace the global `LEX-SRFI-88` cell with a `READER-OPTIONS` record
  passed through the lexer/datum parser (or stored per `parse-source-text`
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
(name, enable?, handler)
```

The handler may:

- set a flag;
- reject the directive in the current profile;
- install a datum post-read hook (future);
- select a language profile (future, first directive only).

The table can live in NPL source and be mirrored by a small C table for the
runtime reader, with a consistency test that both readers accept exactly
the same names.

### Diagnostics

Every directive error should include:

- the directive text;
- line and column;
- whether it was unknown, disallowed by the profile, repeated where only
  one is allowed, or not first where order is required.

### State and scoping

- Runtime: per input port, persistent across calls to `read`.
- Compiler: per source file, reset before lexing.
- `load`/`include`: each included source gets its own reset; directives do
  not leak into the includer.
- REPL: the standard input port keeps its options until changed.
- Multiple files in one process are independent.

## Interaction matrix

| Combination | Expected result |
|---|---|
| `#!fold-case` then `#!colon-keywords` | symbols fold; `foo:` is a keyword |
| `#!colon-keywords` then `#!fold-case` | same; order independent |
| `#!colon-keywords` then `#!no-colon-keywords` | last wins; `foo:` is a symbol |
| `#!curly-infix` then `#!colon-keywords` | `{}` and `foo:` both active |
| `#!curly-infix` then `#!no-curly-infix` | core `{}` behavior follows the last directive |
| shebang then directives | `npli` strips shebang; directives apply to the rest |
| `nplc` shebang | rejected with line-1 shebang diagnostic |
| unknown `#!foo` | error in both readers |
| `#!lang` not first | error (future) |

## Migration plan

1. Add `#!colon-keywords` / `#!no-colon-keywords` as aliases for the
   existing `#!srfi-88` behavior; keep `#!srfi-88` working.
2. Move compiler options from the global cell to a per-source record.
3. Move runtime options from ad-hoc reader fields to the port record (done
   in the interim fix; keep it as the stable model).
4. Add validation and identical diagnostics in both readers.
5. Decide the default curly-infix profile and separate neoteric flags.
6. Add the `#!profile`/`#!lang` registry only after the flag system is
   stable and tested.

## Tests

- Consecutive directives, same line and separate lines, both orders.
- Shebang plus directives under `npli`; shebang rejection under `nplc`.
- Runtime directives persist across forms read from one port; a new port
  starts clean.
- Compiler directives reset between files and between includes.
- Unknown directives fail in both readers with line/column.
- `fold-case` + keyword interactions.
- `#!no-colon-keywords` restores `foo:` as a symbol and `b:` as an import
  prefix.
- `#!curly-infix` / `#!no-curly-infix` with and without `{}` syntax.
- `#!lang`/profile ordering and unknown-profile errors when implemented.
- Differential tests: the same source read by `nplc` and `npli` produces
  the same datum stream and observable output.

## Open questions

- Should basic `{...}` be on by default, or only under `#!curly-infix`?
- Should neoteric forms have a separate flag, a profile, or both?
- Is `#!profile r7rs` useful enough to add before full `#!lang`?
- How should a `#!lang` profile interact with library imports and
  `cond-expand` feature identifiers?
- Should `#!srfi-88` be deprecated in docs immediately or kept as a
  compatibility spelling indefinitely?

## Interim state after the first implementation

The current source implements the feature flags as one-off lexer/runtime
switches, with per-port persistence for the runtime and per-file reset for
the compiler. It deliberately does **not** implement the registry,
`#!colon-keywords` alias, unknown-directive parity, or `#!lang`.
This document is the contract for the follow-up refactor.

# Reader Directives

Status: **implemented**. This document is the settled contract for `#!`
reader directives. Language profiles are out of scope.

## Scope

NPL supports two directive forms:

```scheme
#!name
#!(name argument ...)
```

Directives adjust the reader that consumes the rest of the source or port.
They are one-way feature activations, with the R7RS case-folding pair as
the only standardized toggle exception. They do not select a language, load
code, or grant capabilities.

The bracket group form `#![...]` is not part of the design. Consecutive
directives already compose:

```scheme
#!curly-infix
#!colon-keyword
#!(curly-infix neoteric)
```

## Syntax

### Simple form

```text
#!<name>
```

`<name>` is one or more ASCII letters, digits, `-`, or `_`, followed by
whitespace or a datum delimiter. A simple directive consumes no datum.

### Parameterized form

```text
#!(<name> <argument> ...)
```

- The first element is a directive name.
- Arguments are reader data, not evaluated expressions.
- Arity and argument types are defined by the directive registry.
- The parenthesized form is allowed for zero-argument directives, so
  `#!(colon-keyword)` equals `#!colon-keyword`.
- No whitespace is allowed between `#!` and `(`. At the start of a file,
  `#! ` is shebang syntax, not a directive.
- Nested directives inside the argument list are errors.
- The compiler reader currently accepts identifier atoms as arguments;
  the runtime reader accepts general datums and validates them.

## Shebang boundary

A shebang is not a directive. The first character after `#!` decides:

- `#!` followed by `/` or whitespace is a shebang.
- `#!` followed by a letter or `(` is a directive.

`npli` replaces a shebang line with spaces before reading, so line numbers
stay stable and a directive may follow on the next line. `nplc` rejects
shebang lines because compiled source files are not scripts. `nplc` accepts
directive lines.

## Directive registry

| Directive | Arguments | Effect |
|---|---|---|
| `#!fold-case` | none | fold identifiers and character names |
| `#!no-fold-case` | none | return to case-sensitive reading |
| `#!curly-infix` | none, or `neoteric` | enable neoteric forms; core `{...}` is already available |
| `#!colon-keyword` | none | make trailing-colon tokens keyword objects |
| `#!srfi-88` | none | compatibility alias for `#!colon-keyword` |

`#:foo` is always a keyword, independent of `#!colon-keyword`. With the
directive off, `foo:` is an ordinary symbol so R7RS import prefixes such as
`b:` keep working. With it on, write a literal symbol as `|foo:|`.

There is no `#!no-colon-keyword`, `#!no-curly-infix`, or
`#!no-srfi-88`. Start a fresh source or port for the default behavior.
R7RS `#!no-fold-case` is the exception because R7RS defines the pair.

### Neoteric forms

Under `#!curly-infix`:

```scheme
f(x)      => (f x)
f[x]      => ($bracket-apply$ f x)
f{x + 1}  => (f (+ x 1))
```

Adjacency is significant: `f (x)` remains two data. The mappings recurse
left-to-right inside lists, vectors, and curly lists. `(. datum)` maps to
`datum`, so `f(. rest)` is the improper call `(f . rest)`.

Dotted curly-infix lists are always mixed:

```scheme
{a + b . c} => ($nfx$ a + b . c)
```

## Semantics

- **Presence enables.** Repetition is idempotent.
- **Order independent** for independent features.
- **Per port/source.** Runtime options live on the input port and persist
  across `read` calls. Compiler options reset at the start of each source
  file; included files get their own fresh options.
- **Validated.** Unknown names, bad arity, malformed arguments, and nested
  directives are errors in both readers. The wording is kept aligned.
- **No evaluation.** Directives only change how data is read.
- **No language profiles.** `#!` does not select a language or prelude.

## State model

Runtime:

- `NplPort` stores `read_fold_case`, `read_keyword_colon`, and
  `read_neoteric`.
- Each `npl_read_datum` initializes an `NplDatumReader` from the port.
- A directive updates both the active reader and its port.
- A fresh port starts from the defaults.

Compiler:

- `LEX-FOLD-CASE`, `LEX-SRFI-88`, and `LEX-NEOTERIC` are lexer cells.
- `parse-source-text` resets them before lexing.
- Adjacency and the neoteric flag are captured per token, so a mid-file
  directive does not retroactively change earlier data.
- Included sources and library files are parsed independently.

## Extending the reader

To add a directive:

1. Choose a name, arity, and argument grammar.
2. Add a branch to the runtime directive dispatcher and the compiler lexer
   dispatcher. Keep the accept/reject wording identical.
3. If the option persists, add a field to `NplPort`, initialize it in
   `make_port`, and copy it into `NplDatumReader`.
4. If the compiler needs per-token state (for example adjacency-sensitive
   syntax), store it in the token, not only in a global cell.
5. Reset compiler state in `parse-source-text`.
6. Add a compiler suite, a negative reader test, and a native/REPL
   differential fixture. Update the coverage ratchet if a new export is
   introduced.

Extension arguments are validated by the directive; there is no dynamic
plugin loading and no user code runs at read time.

## Interaction matrix

| Combination | Result |
|---|---|
| `#!fold-case` then `#!colon-keyword` | symbols fold; `foo:` is a keyword |
| `#!colon-keyword` then `#!fold-case` | same; order independent |
| `#!colon-keyword` repeated | still enabled; idempotent |
| `#!curly-infix` then `#!colon-keyword` | neoteric forms and `foo:` both active |
| `#!curly-infix` repeated | still enabled; idempotent |
| `#!(curly-infix neoteric)` | same as `#!curly-infix` in the current reader |
| `#!(colon-keyword)` | same as `#!colon-keyword` |
| shebang then directives | `npli` strips the shebang; directives apply to the rest |
| `nplc` shebang | rejected |
| unknown `#!foo` | error in both readers |
| nested `#!(curly-infix #!fold-case)` | error in both readers |

## Tests

- Simple and parameterized directives, same line and separate lines.
- Zero-argument parenthesized directives.
- Unknown names, arity errors, malformed arguments, nested directives.
- Runtime option persistence across reads; fresh ports reset.
- Compiler reset between files and includes.
- Mid-file directives and per-token adjacency.
- Differential native/REPL fixtures for observable output.

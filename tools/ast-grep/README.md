# NPL ast-grep Lint Suite (C + Scheme/NPL)

Turns recurring agent and code-review defects into generalized structural AST
rules using [ast-grep](https://ast-grep.github.io/) +
[tree-sitter-scheme](https://github.com/6cdh/tree-sitter-scheme) (for `.npl`,
`.sld`, `.scm`) alongside ast-grep's built-in C parser (for `runtime/` and
`lib/**/native/`).

## Quick Start

1. Install `ast-grep` (`npm install -g @ast-grep/cli@0.51.3` or `cargo install ast-grep`).
2. Build the local `tree-sitter-scheme` parser into `.cache/ast-grep/` (gitignored):
   - Windows (PowerShell): `.\tools\ast-grep\setup.ps1`
   - Linux / macOS: `./tools/ast-grep/setup.sh`
3. Run rule unit tests + full scan:
   - Windows (PowerShell): `.\tools\ast-grep\lint.ps1`
   - Linux / macOS: `./tools/ast-grep/lint.sh`
   - Direct CLI: `ast-grep scan --custom-languages allow runtime src lib`

## Boundary with Repository Library Checks

- **Per-file structural invariants (fast, <1s)** live here in `tools/ast-grep/rules/`
  and run on every PR in the `AST lint (C & Scheme)` CI job—including
  `scheme-no-redefine-shared-util`, which prevents re-implementing helpers
  already exported by `(npl util)` and `(npl test util)`.
- **Cross-file library invariants** (arbitrary duplicate private definitions
  across all 94 libraries, export-origin tracking, symbol coverage) are enforced
  by the NPL suite in `tools/libraries/duplicates.scm` and `tools/libraries/` in
  the `Linux gate` CI job.

## Layout

- `sgconfig.yml` — Root config registering `customLanguages.scheme` (`.npl`, `.sld`, `.scm`), rule directory, and test directory.
- `tools/ast-grep/rules/c/` — Generalized C runtime & native-package rules.
- `tools/ast-grep/rules/scheme/` — Generalized Scheme / NPL compiler & library rules.
- `tools/ast-grep/rule-tests/` — Positive (`invalid`) and negative (`valid`) tests for every rule (`ast-grep test`).

## Turning a New Mistake into a Rule

Whenever a review or bug catches a repeatable pattern:

1. Generalize from the specific incident to the full defect class (e.g. `_rmdir` -> all narrow Win32/CRT path APIs; one duplicate helper -> all shared `(npl util)` helpers).
2. Add or extend a rule YAML under `tools/ast-grep/rules/c/` or `tools/ast-grep/rules/scheme/` with a clear `message`, `note`, and `fix:` rewrite where feasible.
3. Add matching `valid` and `invalid` cases in `tools/ast-grep/rule-tests/`.
4. Verify with `.\tools\ast-grep\lint.ps1` (or `./tools/ast-grep/lint.sh`).

# NPL ast-grep Lint Suite (C + Scheme/NPL)

Turns recurring agent and code-review defects into structural AST rules using
[ast-grep](https://ast-grep.github.io/) + [tree-sitter-scheme](https://github.com/6cdh/tree-sitter-scheme)
(for `.npl`, `.sld`, `.scm`) alongside ast-grep's built-in C parser (for `runtime/` and `lib/**/native/`).

## Quick Start

1. Install `ast-grep` (`npm install -g @ast-grep/cli` or `cargo install ast-grep`).
2. Build the local `tree-sitter-scheme` parser into `.cache/ast-grep/` (gitignored):
   - Windows (PowerShell): `.\tools\ast-grep\setup.ps1`
   - Linux / macOS: `./tools/ast-grep/setup.sh`
3. Run rule unit tests + full scan:
   - Windows (PowerShell): `.\tools\ast-grep\lint.ps1`
   - Linux / macOS: `./tools/ast-grep/lint.sh`
   - Direct CLI: `ast-grep scan --custom-languages allow runtime src lib`
   - To trust this workspace once so `--custom-languages allow` can be omitted interactively:
     `ast-grep scan --custom-languages trust runtime src lib`

## Layout

- `sgconfig.yml` — Root config registering `customLanguages.scheme` (`.npl`, `.sld`, `.scm`), rule directory, and test directory.
- `tools/ast-grep/rules/c/` — C runtime & FFI rules (`runtime/`, `lib/**/native/`).
- `tools/ast-grep/rules/scheme/` — Scheme / NPL compiler & library rules (`src/`, `lib/`).
- `tools/ast-grep/rule-tests/` — Positive (`invalid`) and negative (`valid`) regression tests for every rule (`ast-grep test`).

## Turning a New Mistake into a Rule

Whenever a review or bug catches a repeatable pattern:

1. Add a rule YAML under `tools/ast-grep/rules/c/` or `tools/ast-grep/rules/scheme/` with a clear `message`, `note`, and tight `files` / `ignores` scope.
2. Add a matching `*-test.yml` under `tools/ast-grep/rule-tests/` containing both the buggy snippet (`invalid`) and the fixed snippet (`valid`).
3. Verify with `.\tools\ast-grep\lint.ps1` (or `./tools/ast-grep/lint.sh`) that all rule tests pass and the current tree reports zero violations.

# AGENTS.md - NPL Project Instructions

## Core Principles

- **Reuse before reinvention.** Before adding code, inspect the existing compiler frontend, backend, runtime, libraries, evaluator, and helper APIs. Reuse or extract existing behavior whenever it applies. Do not create a parallel implementation of a facility that already exists. In particular, evaluator and REPL work must reuse the shared frontend semantics for datum handling, bindings, hygienic macros, import resolution, derived forms, and core AST behavior whenever those boundaries are available. If existing code is too coupled to reuse directly, refactor toward a shared boundary before duplicating semantics.
- **Keep source files focused.** Aim to keep project-owned `.c` and `.npl` files at or below 800 lines, excluding generated/build artifacts and third-party QBE. Before growing a file near or beyond that soft limit, extract cohesive responsibilities into existing or clearly named modules; do not compress formatting or split arbitrarily just to meet a line count. Explain necessary exceptions. Address oversized files when relevant work touches them, rather than launching unrelated repository-wide refactors. Permanent bootstrap freeze rules still apply.
- **NPL-first implementation.** Implement language, compiler, driver, REPL, evaluator, library, and other product business logic in NPL whenever the current toolchain can express it. Keep C limited to runtime mechanisms, operating-system and libc wrappers, FFI boundaries, and the minimum host bootstrap required to build or recover NPL code. Do not move orchestration or language policy into C for convenience.

## Required Workflow

- Search the repository and read the relevant existing implementation before designing a new abstraction.
- When a behavior has both compiler and evaluator paths, identify the shared semantic boundary and add differential tests or common fixtures; passing tests for only one path does not establish semantic consistency.
- Explain any intentional duplication in the change description, including why reuse or extraction was not practical.
- Keep the permanent bootstrap compiler, `bootstrap/bootstrap.ssa`, stage0, and their frozen runtime `bootstrap/runtime/` frozen unless explicitly authorized. Do not make the current runtime (`runtime/`) compatible with them; the permanent chain links only against `bootstrap/runtime/`.
- Use C99 for stage0 and runtime C, and preserve canonical, reproducible QBE SSA output.
- Run the smallest relevant acceptance checks after each increment and report their output as evidence.
- **Codify review patterns with `ast-grep`.** Whenever a code review or bug exposes a repeatable, pattern-based mistake in C (`runtime/`, `lib/**/native/`) or Scheme/NPL (`src/`, `lib/`), add a structural rule under `tools/ast-grep/rules/` with positive/negative test cases in `tools/ast-grep/rule-tests/` to prevent regressions—and add a `fix:` rewrite to refactor existing occurrences where feasible. Run `./tools/ast-grep/lint.sh` (Linux/macOS) or `.\tools\ast-grep\lint.ps1` (Windows) when modifying C or NPL/Scheme sources.
- Use git history and pull requests (`gh pr`) to record completed changes and project state; keep only uncommitted current focus in `.agents/focus.md` (gitignored).

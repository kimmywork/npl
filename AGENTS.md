# AGENTS.md - NPL Project Instructions

## Core Principles

- **Reuse before reinvention.** Before adding code, inspect the existing compiler frontend, backend, runtime, libraries, evaluator, and helper APIs. Reuse or extract existing behavior whenever it applies. Do not create a parallel implementation of a facility that already exists. In particular, evaluator and REPL work must reuse the shared frontend semantics for datum handling, bindings, hygienic macros, import resolution, derived forms, and core AST behavior whenever those boundaries are available. If existing code is too coupled to reuse directly, refactor toward a shared boundary before duplicating semantics.
- **NPL-first implementation.** Implement language, compiler, driver, REPL, evaluator, library, and other product business logic in NPL whenever the current toolchain can express it. Keep C limited to runtime mechanisms, operating-system and libc wrappers, FFI boundaries, and the minimum host bootstrap required to build or recover NPL code. Do not move orchestration or language policy into C for convenience.

## Required Workflow

- Search the repository and read the relevant existing implementation before designing a new abstraction.
- When a behavior has both compiler and evaluator paths, identify the shared semantic boundary and add differential tests or common fixtures; passing tests for only one path does not establish semantic consistency.
- Explain any intentional duplication in the change description, including why reuse or extraction was not practical.
- Keep the permanent bootstrap compiler and `bootstrap/bootstrap.ssa` frozen unless explicitly authorized.
- Use C99 for stage0 and runtime C, and preserve canonical, reproducible QBE SSA output.
- Run the smallest relevant acceptance checks after each increment and report their output as evidence.
- Record completed logical changes in `.agents/worklog/YYYY-MM-DD.md` and keep `.agents/workstate.md` current.

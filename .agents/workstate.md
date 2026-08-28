# Workstate

## Current

The permanent bootstrap language and compiler are frozen. The evolving full compiler has completed the first two `PLAN.md` increments.

- `stage0/`: untyped C99 compiler with QBE and executable output.
- `bootstrap/compiler/`: permanent modular NPL bootstrap compiler.
- `src/compiler/`: full compiler split into `common/`, `reader/`, `frontend/`, and `backend/`, with NPL suites under `tests/`.
- `bootstrap/bootstrap.ssa`: reproducible 6,753-line bootstrap fixed point.
- `bootstrap/current.ssa`: reproducible 18,451-line full-compiler fixed point.

Dynamic Core includes lexical closures, shared mutation, `let*`, distinct `letrec`/`letrec*` initialization, internal definitions, first-class calls, R7RS truth semantics, and proper tail calls.

Reader/Datum includes dotted data, R7RS ASCII character and string escapes, static and runtime vectors/bytevectors, nested quasiquote, shared/cyclic datum labels, and line/column delimiter diagnostics.

## Evidence

`./tools/run_tests.sh` reports `all suites: PASS`. `tools/check_bootstrap.sh` owns both canonical SSA recovery checks. `tools/check_full_compiler.sh` restores the current compiler, runs thunk-based NPL suites including ten million cross-function tail calls, checks literal mutation contracts, and validates malformed reader fixtures.

## Next

Implement Plan step 3 in `src/compiler/`: hygienic `syntax-rules`, ellipsis, and the remaining derived forms. Keep `bootstrap/compiler/` and `bootstrap/bootstrap.ssa` unchanged, then regenerate `bootstrap/current.ssa` through the full-compiler seed chain.

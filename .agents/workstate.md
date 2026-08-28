# Workstate

## Current

The permanent bootstrap language includes directly desugared `cond` and `case`. Bootstrap source dispatch uses these forms instead of nested `if` chains. Stage0 has no general macro expander.

The three compiler codebases and SSA recovery chain are established.

- `stage0/`: untyped C99 compiler with QBE and executable output.
- `bootstrap/compiler/`: permanent modular NPL bootstrap compiler.
- `src/compiler/`: evolving full compiler baseline.
- `bootstrap/bootstrap.ssa`: reproducible 6,753-line bootstrap fixed point.
- `bootstrap/current.ssa`: reproducible 6,753-line full compiler artifact.

## Evidence

`./tools/run_tests.sh` reports `all suites: PASS`.

## Next

Implement the first missing R7RS core increment in `src/compiler/`, regenerate `bootstrap/current.ssa`, and leave the permanent bootstrap compiler unchanged.

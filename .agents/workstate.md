# Workstate

## Current

The three compiler codebases and SSA recovery chain are established.

- `stage0/`: untyped C99 compiler with QBE and executable output.
- `bootstrap/compiler/`: permanent modular NPL bootstrap compiler.
- `src/compiler/`: evolving full compiler baseline.
- `bootstrap/bootstrap.ssa`: reproducible bootstrap fixed point.
- `bootstrap/current.ssa`: reproducible full compiler artifact.

## Evidence

`./tools/run_tests.sh` reports `all suites: PASS`.

## Next

Implement the first missing R7RS core increment in `src/compiler/`, regenerate `bootstrap/current.ssa`, and leave the permanent bootstrap compiler unchanged.

# Workstate

## Current

Stage0 frontend is complete: generic S-expression AST, fixnum/boolean/string/symbol/pair reader, and recursive top-level `include` expansion.

## Evidence

`./tools/check_stage0.sh` prints `stage0 frontend: PASS`.

## Next

Implement tagged QBE emission for the stage0 language in `PLAN.md` section 2.

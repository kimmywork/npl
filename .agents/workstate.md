# Current Workstate

## Focus

Advance the self-hosted compiler's R7RS-small subset while preserving the Phase 8 bootstrap fixpoint.

## Status

- Stage0 reports matched delimiter errors with opening line/column locations.
- Nplc1 preflights unmatched parentheses outside strings and comments.
- Let-family parsing and the basic `case` subset pass Stage A.
- Bootstrap fixpoint passes with byte-identical 7167-line il2/il3 output.

## Remaining Gaps

- `case =>` and additional datum forms are not implemented.
- `letrec` still uses sequential binding emission, not full location preallocation semantics.
- Stage0 detects self-recursive tail position but still emits a normal call.

## Next

Implement and verify `do`, then decide whether `case =>` or full `letrec` semantics is the next R7RS increment.

## References

- `docs/HANDOFF.md`
- `docs/r7rs-audit.md`
- `docs/design-inc1-core.md`
- `tools/check_bootstrap.sh`

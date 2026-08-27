# Current Workstate

## Focus

Implement remaining `PLAN.md` and `DESIGN.md` scope, excluding parallel scheduling and structured concurrency.

## Verified

- Bootstrap fixpoint is byte-identical at 9649 lines.
- Runtime mmap/mark-sweep GC passes million-allocation and live-graph acceptance.
- Proper tail calls, managed closures, and escape-only `call/cc` pass focused acceptance.
- nplc0 and nplc1 parse explicit variable/parameter/return annotations.
- Fully annotated Integer/Bool functions emit unboxed `function w` bodies in both compilers.
- Dynamic wrappers guard inputs and box results; bad values fail at runtime.
- Type acceptance covers static mismatches, direct typed calls, Bool, globals, lexical shadowing, and 10,000,000 nplc0 typed self-tail iterations.

## Remaining Gaps

- Phase 5 still lacks algorithm W/let-polymorphism, typed lambda, Float/compound unboxing, and nplc1 typed tail lowering.
- Phase 7 lacks base library artifacts, vectors, bignum, and executable flonum support.
- Phases 10-12 are not implemented.

## Next

Complete Phase 5 inference and typed closure/tail support while preserving the bootstrap fixpoint.

## References

- `PLAN.md`
- `DESIGN.md`
- `docs/HANDOFF.md`
- `tools/check_type.sh`

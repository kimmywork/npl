# Current Workstate

## Focus

Implement remaining `PLAN.md` and `DESIGN.md` scope, excluding parallel scheduling and structured concurrency.

## Verified

- Bootstrap fixpoint is byte-identical at 10254 lines.
- Runtime GC, proper tail calls, managed closures, and escape-only `call/cc` pass focused acceptance.
- nplc0 and nplc1 emit unboxed Integer/Bool functions with Dynamic guards and boxing.
- nplc0 local HM includes fresh variables, occurs-check unification, let-generalization, and instantiation.
- One local identity closure instantiates at both Integer and Bool in the same typed function.
- Explicit typed lambdas capture unboxed values through managed environments.
- Both nplc0 and nplc1 run 10,000,000 unboxed self-tail iterations.

## Remaining Gaps

- Phase 5 lacks Float and complete Pair/record/function compound unboxing; self-host HM/typed lambda remains narrower than nplc0.
- Phase 7 lacks base library artifacts, vectors, bignum, and executable flonum support.
- Phases 10-12 are not implemented.

## Next

Add executable Float and compound typed lowering, then align the self-hosted typed subset.

## References

- `PLAN.md`
- `DESIGN.md`
- `docs/HANDOFF.md`
- `tools/check_type.sh`

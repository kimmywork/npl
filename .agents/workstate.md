# Current Workstate

## Focus

Implement remaining `PLAN.md` and `DESIGN.md` scope, excluding parallel scheduling and structured concurrency.

## Verified

- Bootstrap fixpoint is byte-identical at 10742 lines.
- Runtime GC, proper tail calls, managed closures, and escape-only `call/cc` pass focused acceptance.
- nplc0 and nplc1 emit unboxed Integer/Bool/Float functions with Dynamic guards and boxing.
- Both compilers preserve `d` values through typed self-tail loops.
- nplc0 local HM includes unification, let-generalization, and polymorphic instantiation.
- Explicit typed lambdas capture unboxed values through managed environments.
- nplc0 typed Pair/function pointers use `l`, boundary kind guards, typed access, and closure apply.

## Remaining Gaps

- Phase 5 lacks record/row/refinement and full self-host HM/typed-lambda/structured-type support.
- Phase 7 lacks base library artifacts, vectors, bignum, and complete numeric-tower support.
- Phases 10-12 are not implemented.

## Next

Implement Phase 7 numeric/base primitives that also supply the compound runtime values needed by later tensors.

## References

- `PLAN.md`
- `DESIGN.md`
- `docs/HANDOFF.md`
- `tools/check_type.sh`

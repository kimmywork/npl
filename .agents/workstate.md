# Current Workstate

## Focus

Complete language features in the self-hosted compiler. Keep stage0 limited to compiling and running `src/compiler.npl`.

## Verified

- Bootstrap Stage A passes.
- Bootstrap fixpoint is byte-identical at 11745 lines.
- Self-hosted Integer/Bool/Float typed emission and typed self-tail paths pass current tests.
- Self-hosted Pair parameters support guards and typed construction/access.
- Self-hosted unary `(-> A B)` and explicit non-capturing typed lambdas use managed closures, function guards, and `npl_apply`.
- Runtime GC, proper tail calls, managed closures, and escape-only `call/cc` pass focused acceptance.

## Remaining Gaps

- Phase 5 lacks self-hosted lambda capture, HM, record/row, and refinement support.
- Phase 7 lacks base library artifacts, vectors, bignum, and complete numeric-tower support.
- Phases 10-12 are not implemented.

## Next

Add managed captures to self-hosted typed lambdas without extending stage0.

## References

- `PLAN.md`
- `DESIGN.md`
- `docs/HANDOFF.md`
- `tools/check_type.sh`
- `tools/check_bootstrap.sh`

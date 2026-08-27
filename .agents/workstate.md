# Current Workstate

## Focus

Complete language features in the self-hosted compiler. Keep stage0 limited to compiling and running `src/compiler.npl`.

## Verified

- Bootstrap Stage A passes.
- Bootstrap fixpoint is byte-identical at 11019 lines.
- Self-hosted Integer/Bool/Float typed emission and typed self-tail paths pass current tests.
- Self-hosted `(Pair A B)` parameters use QBE `l`, Dynamic boundary guards, typed `cons/car/cdr`, and element-type checking.
- Runtime GC, proper tail calls, managed closures, and escape-only `call/cc` pass focused acceptance.

## Remaining Gaps

- Phase 5 lacks self-hosted function types, typed lambda, HM, record/row, and refinement support.
- Phase 7 lacks base library artifacts, vectors, bignum, and complete numeric-tower support.
- Phases 10-12 are not implemented.

## Next

Implement self-hosted function type parsing and explicit typed lambda lowering without extending stage0.

## References

- `PLAN.md`
- `DESIGN.md`
- `docs/HANDOFF.md`
- `tools/check_type.sh`
- `tools/check_bootstrap.sh`

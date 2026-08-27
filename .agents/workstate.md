# Current Workstate

## Focus

Complete language features in the self-hosted compiler. Keep stage0 limited to compiling and running `src/compiler.npl`.

## Verified

- Bootstrap Stage A passes.
- Bootstrap fixpoint is byte-identical at 10488 lines.
- `src/compiler.npl` uses a runtime-call record constructor and shared lookup for fixed-arity runtime calls.
- Runtime GC, proper tail calls, managed closures, and escape-only `call/cc` pass focused acceptance.
- Self-hosted Integer/Bool/Float typed emission and typed self-tail paths pass current tests.

## Remaining Gaps

- Phase 5 lacks record/row/refinement and full self-host HM/typed-lambda/structured-type support.
- Phase 7 lacks base library artifacts, vectors, bignum, and complete numeric-tower support.
- Phases 10-12 are not implemented.

## Next

Implement missing compiler and language behavior in `src/compiler.npl`; change stage0 only when bootstrap cannot express or run the self-hosted compiler.

## References

- `PLAN.md`
- `DESIGN.md`
- `docs/HANDOFF.md`
- `tools/check_bootstrap.sh`

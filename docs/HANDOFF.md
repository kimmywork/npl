# NPL Current Handoff

Status: **approval-gated; no new implementation increment may start before user approval**.

## Current objective

Complete the R7RS-small standard libraries specified by `docs/reference/r7rs.html` and validate one observable behavior contract through all three surfaces:

- NPL compiled native executables;
- the persistent NPL REPL;
- Guile in its R7RS environment.

Native and REPL execution must not have different R7RS semantics. Execution paths may differ internally, but results, error classes, dynamic state, port behavior, file effects, resource closure, and exit status must agree. Guile is an external reference for finding differences; Guile extensions do not define the NPL contract.

The complete approved-for-review plan is:

```text
docs/plan-r7rs-conformance-guile.md
```

Until the user explicitly approves that plan:

- do not modify implementation files;
- do not start number-tower, Unicode, time, eval/load/repl, or any other library increment;
- do not add primitives, bridges, runtime interfaces, or architecture;
- do not implement benchmark tooling;
- only revise the plan, dependency order, or handoff if requested.

## Current baseline

- Branch: `master`.
- The current standard-library implementation is partial and must not be described as full R7RS-small conformance.
- `(scheme eval)` has a partial `environment`/`eval` implementation with known procedure-crossing, multiple-values, and syntax-visibility gaps.
- `(scheme load)` has a usable single-argument sequential-loading subset; the optional `environment-specifier` form is incomplete.
- Commit `880d74d` contains evaluator changes that must be audited before formal conformance work, especially exploratory primitives not proven necessary by the final path.
- `bootstrap/compiler/` and `bootstrap/bootstrap.ssa` are permanently frozen.
- Current `bootstrap/current.ssa`: 348,879 lines, SHA-256:

```text
ed6f3a59116ac86cff1dc24bbc07562470ab140b2dc5cb058e054cf66f93509e
```

Recent baseline evidence:

```text
./tools/check_repl.sh                            PASS
./tools/run_tests.sh --skip-current-fixed-point  all suites: PASS
git diff --check                              PASS
```

This evidence covers the current finite implementation and tests only; it does not establish complete conformance.

## Dependency order

This is the only permitted semantic order. Public standard-library source may use private implementation boundaries to avoid import cycles, but it may not bypass these semantic prerequisites.

```text
A0 baseline audit, R7RS index, Guile environment
  -> A1 complete binding inventory
  -> A2 shared native/REPL/Guile fixture runner
  -> B1 values, types, errors, mutability, identity
       |----> B2 control, exceptions, dynamic state, multiple values
       |----> B3 textual/binary ports, EOF, closure, flushing, resources
       |----> B5 Unicode characters, strings, and UTF-8
       |----> C4 number tower, complex, inexact, numeric parse/print
                    B3 + B5 + C4
                         |
                         +----> B4 complete datum reader/writer semantics
                                      B1 + B2 + B3 + B4 + B5 + C4
                                                   |
                                                   +----> C1 complete `(scheme base)` export closure
                                                              |
                                                              +----> C2a `(scheme cxr)`
                                                              +----> C2b `(scheme case-lambda)`
                                                              +----> C3 `(scheme char)`
                                                              +----> C5 `(scheme read)` and `(scheme write)`
                                                              +----> C6 `(scheme file)`
                                                              +----> C7 `(scheme process-context)`
                                                              +----> C8 `(scheme time)`
                                                              +----> C9 `(scheme lazy)`
                                                                         |
                                                                         +----> C10 `(scheme eval)`
                                                                                    |
                                                                                    +----> C10 `(scheme repl)`
                                                                                               |
                                                                                               +----> C10 `(scheme load)`
                                                                                                          |
                                                                                                          +----> C11 `(scheme r5rs)`
```

The dependency details are:

- B1 is the prerequisite for every semantic feature.
- B2 depends on B1 and supplies the dynamic/control contract used by base, lazy, and eval.
- B3 depends on B1 and supplies port/resource behavior used by datum I/O, file, and load.
- B5 depends on B1 and supplies Unicode behavior used by strings, char, and datum I/O.
- C4 depends on B1, not on Unicode or ports. It must close before base is marked complete because base exports numeric operations and conversions.
- B4 depends on B3, B5, and C4 so reader/writer behavior covers final ports, Unicode, and numeric representations.
- C1 depends on B1, B2, B3, B4, B5, and C4. A resolving `(scheme base)` library is not enough; every export must be closed in the inventory.
- C2a, C2b, C3, C5, C6, C7, C8, and C9 depend only on the prerequisites listed in the plan and may not pull unfinished behavior forward.
- C10 eval depends on the completed base/control/reader/library contracts.
- C10 repl depends on eval and the established I/O and interaction-environment contract.
- C10 load depends on eval, read, file, and the stable interaction-environment contract. It must not be completed with an unapproved temporary environment protocol.
- C11 is always last and may export only semantics already completed underneath it.

B2, B3, B5, and C4 are independent after B1. They may be executed in the order approved for the current work session, but B4 and C1 may not start until all of their listed prerequisites are complete. This avoids inventing a false dependency while preserving a strict stop point for every increment.

If an implementation step discovers a new prerequisite, stop that increment and request plan review. Do not extend the dependency graph during execution.

## Execution protocol

For each numbered increment, define before editing:

- one unique objective;
- included libraries, exports, and R7RS sections;
- completed prerequisites;
- permitted file categories;
- explicit exclusions;
- focused native, persistent-REPL, and Guile fixtures;
- completion and stop conditions.

Then execute only that increment:

1. Read the relevant R7RS sections and update the inventory.
2. Inspect existing implementation and tests and choose the smallest necessary change.
3. Add the common behavior fixture and its expected result/error classification.
4. Implement only the current increment.
5. Run the focused correctness checks through native, persistent REPL, and Guile.
6. Classify every difference.
7. Run benchmarks only when a performance-relevant representation/algorithm/runtime path changes or a semantic family is complete.
8. Run the full repository gate only after compiler/runtime changes or completion of a semantic family.
9. Review the complete diff, bootstrap freeze, inventory, worklog, and workstate.
10. Commit only after all applicable checks pass.

Unrelated library tests, benchmarks, and full suites must not be repeated for every small edit.

## Minimum correctness evidence

For each applicable export, cover:

- normal result;
- arity error;
- type error;
- range and boundary behavior;
- EOF, closure, and resource errors;
- exception and recovery behavior;
- mutation, identity, sharing, or dynamic state where applicable;
- native/REPL agreement;
- Guile R7RS comparison, or a recorded oracle limitation.

A program merely not crashing is not evidence of correctness. One basic example is not evidence that an API is complete.

## Minimum benchmark evidence

Benchmarks run only after correctness for the corresponding family is complete. Use a small representative set:

- numeric: fixnum, bignum, rational, complex, parse/print;
- data: list, vector, bytevector, string;
- control: closure, tail loop, exception, continuation, promise;
- I/O: read/write, text/binary port, file, load;
- interaction: REPL startup and repeated-input steady-state latency.

Separate compilation, startup, and steady-state time. Fix inputs and environment, use fixed repetitions, and retain raw samples, median, p95, RSS, and the Guile comparison. Do not merge incomparable execution modes into one result.

## Prohibited scope

- Do not expand the current increment.
- Do not add an unlisted primitive merely to pass a test.
- Do not retain exploratory bridges or debug interfaces without final-path necessity evidence.
- Do not create a second standard-semantics implementation.
- Do not modify the permanent bootstrap.
- Do not introduce a control IR, JIT, package manager, dynamic loader, or unrelated refactor.
- Do not describe finite test coverage as full R7RS conformance.
- Do not change the design when blocked; stop and request review.
- Do not run unrelated benchmarks or full validation merely to create activity.

## Next handoff action

Wait for explicit user approval of:

```text
docs/plan-r7rs-conformance-guile.md
```

After approval, start with A0: baseline audit, dependency confirmation, and Guile environment recording. Do not implement a new standard-library feature before A0–A2 are complete.

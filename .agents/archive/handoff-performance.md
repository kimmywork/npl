# Handoff: Core Performance (O1-O3)

Read this, then `.agents/workstate.md` and `AGENTS.md`, before changing
code. Scope: core language performance, stability, and extensibility. GUI
(`.agents/archive/research/gui`) is out of scope until the user schedules it.

## Decisions (user, 2026-10-27)

- P7 "optional inline type checks" is dropped as stated: the bench shows
  type checks are not the bottleneck. P7 is replaced by O1-O3 below.
- Order: O1, then O2, then an O3 design document for user review. Do not
  implement O3 before the user accepts the design.
- O2 and O3 must not block a later concurrent or parallel runtime, and O3
  must fit a later rewrite of core library and runtime logic in NPL (see
  "Future Constraints").

## Evidence (worklog 2026-10-27, "P7 bench")

Scaled CPU seconds (startup amortized), native / Guile / Racket:

| workload | native | Guile | Racket | native/Guile |
|---|---|---|---|---|
| loop | 7.57 | 0.79 | 0.39 | 9.6x |
| fib | 3.81 | 0.40 | 0.27 | 9.5x |
| tak | 2.55 | 0.27 | 0.25 | 9.5x |
| vectors | 7.14 | 0.22 | 0.39 | 32x |
| strings | 6.82 | 1.02 | 0.65 | 6.7x |
| lists | 7.01 | 2.04 | 0.49 | 3.4x |
| closures | 1.95 | 1.67 | 0.35 | 1.2x |
| callcc | 5.48 | 7.13 | 0.37 | 0.77x |
| fact | 0.49 | 1.07 | 0.63 | 0.45x |

Startup CPU: native 0.006 s, npli 0.427 s, Guile 0.047 s, Racket 0.160 s.

Profiles (macOS `sample`, top of stack):

- loop: ~60% in `npl_number_add/sub/compare` and the `npl_is_inexact` /
  `npl_is_complex` calls inside them; ~30% in `npl_tail_request`.
- fib, vectors: `npl_alloc_object`, `npl_gc_sweep_minor_internal`,
  memset, `npl_make_environment`, `npl_environment_ref` lead. Non-tail calls
  allocate heap environments and frames.

## O1: Inline Fixnum Arithmetic

Problem: the emitted fixnum guard already proves both operands are fixnums,
then calls the generic C function, which tests complex, inexact, and
rational before fixnum (`runtime/numeric.c` `npl_number_add` ~562,
`npl_number_sub` ~588, `npl_number_compare`). `+` folds from a `0` seed.

Code sites:

- `src/compiler/backend/numeric.npl`: `emit-integer-fold`,
  `emit-integer-arithmetic`, `emit-numeric-alias-inline`,
  `emit-numeric-alias-ops`, `emit-numeric-alias-guard`.
- `src/compiler/backend/expression.npl`: `emit-compare-pair` (~312).

Approach:

- Fixnums are `value << 2` with tag 0 (`runtime/npl.h`), so tagged values
  add, subtract, and compare directly as signed 64-bit integers, and the
  fixnum range overflows exactly when the tagged int64 operation overflows.
  For add, `s = a + b`, overflow iff `((s ^ a) & (s ^ b)) < 0`; for sub,
  `s = a - b`, overflow iff `((a ^ b) & (a ^ s)) < 0`. On overflow call the
  existing C function (it builds the bignum).
- Compare: `csltl`/`cslel`/`ceql`/... on the tagged operands, no call.
- Drop the `0` seed of `+` when there is at least one operand. Keep `*` as a
  call.
- Reorder `npl_number_add/sub/compare` to test fixnums first; this also
  helps npli, which reaches them through primitives.

Acceptance: overflow boundaries (2^61 - 1, -2^61, mixed bignum, inexact,
rational) under native and npli in a shared fixture (extend
`tests/r7rs/fixtures/integer-arithmetic` if it already runs on both
paths); `tools/run_tests.sh` and `tools/check_linux.sh` all PASS; seed
relay `tools/update_current_ssa.sh` (gen1-gen3 identical); bench before and
after on loop, fib, tak.

## O2: Direct Self Tail Calls

Problem: every tail call allocates an argv, calls `npl_tail_request`
(`runtime/trampoline.c` ~56), and returns to the trampoline, including a
top-level function calling itself in tail position.

Code site: `src/compiler/backend/tail.npl` `emit-tail-request`; function
prologue in `src/compiler/backend/program.npl` `emit-function` and
`src/compiler/backend/environment.npl` (argument loads from `%npl_argv`).

Approach: for a tail call to the function being emitted, with the same
fixed arity, no rest parameter, and a name not in `ASSIGNED-NAMES`, assign
the new argument values to the parameter temporaries and `jmp` to a body
label after the argument loads. QBE accepts reassigned temporaries and
builds SSA itself. Evaluate all new arguments before assigning any.
Mutual recursion and closure calls keep the trampoline.

Concurrency note: O2 adds no shared state and removes writes to the global
tail context. A `jmp` loop has no runtime call, so a later preemptive
scheduler or multi-thread stop-the-world GC needs a safepoint poll on that
back edge; leave a single emission point for the back edge so a poll can be
added there.

Acceptance: SSA check that `loop`/`tak` contain no `npl_tail_request` on
the self call; tests for argument evaluation order and a captured parameter
(closure created in the loop body sees per-iteration values); gates on
macOS and Linux; relay; bench.

## O3: Call Convention and Environments (design only)

Write `.agents/archive/design/design-calls.md` for user review. Cover:

- Which environments may live on the C stack or in temporaries (not
  captured, not reachable from a continuation captured by `call/cc`), and
  how the CPS conversion and `call/cc` (`runtime/trampoline.c` escape and
  sync frames) constrain this.
- Cheaper non-tail calls to known functions (direct calls instead of
  `npl_call_sync` plus argv allocation) and their interaction with the
  conservative, non-moving GC (`runtime/gc.c` scans the C stack and
  `setjmp` registers).
- Effect on npli (`lib/npl/evaluator-session.scm`, native closures) and
  the shared frontend; differential tests.
- The Future Constraints below, explicitly.
- Increments with acceptance checks and expected bench effect.

## Future Constraints

- Concurrency: the runtime is single-threaded by design. Global mutable
  state includes `g_tail_context`, `g_sync_frame`, `g_escape_frames`
  (trampoline.c), `g_current_wind/handler/parameters` (control.c), the GC
  heap, nursery, and bump pointer (alloc.c), the symbol table (symbol.c),
  and current ports (port.c). The intended direction is a per-thread
  context, not a global lock. O3 must say how a thread context reaches
  compiled code (thread-local or an extra parameter) so the new convention
  does not have to change again.
- NPL runtime rewrite: core library and runtime logic will move to NPL
  later (AGENTS.md NPL-first); GC, allocation, and OS boundaries stay in C.
  That move needs cheap calls (O3) and inline primitives (O1) so NPL code
  can replace C fast paths without slowdown. O3 must state which raw
  operations NPL code needs (fixnum ops, tag tests, object field access).

## Working Rules Learned

- Control conversion copies an `if`'s continuation into both branches; new
  compiler code keeps tests in tail position of small helpers, or the seed
  grows (P6b first draft grew it by 11,145 lines).
- An encoding change needs two steps: install gen2, then a strict relay.
  Relays can take over an hour under load; run them in the background.
- `tools/bench.py` includes startup in each run. For startup-amortized
  numbers, repeat `(bench-main)` per workload (the P7 run used repeats
  callcc 10, closures 3, fact 10, fib 20, lists 10, loop 10, strings 20,
  tak 50, vectors 50); adding this as a `bench.py` option is in scope for O1.
- Commits: specific files only, never `.claude/` or `tools/__pycache__/`;
  record each increment in `.agents/worklog/YYYY-MM-DD.md` and update
  `.agents/workstate.md`.

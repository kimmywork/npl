# Design: Call Convention and Environments (O3)

Status: accepted 2026-10-27 with the recommended option for D1-D4 (see
"Decisions").
Follows O1 (inline fixnum arithmetic) and O2 (direct self tail calls);
see [handoff-performance](../.agents/handoff-performance.md).

## Goal

Make non-tail calls and closure environments cheap enough that fib and tak
approach Guile and that later NPL code can replace C fast paths without a
slowdown, without breaking re-entrant `call/cc`, the conservative non-moving
GC, npli, or a later per-thread runtime.

## Current State

Checked 2026-10-27 at `2a336ef`.

Interleaved A/B, CPU seconds, startup amortized (`bench.py --scaled`):

| workload | before O1 | O1 | O2 | Guile |
|---|---:|---:|---:|---:|
| loop | 7.41 | 1.26 | 0.33 | 0.79 |
| fib | 3.77 | 2.95 | 2.89 | 0.43 |
| tak | 2.46 | 2.12 | 2.05 | 0.27 |

fib and tak are 6.7x and 7.6x Guile. Their cost is the non-tail call.

### Every call is CPS, and every continuation is a heap closure

- Control conversion (`src/compiler/frontend/control.npl`) gives every
  source procedure a final continuation parameter and puts every procedure
  call in tail position. Only literals, lambdas, and single-value
  primitives or numeric aliases with simple operands stay in value position
  (`control-simple?`).
- A non-tail call `(f x)` becomes `(f x k)` with
  `k = (lambda (v) (set! v (npl_expect_one v)) rest...)`
  (`control-continuation`). `k` is a closure capturing what `rest`
  references, including the outer continuation.
- `call/cc` is fully re-entrant: the continuation object is a closure over
  `k` plus the wind and handler state (`control.npl` call/cc,
  `npl_wind_transfer` in `runtime/control.c`). Any continuation may be
  captured and resumed later, any number of times.
- Calls return to the trampoline: `npl_tail_request` /
  `npl_tail_request_closure` store the callee and arguments in the global
  tail context, then the function returns (`runtime/trampoline.c`).

### Cost of one fib node (two non-tail calls, one addition)

From the emitted SSA of `_npl_fn_fib` and its two continuation lambdas:

| item | count | where |
|---|---:|---|
| heap objects | 6 | 2 environment vectors, 2 closures, 2 `set!` cells |
| bytes allocated | ~576 | 32 payload + 64 `NplAllocMeta` each |
| `npl_environment_ref` (out of line, checked) | 4 | `bind-captures` |
| `npl_make_environment` + `npl_make_closure` | 2 + 2 | `emit-capture-environment` |
| `npl_cons`, `npl_car`, `npl_set_car` (the cells) | 2, 4, 2 | `set!` in `control-continuation` |
| `npl_expect_one` | 2 | `control-continuation` |
| `npl_tail_request(_closure)` | 3 | one call, two returns |

Two of the six objects and eight of the ~21 runtime calls exist only because
the continuation parameter is rebound with `set!`, which makes it an
assigned variable with a heap cell (`env-bind`). A closure and its
environment are two objects, and every capture read is a call. In the
vectors workload, `npl_environment_ref` is the most frequent call site in
the emitted code (1,882 sites).

### GC stack scan gap (existing bug, found during this design)

`npl_trampoline` sets the GC stack bottom to its own frame
(`trampoline.c` `npl_gc_swap_stack_bottom(&stack_boundary)`). A collection
inside a nested trampoline therefore scans only frames newer than it. Live
temporaries in the frame that called `npl_call_sync` (the numeric alias
slow path) are not scanned:

```scheme
(define big 100000000000000000000000000000)
(define (step s i)                    ; 24 fresh strings live in this C frame
  (cons (vector (string-append s "a") ... (string-append s "x"))
        (+ big i)))                   ; bignum: npl_call_sync, nested GC
```

With `NPL_GC_NURSERY_BYTES` 512-4096 the program fails deterministically
(`vector-ref: expected a vector`); default settings pass by chance. It
reproduces with the pre-O1 compiler and runtime (`55ec07b`). A temporary
runtime that keeps the outermost bottom passes at every nursery size. This
blocks any design that adds nested C frames, so it is increment C0.

## Constraints

1. **Re-entrant continuations.** A continuation frame may be captured by
   `call/cc` inside any callee that calls unknown code, raises, or uses
   `dynamic-wind`. A continuation frame may therefore live on the C stack
   only when the callee is proven never to capture or escape. Today no such
   analysis exists. Non-captured `let` and parameter bindings are already
   SSA temporaries, not environments.
2. **Conservative, non-moving GC** (`runtime/gc.c`, `runtime/alloc.c`). Roots
   are explicit roots, trampoline contexts, and a word scan of the C stack
   and setjmp registers. Only exact object starts are recognized (interior
   pointers are ignored), and objects never move. Cheney-on-the-MTA
   (continuations on the C stack, evacuated by a copying minor GC) is
   therefore ruled out.
3. **Deep non-tail recursion works today.** `(len (build 1000000 '()))`
   runs natively with an 8 MB C stack because continuations are on the
   heap; npli and Guile match. Any direct-call design must keep this.
4. **Errors.** A primitive error longjmps to the innermost trampoline
   (`npl_raise_from_primitive`). `raise-continuable` resumes the raiser's
   continuation, so it must not discard C frames that are still needed.
5. **npli** is NPL compiled by nplc. It does not use control conversion;
   its evaluated procedures are compiled CPS closures over frame vectors
   (`lib/npl/evaluator-compile.scm`). A convention change reaches npli
   through its own compiled code, the primitive wrappers
   (`backend/primitive-values.npl`), FFI closures (`runtime/ffi.c`), and the
   runtime's C continuations (`sync_return`, `raise_handler_returned`,
   `wind_transition_resume`, `promise_force_resume`, `parameter_converted`).
6. **Seed.** `bootstrap/current.ssa` calls `npl_make_environment`,
   `npl_make_closure`, and `npl_environment_ref`. A layout change needs the
   two-step transition: the runtime keeps the old entry points working
   until the new seed is installed, then a strict relay.

## Options

### C1: keep CPS, cut the constant factors (recommended now)

- **C1a: continuation without a cell.** `control-continuation` binds the
  checked value with a fresh `let` instead of `set!`. The parameter is no
  longer assigned, so no cell: per fib node 6 -> 4 objects, and the
  `cons`/`car`/`set_car` calls disappear. Frontend change only; the
  `if`-continuation copy is unchanged, so seed growth is small.
- **C1b: flat closure with inline captures.** One object
  `{header, function, arity, count, captures[]}` instead of a closure plus
  an environment vector. Compiled code receives the closure as `%npl_env`
  and reads capture `i` with an inline `loadl` (no call, no check: the
  compiler created the closure). Per fib node 4 -> 2 objects; 4
  `npl_environment_ref` calls become loads. Runtime closures with an
  environment value (FFI, control callbacks) store it in capture 0. A
  header flag keeps the old two-object layout working for the seed
  transition, then is removed.
- **C1c: inline cell and pair access.** `car`/`cdr` on cells, and
  `set-car!` with an inline write-barrier fast path (holder not OLD, or
  value not a young pointer), as the inline primitives in O1. These are
  among the raw operations the NPL runtime rewrite needs (below).

Expected effect (estimate, to be measured per increment): per fib node 6
-> 2 objects and ~21 -> ~7 runtime calls; fib and tak about 1.5-2.5x
faster; vectors and closures gain from inline capture loads. Remaining gap
to Guile: the trampoline round trip per call and allocation of one
continuation closure per non-tail call.

### C2: direct calls to known functions (decide after C1)

Compile a non-tail call to a known top-level function as a C call that
returns its value, when the callee (transitively) calls only primitives
that do not call user code and other such functions, and does not use
`call/cc`, `raise-continuable`, `dynamic-wind`, or parameters. Such a
callee gets a second, direct-style entry point; the CPS entry stays for
unknown callers and closures.

- A stack-depth check at the direct entry falls back to the CPS entry when
  the C stack is near its limit, which keeps constraint 3. The check needs
  a per-thread stack limit, and the same site can be the safepoint poll.
- Primitive errors keep working: they longjmp to the caller's trampoline
  and discard direct frames, which is correct for a non-continuable raise.
- Live values in direct frames are found only by the stack scan, so C2
  requires C0.
- Cost: dual emission roughly doubles eligible functions in the seed; a
  new effect analysis in the frontend; npli is unaffected (it calls through
  closures). This is a separate decision, with its own design addendum,
  once C1 numbers show what is left.

### Rejected

- Cheney-on-the-MTA: needs a moving collector (constraint 2).
- Stack-allocated continuation frames without an escape analysis: unsound
  under re-entrant `call/cc` (constraint 1).
- An inlined tail request (compiled code writing the tail context
  directly): ties compiled code to the global context layout, which the
  thread-context decision below must change; keep it a runtime call.

## Future Constraints

### Thread context

Recommendation: compiled code reaches the per-thread runtime context
through one thread-local pointer, not an extra parameter.

- The function signature `(l %npl_env, l %npl_argc, l %npl_argv)` stays,
  so compiled code, FFI trampolines, runtime C continuations, and npli need
  no change when the runtime moves `g_tail_context`, `g_sync_frame`,
  `g_escape_frames`, `g_current_wind/handler/parameters`, the nursery and
  bump pointer, and current ports into a per-thread struct.
- QBE emits thread-local data on the targets in use: Mach-O arm64 (TLV),
  ELF arm64 and amd64 (TP-relative). rv64 lacks only external TLS.
- C1 adds no access to runtime globals from compiled code; every access
  stays a runtime call. Only C2's stack-limit check (and a later safepoint
  poll at the O2 back edge) would read the context inline, one TLS load.
- An extra parameter would cost a register at every call and change every
  function, FFI trampoline, and runtime continuation, plus a relay.

### NPL runtime rewrite

Core library and runtime logic moving to NPL needs these raw operations
emitted inline, declared as primitive-table rows with an inline kind so
the frontend, compiler, and npli share them
([design-primitives](design-primitives.md)):

- fixnum add, sub, compare with overflow fallback (O1, done); mul, shifts,
  bitwise and/or/xor on fixnums;
- tag tests: fixnum, pointer, immediate, box tag of a pointer;
- object field load and store: pair car/cdr, vector slot, closure capture,
  header flags; stores with and without the write barrier;
- fixed-size allocation of a box with a given tag.

C1b and C1c add the closure-capture load and pair access internally; a
private `(npl raw)` library exposing the set is a later increment, with the
runtime rewrite.

## Increments

Each increment: `tools/run_tests.sh` and `tools/check_linux.sh` all PASS,
native and npli differential fixtures, seed relay where SSA changes, bench
before and after on fib, tak, loop, vectors, closures, callcc, and an entry
in the worklog.

1. **C0 GC stack scan.** The stack scan runs from the outermost stack
   bottom; trampoline contexts keep their own bottom only to restore state
   after a longjmp. Acceptance: the reproduction above as a native test at
   nursery sizes 256 B to 16 KiB, also under `NPL_GC_VERIFY`; `gc_stress`
   under ASan; no SSA change. Effect: correctness; a longer scan only while
   nested trampolines are active.
2. **C1a continuation without a cell.** Acceptance: fixtures under native,
   npli, Guile; `multiple-values` and value-context error messages
   unchanged; fib SSA has no `npl_cons`; seed relay. Expected: fib/tak
   10-25% less CPU.
3. **C1b flat closures.** Runtime layout, constructor for compiled
   closures, inline capture loads, runtime and FFI closures migrated; the
   old layout supported until the new seed is installed. Acceptance: two
   step seed transition and strict relay; FFI suites (`c-extension`,
   `typed`, npli `-l`); `NPL_GC_VERIFY` full suite; chibi suite unchanged
   (1207 passed native and npli). Expected: fib/tak another 25-40%;
   vectors and closures gain.
4. **C1c inline cell and pair access.** Dropped 2026-10-29 by the user
   after measurement: inline `car`/`cdr` with a pair tag check, inline
   cell reads, young-cell stores with an old-holder barrier fallback, and
   an inline `npl_expect_one` tag check grew the seed 262,127 -> 349,017
   lines (+33%), with no measurable gain (interleaved A/B, 3 rounds: fib
   1.23 -> 1.24 s, tak 0.99 -> 0.96, vectors 2.81 -> 2.71, callcc 2.76 ->
   2.70, loop and closures unchanged). The runtime calls are cheap next to
   allocation and the trampoline. Inline field access returns with the NPL
   runtime rewrite (D4).
5. **C2 decision.** Decided 2026-10-29: first O2b (named-let and
   `letrec` loops as jumps, the remaining tail-call cost in vectors,
   lists, and strings), then a C2 design addendum for review. O2b and
   O2b-2 done (vectors 2.83 -> 0.50 s). Addendum below, draft for review
   2026-11-02.

## Decisions

Accepted by the user 2026-10-27: D1 C0 now as a standalone fix, D2 C1
first and C2 decided on measurements, D3 thread-local pointer, D4
primitive-table rows.

- **D1** C0 timing: fix the GC scan gap now as a standalone bug fix before
  any O3 work (recommended; it affects existing programs), or as the first
  O3 increment after this design is accepted.
- **D2** Scope: implement C1a-C1c, then decide C2 on measurements
  (recommended), or design C2 now.
- **D3** Thread context: thread-local pointer (recommended) or an extra
  parameter.
- **D4** Raw operations: primitive-table rows with an inline kind, exposed
  later as a private `(npl raw)` library (recommended), or compiler-only
  intrinsics.

## Addendum: C2 Direct Calls (accepted 2026-11-02)

Accepted by the user 2026-11-02 with the recommended E1-E3: C2a (analysis
and numbers, no SSA change) first, then a go/no-go before code generation.

### State after C1 and O2

Interleaved A/B, CPU seconds, startup amortized, `b0ceb56`. Guile from the
P7 run (2026-10-27, a different host load, so ratios are approximate):

| workload | native | Guile | ratio |
|---|---:|---:|---:|
| loop | 0.34 | 0.79 | 0.4x |
| fib | 1.26 | 0.40 | 3.2x |
| tak | 0.99 | 0.27 | 3.7x |
| vectors | 0.50 | 0.22 | 2.3x |
| strings | 3.51 | 1.02 | 3.4x |
| lists | 4.95 | 2.04 | 2.4x |
| closures | 2.04 | 1.67 | 1.2x |
| callcc | 2.84 | 7.13 | 0.4x |

fib and tak are now bound by the non-tail call itself. Per fib node: two
continuation closures (one per non-tail call), about 7 runtime calls
(`npl_make_flat_closure` x2, `npl_expect_one` x2, one `npl_tail_request`,
two `npl_tail_request_closure`), and about 3 trampoline round trips.

### Design

**Direct-safe functions.** A top-level function is direct-safe when it
has fixed parameters, its name is not assigned (top-level function names
cannot be today: `set!: unbound`), and its body, after expansion and before
control conversion, contains only:

- literals, variable references, `if`, `let`, `let*`, `begin`, `lambda`
  (creating a closure is safe; calling an unknown one is not);
- applications of registered intrinsics that never run NPL code, which
  excludes `apply` and every row reaching `npl_force`, `npl_values_dispatch`,
  `npl_parameter_dispatch`, `npl_raise`, or `npl_wind_transfer`
  (`runtime/control.c`); declared C primitives with a `values` result;
- calls to public numeric aliases (their slow path already runs through
  `npl_call_sync`, which forwards a raise);
- calls to direct-safe functions with matching arity.

Not allowed (v1): calls to closures, globals holding procedures, library
procedures other than numeric aliases (this excludes `error`, `raise`, and
the library `exit`, which runs `dynamic-wind` after thunks), `call/cc`,
`dynamic-wind`, `with-exception-handler`, `parameterize`, `guard`,
`set!` of a captured variable inside a nested lambda the function calls,
and a tail call to another function in the same strongly connected
component of tail calls (mutual tail recursion must stay in constant C
stack; a self tail call is the O2 jump). The set is the greatest fixed
point: start with every candidate, remove violators, repeat.

The analysis runs in the frontend (`src/compiler/frontend/control.npl`
area), because control conversion is where it changes the output.

**Direct entry.** Each direct-safe function also gets
`function l $_npl_dfn_<name>(l a0, l a1, ...)`, which returns the value.
Its body is emitted in value position: a non-tail call to a direct-safe
function is a C call, a tail self call is the O2 jump, a tail call to
another direct-safe function is a C call followed by `ret` (bounded by the
SCC rule). The CPS entry `$_npl_fn_<name>` is kept for closures, `apply`,
npli, and the stack fallback below.

**Call sites.** `control-simple?` accepts an application whose head is a
direct-safe function with simple operands, so control conversion no longer
splits there: `(+ (fib (- n 1)) (fib (- n 2)))` stays one expression, and
the backend emits `call $_npl_dfn_fib`. This applies in CPS code too, so a
CPS caller of fib makes no continuation for the call.

**Deep recursion (constraint 3).** The direct entry compares the address
of a stack slot with `npl_stack_limit`; below it, the call runs the CPS
entry through `npl_call_sync` (a nested trampoline whose continuations are
on the heap), so `(len (build 1000000 '()))` still works. The runtime sets
the limit at init from `getrlimit(RLIMIT_STACK)` with a margin for C
primitives that recurse on the C stack (`write`, `equal?`, bignum
printing); 8 MiB today. This compare is the one place a later safepoint
poll and the thread-local limit (D3) go.

**Errors.** A primitive error longjmps to the innermost trampoline and
discards direct frames, which is correct for a non-continuable raise. A
numeric alias slow path raises through `npl_call_sync`, as today.

**GC.** Direct frames hold values in registers and the C stack. The
conservative scan covers them since C0 (the scan reaches the outermost
bottom). No new heap objects; fixnum-only direct code allocates nothing.

**npli.** Unchanged: npli does not use control conversion, and evaluated
procedures call compiled library procedures through closures (CPS entry).
Direct entries are an internal compiler optimization.

### Expected effect

fib and tak lose both continuation closures and every trampoline round
trip per node; each node becomes one C call. Estimate (to be measured):
2-4x on fib and tak. lists, strings, closures, callcc are mostly unaffected
(their hot calls go through closures, `map`, ports, or `call/cc`). Seed:
grows by the direct bodies of eligible compiler functions; measured in C2a
before any code generation change.

### Increments

6. **C2a analysis and numbers.** Frontend computes the direct-safe set; an
   internal flag prints it. No SSA change. Acceptance: a test listing the
   set for fib, tak, `len`, mutual tail recursion (excluded), call/cc,
   closures, `apply`, `error`, `dynamic-wind`, `parameterize` (excluded),
   a numeric alias user (included); the count of eligible functions in the
   compiler and the 9 workloads, with an estimate of seed growth. Then a
   go/no-go with the user.
7. **C2b direct entries and call sites.** Acceptance: shared fixture
   (native, npli, Guile) with 1,000,000-deep non-tail recursion through a
   direct function, a primitive error inside a direct call caught by a
   `guard` in the caller, bignum slow paths, `call/cc` escaping around a
   direct call, a closure created in a direct function and called later,
   small-nursery GC inside direct recursion; SSA check that fib calls
   `$_npl_dfn_fib`; two-step seed transition and relay; macOS and Linux
   gates; `NPL_GC_VERIFY=100` suite; chibi unchanged; A/B on all workloads.
8. **C2c (optional) `error` and `raise` in direct functions** through the
   primitive-error unwinding path, if C2b shows many hot functions are
   excluded only by error checks.

### Decisions (for the user)

- **E1** Scope v1: exclude functions that call `error`/`raise`
  (recommended; C2c later if numbers justify it), or include them now.
- **E2** Stack limit: from `getrlimit` at init with a margin (recommended),
  or a fixed constant.
- **E3** Order: C2a analysis and numbers first, then a go/no-go before any
  code generation change (recommended), or C2a and C2b together.


### As built (C2b, C2c; 2026-11-03)

- **Stack windows.** Direct frames use at most 256 KiB
  (`NPL_DIRECT_STACK_BYTES`, `runtime/stack.c`), because every collection
  scans the whole C stack conservatively. Below the limit a direct entry
  calls `npl_direct_fallback`, which runs the CPS entry on a nested
  trampoline and opens a new window below its own frame, so direct calls
  made from CPS code there run directly again instead of falling back one
  by one. No window ends below a floor that keeps a quarter of the stack
  free. Trampolines and call/cc frames restore the limit they saved.
- **Loop registers.** A direct entry with a self jump combines each
  parameter with `npl_opaque_zero`, which QBE cannot fold, so the loop
  variables get their own registers.
- **C2c.** `raise` with one operand is allowed: in a direct body it becomes
  `npl_raise_direct`, a non-continuable raise delivered like a primitive
  error (the direct frames are discarded and the innermost trampoline runs
  the handler). `raise-continuable` stays excluded. Variadic functions get
  direct entries whose last C parameter is the rest list (the call site
  conses it; the fallback spreads it with `npl_direct_fallback_rest`). A
  variadic self tail call is no jump, so it counts as a tail cycle. `error`
  and the library argument checks become direct-safe through these two
  rules; no special case for `error`. A numeric alias slow path calls the
  library procedure's direct entry when it has one.

## Addendum: C3 Direct Entries for Lambdas (2026-11-05)

### Problem

After C2c, `map` is the gap on the lists workload (its part: 3.5 s CPU
against Guile 0.70 and Racket 0.29). `map-one` calls an unknown closure,
`(procedure (car items))`, in non-tail position. Control conversion
turns that into a CPS call with a continuation closure, and every element
costs one `npl_make_flat_closure`, three trampoline requests, and about
800 instructions of call overhead before any allocation. C2 does not
help: the callee is a closure, not a named function.

### Design

**Direct lambdas.** A lambda with fixed parameters whose body is
direct-safe under the C2 rules (top-level verdicts are settled first, so
a lambda may call direct-safe functions; a call through a parameter or a
captured variable stays unsafe) is converted to
`(219 cps-lambda direct-body)`. The backend emits the CPS function as
today plus a direct entry `$_lambda_N_d(l %npl_env, l a0, ...)` that
takes the arguments as C parameters and returns the value. It checks the stack limit like a
function's direct entry and falls back through `npl_direct_fallback` to
the CPS function. Named-let loop lambdas are never direct (they call their
own captured name), so O2b is unaffected.

**Closure layout.** `NplClosure` gains `direct` (a code pointer, NULL for
every other closure) before `captures[]`; capture *i* moves from byte
40 + 8i to 48 + 8i. The collector does not visit it. Creating a direct
lambda stores the entry into the fresh closure.

**Call sites.** A CPS tail call `(h a ... (lambda (v) body))` whose head
is a variable holding a closure (not a top-level function, primitive, or
self call) and
whose continuation is a literal lambda with a small body (at most 120
nodes, nested lambdas and call sites included) is emitted with two paths. `npl_closure_direct(h, n)` returns the
direct entry when `h` is a closure that has one and accepts `n`
arguments, else 0. The fast path calls the entry and runs `body` inline
with `v` bound, in the same C function. The slow path is today's code.
Wrong arity, non-procedures, parameter objects and every non-direct
closure take the slow path, so their errors are unchanged. In `map-one`
the inline body is the self tail call, so `map` becomes a loop with one
direct call per element.

**Semantics.** A direct lambda captures no continuation and raises only
non-continuably, as C2c functions do, so running it on the C stack is
unobservable. The continuation is not reified on the fast path.

**Seed.** The capture offset is part of the compiled code, so gen1 is
built by the current seed linked with the previous runtime, then linked
with the new one; gen2 and gen3 use the new runtime.

### Acceptance

Shared fixture: map and for-each over direct lambdas, a direct lambda
that raises under guard, call/cc in a mapped procedure (slow path), wrong
arity and non-procedure errors, a captured variable assigned by the
lambda, deep recursion from a direct lambda. Gates, GC verify, chibi.
Bench: lists against Guile and Racket; no workload slower.

## Addendum: C4 Conditional Direct Entries for npli (2026-11-05, for review)

### Problem

npli runs a program by building one native closure per syntax node,
`(lambda (frame) ...)`, which calls its child nodes
(`lib/npl/evaluator-compile.scm`). Every node call is a CPS call through
the trampoline: `fib(25)` (242,785 calls) makes 13.7 million trampoline
requests, about 56 per call, and `fib(30)` takes 1.09 s against 0.008 s
native. Profile: allocation 22%, trampoline requests 34%, closure creation
10%, minor GC 11%.

C3 does not reach these nodes. A node's children are captured variables,
and a call through a variable is an unknown call, so `direct-lambda?`
rejects the node: of npli's 3,861 lambdas, 389 have a direct entry, and
those are mostly leaves (local reference, constant).

Baseline, CPU s, one `(bench-main)` per run; Guile interpreting the same
source with `--no-auto-compile` is the fair comparison for npli:

| workload | npli | Guile interpreter |
|---|---:|---:|
| fib | 1.12 | 0.35 |
| tak | 0.51 | 0.18 |
| loop | 8.46 | 2.41 |
| lists | 2.94 | 0.69 |
| vectors | 1.84 | 0.45 |
| strings | 0.25 | 0.13 |
| fact | 0.09 | 0.12 |
| closures | 0.23 | 0.45 |
| callcc | 1.50 | 1.52 |

### Design

**Conditional direct lambdas.** A lambda gets a direct entry when its body
is direct-safe except for calls whose head is a captured variable that is
never assigned. Each such call is a requirement `(variable . argc)`. In the
direct body the call goes straight to the callee's direct entry, read from
the closure's `direct` field.

**Checked at creation.** The captured values are fixed once the closure is
made (flat closures copy them, and an unassigned variable has no cell).
When the closure is created, each requirement is tested with
`npl_closure_direct`; the direct entry is stored only if every captured
callee is a closure with a direct entry for that arity. Otherwise the
field stays NULL and the closure runs in CPS as today. The evaluator
builds a tree bottom-up, so a node becomes direct exactly when all its
children are: constants, local references, fixed arithmetic, `if`, `let`,
and sequences over such children.

**Soundness.** A direct entry only ever calls direct entries, whose bodies
are direct-safe by the C2 rules, so it never captures a continuation or
raises continuably. Nodes that call a runtime value (application of a
global or a computed procedure, `call/cc`, `dynamic-wind`) stay CPS. The C3
call-site fast path is unchanged and still lets a CPS node call a direct
child without a continuation closure.

**Rejected.** A direct entry that calls an arbitrary closure through
`npl_call_sync` when it has no direct entry: a continuation captured inside
the nested trampoline and re-entered after it returns would resume in the
wrong trampoline (constraint 1).

### Alternative: fused evaluator nodes

Keep the compiler as it is and give the evaluator larger nodes for common
shapes, such as `if` whose test compares a local with a constant, or `+`
and `-` of a local and a constant: one closure where today there are three
or four. This only changes `lib/npl/evaluator-compile.scm` (npli is rebuilt;
the seed and compiled programs are unchanged) and can be measured in a day.
It helps npli only, and each shape is a separate case in the evaluator.

Recommendation: the C4 prototype first, because it reuses the C2/C3
machinery, also speeds up compiled programs that build closure trees, and
its first increment is a measurement with a go/no-go. Fused nodes remain
a cheap follow-up for the shapes C4 leaves in CPS (the `if` and calls of
user procedures). `frontend/direct-calls.npl` is 572 lines, so C4's
analysis goes into its own file.

### Expected effect

In `fib`, the test `(< n 2)`, the operand expressions `(- n 1)` and
`(- n 2)`, and every leaf become direct. The `if`, the `+` over two
calls, the two global applications, and the procedure wrapper stay CPS.
Estimate: about a third of today's requests per call, roughly 2x on fib,
tak, loop, and vectors (to be measured on a prototype first). Recursive
user procedures do not become direct, so this does not close the gap to
the Guile interpreter on its own.

### Increments

1. Prototype: the analysis and code generation for compiled programs,
   with an SSA check on a two-level closure tree, and the fib and loop
   counts under npli. Go/no-go on the measured numbers.
2. Shared fixture: direct and non-direct children mixed in one tree, a
   captured callee assigned later (no direct entry), arity mismatch,
   errors in a direct node under guard, call/cc in a child; npli versions
   of the existing evaluator tests; gates, chibi on npli and native.

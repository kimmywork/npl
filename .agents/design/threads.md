# Threads and the Runtime

Status (2026-11-26): merged baseline `bf6d497`, PR #8. T1.1–T1.3,
T2 `(npl thread)` / `(srfi 18)`, cross-thread user continuations, T2b
virtual threads through increment D, T3-A/B/C (SRFI 230), and the T3-D
weak-order scalar fast paths/benchmark probe are implemented. A **library-level
T4 control prototype** is implemented as `(npl control)` (prompts,
non-composable/composable continuations, barriers, `call-in-continuation`,
`call-in`, `return-to`, inspection, `reset`/`shift`) with compiler-emitted
continuation segments and prompt frames; a T4-D subset (shared-cell
parameterization snapshots and `temporarily`) is in
`(npl control parameters)`. T4-A has the shared segment/semantic-frame
representation on the compiled CPS path, shared runtime continuation objects
across compiled/evaluated libraries, and compiler/evaluator interop fixtures.
Remaining T4-A/T4-C work is the evaluator's direct-style mark-frame adapter and
continuation marks; thread parameters/fluids, promises, conditions and the
thread/interrupt adapters also remain **designs, not implemented features**,
as do optional pair specialization and the optimization/benchmark milestones.
Earlier increment reports are historical; the two PR review sections supersede
their intermediate implementation details.

Current T2b uses per-carrier queues (two carriers by default), migration on
yield, and a growing helper pool for explicitly wrapped blocking work. It
does not yet have work stealing, a netpoller, or transparent suspension of
ordinary SRFI 18 blocking calls. Parameter binding ownership follows the
logical virtual thread, not the carrier. The evaluator shares native control
operators and the C substrate; native-only acceptance coverage must not be
read as proof that npli has no virtual-thread implementation.

The plan below is grounded in `runtime/{coordinator,dynamic-context,
thread-lifecycle,virtual-thread}.c`, the shared frontend, and the checked-in
[SRFI 230](../../docs/reference/srfi-230.html) and
[SRFI 226](../../docs/reference/srfi-226.html) specifications (including their
errata). Correctness debts from PR #8 remain prerequisites where identified;
the local focus file is not a replacement for the merged history.

Next-work navigation: [T3 atomics](#t3-srfi-230),
[T4 control system](#t4-srfi-226),
[correctness and optimization](#correctness-prerequisites-and-later-optimization),
[benchmark plan](#benchmark-plan).

## Question

Should NPL expose native threads directly, or build a Go/JVM-style M:N
scheduler? The runtime should not be tied to one native thread per NPL
thread, but the first implementation does not need green threads.

## Recommendation

Separate three layers:

1. **NPL thread object.** An opaque value with identity, state, result,
   exception, and dynamic context. SRFI 18 operates on this object.
2. **Scheduler interface.** Create, start, join, yield, block, unblock, and
   terminate. The interface does not mention pthreads.
3. **Execution substrate.** Start with one native thread per runnable NPL
   thread (1:1 pthreads). Keep the scheduler interface so a later M:N
   substrate can reuse the same public API.

This is the native-thread starting point for Go-like scheduling. SRFI 18
does not require M:N or impose fairness constraints. Document the fairness
and progress guarantees NPL provides; pthread scheduling alone does not
establish bounds for NPL safepoints or cancellation.

## Why Not M:N First

- The collector is non-moving and scans the active C stack conservatively.
  A CPS virtual thread can avoid retaining a C stack only when suspension
  occurs at a CPS boundary; stackful suspension would need registered stack
  chunks and saved registers.
- Preemption, cancellation, and stop-the-world collection need a defined
  safepoint protocol. NPL currently has no safepoint table or generated poll
  sites.
- Blocking I/O and FFI calls must tell the scheduler to park a light thread
  and release its carrier. Without that, an M:N scheduler blocks all work
  behind one `read`.
- Go pays for M:N with a large runtime: work stealing, netpoller, stack
  growth, and write barriers tuned for stack moves. JVM virtual threads pay
  for continuations and a carrier-thread scheduler.

An M:N scheduler is a second runtime design, not an implementation detail
of SRFI 18. The public thread object and scheduler interface let it arrive
later without changing user code.

## T1 Runtime Foundation

- **Thread registration and context.** Each runtime thread record stores
  native stack bounds, the active stack pointer and register snapshot
  published at a safepoint, its per-thread root stack and root count, dynamic
  state, trampoline/unwind state, and native handle. The primordial thread
  registers during one-time runtime initialization. Foreign threads must
  attach through an explicit registration API before entering NPL; they may
  not call the runtime allocator or retain NPL values after detaching.
- **Safepoints and collection.** Add compiler-visible poll sites at
  allocation slow paths, trampoline dispatch, direct procedure entries,
  generated loop backedges, and long runtime loops (or an equivalent
  mechanism with a documented progress bound). A collector requests a
  world stop; each mutator publishes its stack pointer and scanable register
  context, then parks. The collector scans each parked thread from its saved
  stack pointer to the registered stack bound, visits each thread's root
  stack, and includes process-wide roots. No mutator resumes until collection
  finishes. Validate stack direction and context capture for every supported
  ABI. A thread in arbitrary C code is not safe to scan: an unwrapped call
  keeps GC excluded, while a wrapped call must publish its roots and machine
  context before it releases that exclusion.
- **GC and lock order.** Serialize collector ownership and heap metadata
  updates. Keep the stop-world coordinator separate from the allocator/heap
  mutex; never wait for mutators to park while holding a mutex they need to
  reach a safepoint. Root registration/restoration and remembered-set writes
  belong to the owning thread or are synchronized with the heap mutex.
  Concurrent access to shared mutable NPL objects must use NPL mutexes or
  atomics; unsynchronized shared mutation is outside the contract. Ports are
  the exception: SRFI 18 permits concurrent reads and writes, so the runtime
  must serialize access to the same port with its own synchronization.
- **Per-thread state audit.** Move `g_current_wind`, `g_current_handler`,
  `g_current_parameters`, trampoline and escape/synchronous-call contexts,
  stack limits, GC stack bounds, and reader recovery state into per-thread
  context. Audit every mutable runtime static before T2: classify it as
  thread-local, process-wide immutable, or process-wide synchronized. Keep
  symbol interning and global roots process-wide but synchronized. Make
  runtime initialization one-time and safe against concurrent entry. Public
  parameter and handler APIs read the calling thread's context.
- **Parameter inheritance.** Define independent per-thread parameter
  bindings when inheriting the creator's dynamic environment. Moving only
  `g_current_parameters` into TLS is insufficient: `npl_parameter_set`
  currently mutates shared binding nodes or `NplParameter.value` when no
  binding exists. Preserve shared parameter identity without leaking one
  thread's binding updates into another; test inherited and default bindings.
- **Synchronization.** Mutex and condition-variable primitives over
  pthreads, with timeout and error mapping. SRFI 18 builds on these; it does
  not call pthreads directly.
- **Shared native objects.** There is no process-global directory-handle
  list: `(npl unix)` keeps a directory handle behind a private record, and
  FFI liveness lives in the pointer's GC-managed block. Treat these as
  per-object mutable state. Either enforce single-thread ownership or
  synchronize the entire directory read/close operation and FFI access/free
  check-use-free sequence so a concurrent close/free cannot invalidate an
  in-flight operation.
- **Blocking.** A known blocking wrapper first publishes the thread's safe
  context, then releases runtime exclusion, performs or delegates the call,
  and reacquires exclusion before touching NPL state. GC may proceed while
  such a thread is parked only after its roots and machine context are
  visible to the collector. Publish/acknowledge these transitions under the
  coordinator lock; a returning thread waits for an active collection before
  invalidating its snapshot or accessing the heap. Wrapped native work must
  not mutate managed objects or call back into NPL while marked safe; keep
  argument owners rooted and native resources borrowed until completion.
  An unknown C call keeps GC excluded. Condition
  and mutex waits use the same safe blocked-state transition.
- **Termination.** Join waits for normal return or an uncaught raise;
  cancellation is cooperative, never asynchronous `pthread_cancel`. For a
  running thread, the cancellation request is observed at the same generated
  polls used for safepoints, and runtime-managed blocking waits must be woken
  so the thread can observe it. A thread that has not started has no poll
  site: `thread-start!` and `thread-terminate!` must synchronize the
  not-started-to-terminated transition, so termination can mark it terminated
  and complete without waiting for a generated poll. SRFI 18 requires another
  thread's termination to be acknowledged before `thread-terminate!` returns:
  abandon owned mutexes and publish the terminated-thread exception for join.
  Scheduler termination does not run dynamic-wind after thunks. Specify how
  outstanding native calls finish before resources are released; an
  uninterruptible call can delay termination. Preserve the distinct
  normal-result, uncaught-error, terminated-thread, and timeout outcomes of
  join.

### QBE TLS and the current context

QBE already supplies native thread-local storage. Its `thread data`
definition creates one instance per OS thread; `thread $name` obtains that
thread's address. See [IL linkage and values](../../external/qbe-1.3/doc/il.txt).
Use this mechanism for a current native-context pointer; keep an explicit
process-wide thread registry for the collector to enumerate all contexts.
TLS storage itself is not automatically a GC root.

Generated code currently loads `$npl_stack_limit` directly and emits a global
definition for shared-runtime executables
([backend](../../src/compiler/backend/program.npl),
[runtime](../../runtime/stack.c)). T1 must update both symbol definition and
access together: either use a matching TLS ABI on both sides or access the
limit through the current-context runtime API. Runtime C remains C99; use a
checked compiler TLS extension or pthread keys behind that API, rather than
silently requiring C11 `_Thread_local`.

Choose direct TLS emission per target and link mode. In this vendored QBE,
ELF `thread` uses local-exec; amd64 `extern thread` uses initial-exec, suitable
for shared objects loaded at process startup, not arbitrary later `dlopen`.
Linux arm64 and rv64 reject `extern thread`; Apple targets use TLV access.
Use a runtime accessor when the target/link mode lacks a proven direct TLS
path. Native packages and generated code must reach the same runtime context,
not define independent TLS copies. Verify static/shared runtime identity and
late-loaded package access before enabling a path.

For T2b, native TLS identifies the carrier. Store a pointer to the currently
mounted logical thread in its context; parameter/handler/wind state and
thread-specific values belong to the logical thread record. Switch that
pointer at mount/unmount, and reacquire it after a yield that may migrate to
another carrier. Keep C stack limits and active C unwind frames carrier-owned.
TLS does not supply thread creation, safepoints, synchronization, or a
scheduler.

### T1 implementation status (2026-11-25)

T1.1 (per-thread runtime context) and T1.2 (TLS stack limit and compiler
backend) are implemented. T1.3 (safepoints, stop-the-world coordination,
blocking, termination) and the whole SRFI 18 surface remain ahead. The split
moves the collector-visible context and the compiler/runtime ABI together, as
required above. No NPL thread object, scheduler, or pthread-backed thread
creation exists yet; `npl_thread_attach` is a runtime C registration API, not
`thread-start!`.

**Implemented**

- `runtime/thread.h` / `runtime/thread.c`: one `NplThreadContext` per native
  thread, a native TLS pointer (`NPL_TLS`, the checked C99 `__thread`
  extension), a process-wide registry, `npl_current_context()`,
  `npl_thread_attach()` / `npl_thread_detach()`, `npl_thread_foreach()` and
  `npl_thread_count()`. The primordial thread registers during one-time
  `npl_runtime_init`, which is now safe against concurrent entry.
- Per-thread state moved into the context: GC stack bound and root stack,
  direct-frame window bounds, the wind/handler/parameter chains and wind id,
  trampoline/escape/synchronous-call frames, reader recovery, and reserved
  safepoint/cancellation/T2b fields.
- `npl_gc_mark_roots_internal` walks every registered context's roots and
  `npl_control_mark_roots` walks every context's trampoline chain, so a
  collection on one thread keeps every other thread's roots alive.
  `runtime/thread_test.c` covers this with two live pthreads, plus per-thread
  stack-limit/dynamic-state isolation, attach/detach, and GC root enumeration.
- `npl_stack_limit` is a native TLS slot the generated executable defines as
  `export thread data $npl_stack_limit = { l 0 }`; `npl_opaque_zero` stays a
  process-wide immutable `export data` slot. Both are declared `extern`
  through `npl.h` in every link mode; `runtime/standalone-slots.c` defines
  them for C-only harnesses (`gc_stress`, `numeric_stress`, `thread_test`).
  The backend guard reads the slot with `loadl thread $npl_stack_limit` (one
  IL instruction, no accessor call at each direct entry), and `emit-program`
  emits both definitions unconditionally, removing the static/shared special
  case. This keeps the emitted IL no larger than before and avoids the
  per-entry call an accessor path would add.

**Mutable-global audit (T1 "Per-thread state audit")**

| Class | State | Decision |
|---|---|---|
| Thread-local (moved into `NplThreadContext`) | `g_stack_bottom`, `g_stack_floor`, `g_window_bytes`, `g_roots`/`g_root_count`/`g_root_capacity`, `g_current_wind`/`g_current_handler`/`g_current_parameters`/`g_next_wind_id`, `g_tail_context`, `g_escape_depth`, `g_escape_frames`, `g_next_escape_id`, `g_sync_frame`, `g_next_sync_id`, `g_read_recovery`, `g_read_error_message` | one chain, root stack, and C stack per thread |
| Process-wide immutable | `npl_opaque_zero`, `g_eof_object`, const tables | shared, never written |
| Process-wide, to synchronize in T1.3 | heap metadata (`alloc.c`: chunks, free bins, mark stack, young/remembered logs, bump cursor, thresholds), symbol intern table (`symbol.c`), current-port parameter objects (`port.c`), command line (`io.c`), `g_record_type_tag` (`value.c`), GC verification counters | keep one copy; add the heap/coordinator mutex and port/FFI synchronization before T2 |

**Target / link-mode decision (direct TLS)**

Plain QBE `thread data` (local-exec on ELF, TLV on Apple) is emitted and the
symbol is always defined in the executable, so generated code never needs
`extern thread` (initial-exec, rejected by arm64 ELF / rv64 and unsafe for
late `dlopen`). `qbe -t` probes accept `export thread data` plus
`loadl thread $sym` on `amd64_sysv`, `amd64_apple`, `arm64`, `arm64_apple`,
and `rv64` — every target the vendored QBE builds by default; only
`amd64_win` rejects it and nplc never selects it. The shared runtime declares
`extern __thread` and resolves the executable's TLV through
`-undefined dynamic_lookup` (Darwin) or the dynamic linker (ELF). Static,
`--native=shared`, and `npli` were all exercised (including deep recursion
under a 64 KiB nursery with `NPL_GC_VERIFY`), and the shared runtime and
native packages never define an independent copy. Late `dlopen` of a package
that itself emitted a *direct* TLS access is still unproven and excluded:
external packages do not emit direct entries. Add a runtime accessor path
before enabling that case, per the rule above.

## T1.3 implementation status (2026-11-25)

The parallel stop-the-world foundation (increment A) is implemented. T2's
SRFI 18 surface is **not** part of this increment; the remaining T1.3 items
that depend on it are listed at the end.

**Implemented**

- `runtime/coordinator.c`: a coordinator mutex + condvars, an epoch-based
  world stop, active / parked-for-epoch / blocked counts, `npl_safepoint_poll`,
  `npl_gc_stop_world` / `npl_gc_start_world`, `npl_gc_mark_parked_stacks`, a
  heap mutex (`npl_heap_lock`), a collect lock, `npl_gc_exclude` /
  `npl_gc_include`, `npl_gc_block_begin` / `npl_gc_block_end`, and
  `npl_thread_request_cancel` / `npl_thread_cancel_pending`. A monotonically
  increasing epoch means a long blocking wait is "already parked" and never
  delays a resume.
- `NplThreadContext` gained `jmp_buf registers`, `saved_stack_pointer`,
  `snapshot_valid`, `parked`, `parked_epoch`, `gc_excluded`, `blocked`,
  `cancellation_requested`, `cancel_observed`. At a poll a mutator publishes a
  callee-saved register snapshot and the low end of its scanable C stack, then
  parks. `collect()` scans every parked context's stack range and `jmp_buf`
  **before** draining, in addition to the collecting thread's own stack and
  registers.
- Lock order: collectors serialize with `npl_try_collect_lock` (a losing
  collector retries at a safepoint instead of blocking unparked on a mutex);
  `npl_gc_stop_world` waits for mutators **before** taking the heap lock, so a
  mutator never needs a mutex to reach its poll. Allocation and sweep hold the
  heap lock; `npl_gc_remember_internal` (write barriers) takes it on the slow
  path. Root registration stays per-thread. `npl_runtime_init` now uses
  `pthread_once`.
- Safepoint polls at: `npl_alloc_object` (before the heap lock), every
  `npl_trampoline` dispatch step, every **direct procedure entry** (backend
  `emit-stack-guard` → `emit-safepoint-poll`), every **generated self-jump
  backedge** (backend `emit-self-jump`), and long runtime loops in `write.c`
  (`print_datum`, `find_labels`). `equal_bounded` is budget-bounded, so it has
  a documented progress bound without a poll. The executable owns a third
  `export data $npl_gc_stop_requested = { l 0 }` slot; each poll is one global
  load and a branch on the common path (no C call), and QBE was verified not to
  hoist that load out of a loop.
- Parameter copy-on-write: `NplParameterBinding` carries an `owner` context.
  A binding created by this thread's `parameterize` is still mutated in place
  (continuation re-entry semantics); a binding owned by another thread, or the
  shared `NplParameter.value` default, is shadowed by a fresh thread-owned
  binding. `npl_parameters_leave` pops back to the extent's baseline instead
  of comparing a single frame pointer. `safepoint_test` asserts that a shared
  parameter's default is unchanged in the primordial thread after three
  workers call `parameter-set!`.
- Acceptance: `runtime/safepoint_test.c` runs three concurrent mutators with
  200 full collections, checks a value reachable **only from a parked thread's
  C stack** survives, checks a rooted value, cancellation at a poll, and
  exclusion / blocking roundtrips; it passes repeatedly and under
  `NPL_GC_NURSERY_BYTES=65536 NPL_GC_VERIFY=100`. `runtime/thread_test.c`
  now uses `npl_gc_block_begin/end` for its spin, demonstrating the safe
  blocked-state transition.

**Compile-efficiency note:** the seed grew 341,849 → 350,624 lines (+2.6%)
for 1671 poll sites. Each adds one global load and one branch to the fast
path; no accessor call per entry. Self-emission cost is unchanged in kind
(one load per poll), and QBE does not lift the load out of loops.

**Remaining T1.3 work (deferred to T2 / follow-up)**

The items in this list are **resolved by T2 and the follow-up increment**
(2026-11-25); see "T1.3 remaining implementation status" below.

- Cross-thread invocation of a user `call/cc` continuation is implemented;
  see "Cross-thread continuation invocation" below.
- Cross-target execution evidence (only host macOS arm64 is executed; other
  targets have emission evidence). Linux execution and late `dlopen` are
  untested.

## T1.3 remaining implementation status (2026-11-25)

The remaining T1.3 items are implemented. `runtime/shared-lock.c` adds one
recursive shared-object lock with a **trylock + safepoint poll** acquisition
(a blocking `pthread_mutex_lock` waiter is not at a poll site and would
deadlock a world stop), a per-thread depth counter, and
`npl_shared_release_all`, called at every `longjmp` landing (`npl_trampoline`,
`npl_call_cc`, `npl_call_sync`, `npl_read_result`, `thread_entry`) so a
`npl_primitive_error` cannot leave the lock held. `NPL_SHARED_GUARD()` uses a
cleanup attribute for every normal return.

- Ports (`port.c`, `port-ready.c`), FFI blocks (`ffi-memory.c`), directory
  handles (`lib/npl/unix/native/unix.c`) and the symbol intern table
  (`symbol.c`) serialize through it; `npl_symbol_mark_roots` takes no lock
  because all mutators are parked during collection.
- Safepoint polls added to the long runtime loops in `read.c` (4 dispatch
  loops), `lexer.c`, `numeric.c` and `string.c`.
- Exact stack bounds at thread registration, runtime mutex/condition
  primitives, cancellation raise, the not-started → terminated transition,
  abandoned mutexes and the four join outcomes are covered by T2's substrate.
- `npl_gc_stop_world` now recomputes the expected parked count on every
  wake: a mutator that detaches while the collector waits no longer leaves
  the collector waiting for a thread that has left. Found by
  `runtime/shared_lock_test.c`, which runs three concurrent interning
  mutators with forced collections (8x and under `NPL_GC_VERIFY=100`).
- Blocking libc calls now use the safe blocked-state transition:
  `fread` / `fwrite` / `fclose` / `fflush` (`port.c`) and the two `waitpid`
  calls in `run-process` / `run-process*` (`io.c`) are bracketed by
  `npl_gc_block_begin` / `npl_gc_block_end`, so the thread publishes its
  stack/register snapshot and parks instead of stalling a world stop with
  no poll site. Error raises moved after `block_end` so a `longjmp` cannot
  leave the blocked state set. `runtime/blocking_io_test.c` blocks a reader
  on a pipe while the main thread forces 30 collections; an A/B check with
  the boundary removed hangs, and with it restored the test passes (5x and
  under `NPL_GC_VERIFY=100`), including a value reachable only from the
  parked thread's C stack.

## Cross-thread continuation invocation (2026-11-25)

SRFI 18 (`docs/reference/srfi-18.html`, "Dynamic environments, continuations
and dynamic-wind" and `make-thread`) defines invoking a continuation created
by another thread, including after the source thread ended: the saved dynamic
environment and dynamic-wind stack are reinstated on the current thread, each
before/after thunk runs in the environment in effect when its dynamic-wind
was executed, and the initial continuation / initial exception handler act
on the "(then) current thread".

Implementation:

- `thread_done` / `thread_uncaught` resolve the thread at call time from
  `npl_ctx()` via `npl_thread_current_object()`; both closures are
  thread-agnostic. `npl_thread_finish_current()` abandons the current
  thread's mutexes, marks it terminated, wakes joiners, and longjmps only to
  that thread's own valid jmp_buf. Initial handler frames are marked with
  `NPL_FLAG_INITIAL_HANDLER`; `npl_raise` resolves a captured chain's bottom
  frame against the invoking thread's own `initial_handler`, so a dead
  jmp_buf is never used.
- `call/cc` captures a heap `BOX_DYNAMIC_CONTEXT` (wind, handler, parameters,
  source initial handler) via `npl_capture_dynamic_context`. The transfer call
  keeps its 4-argument shape; the runtime dispatches on the first argument's
  tag, so a pre-relay seed and npli keep working.
- `NplWindFrame` records the handler and parameter chain in effect at push
  time, plus the extent's `frames` vector as `extent_key`.
  `schedule_wind_transition` installs that environment before every
  before/after thunk and the target context at completion.
- `npl_parameters_enter/leave` no longer write the shared `frames` vector.
  A per-thread extent mapping (`parameter_extents`) maps each extent to a
  thread-owned binding chain plus its baseline. Re-entry reuses the same
  nodes (so in-place `parameter-set!` mutations survive, as required by
  `src/compiler/tests/parameters.npl`); a cross-thread first entry clones the
  source chain under the invoking thread's ownership, preserving the existing
  owner/COW rules.
- User handler frames are kept by identity (`npl_handler_pop` validates
  `current_handler == frame`); only the initial frame is resolved dynamically.

`runtime/dynamic-context.c` owns the captured context, extent mapping and
transfer (extracted from `control.c`, which stays under the 800-line soft
limit). `runtime/gc.c` visits the new `NplVal` fields; `runtime/thread.c`
registers the three new per-thread roots. Frontend changes are one primitive
row, one internal name, and one changed let RHS in `control-cps-call-cc`;
`bootstrap/current.ssa` was re-seeded with `tools/update_current_ssa.sh`.

Acceptance: `tests/unit/cross-thread-terminal.npl` (initial continuation and
uncaught handler terminate the invoking thread) and
`tests/unit/cross-thread-continuation.npl` (nested parameterize/dynamic-wind/
handler observations, correct parameter values, restored handler by identity,
`current-thread` is the invoker, uncaught path does not touch the source
thread). Single-thread semantics stay in the compiler tests and evaluator
fixtures; cross-thread scenarios are native-only because the differential
fixture runner compares nplc and npli output.

## T2 SRFI 18

- `(npl thread)` / `(srfi 18)` exports `thread?`, `make-thread`,
  `thread-start!`, `thread-join!`, `thread-yield!`, `thread-sleep!`,
  `thread-terminate!`, mutexes, condition variables, and
  `current-thread`.
- Threads created by `thread-start!` are 1:1 with pthreads in T2. The
  scheduler interface is the only place that knows this.
- `thread-specific` and `thread-specific-set!` access one arbitrary NPL
  value on the thread object, initially unspecified.
- Follow the [SRFI 18 dynamic environment and termination contract](../../docs/reference/srfi-18.html).
  New threads inherit the creator's dynamic environment with a fresh initial
  exception handler and an empty initial wind stack. User continuations may
  be invoked by another thread (including after the source thread ended): the
  saved dynamic environment is reinstated on the invoking thread, each
  dynamic-wind thunk runs in its own extent's environment, and reaching the
  initial continuation / initial exception handler terminates the invoking
  thread. Implemented in `runtime/dynamic-context.c`; see the section above.
- Blocking primitives (`thread-sleep!`, mutex/condition waits) park the
  native thread through the T1 safe blocked-state transition; GC continues
  while it is parked only after its saved context and roots are published.

### T2 implementation status (2026-11-25)

`(npl thread)` (`lib/npl/thread.sld`) and the re-export-only alias
`(srfi 18)` (`lib/srfi/18.sld`) are implemented, with `srfi-18` in
`cond-expand` / `(features)` and the [user guide](../../docs/thread.md).
Runtime substrate: `runtime/thread-sched.{c,h}` and
`runtime/thread-lifecycle.c`.

**Implemented**

- All 38 SRFI 18 final exports (no `condition-variable-wait!`, no SRFI-21
  priority/quantum procedures, no three-argument `make-thread`). The four
  exception kinds are opaque `BOX_THREAD_EXCEPTION` objects;
  `uncaught-exception-reason` is the only accessor.
- 1:1 pthread creation with an exact C-stack bound, a startup GC-root
  handshake, fresh per-thread parameter bindings (copy-on-write), a fresh
  initial exception handler, an empty wind stack, and one termination path
  for normal return, uncaught raise and cooperative cancellation.
  Owned mutexes are abandoned; the terminated state is published before
  `thread-terminate!` returns, including the not-started -> terminated
  transition that has no poll site.
- Four join outcomes: normal result, `timeout-val`, join-timeout-exception,
  and re-raised end-exception (uncaught or terminated). Timeouts accept a
  `time` object, a real number, or `#f` (rejected by `thread-sleep!`).
- Mutex four-state model (non-recursive, abandoned-mutex-exception),
  `mutex-unlock!` with a condition variable (atomic unlock + wait, no
  auto-relock), `signal!` / `broadcast!`. All waits park through
  `npl_gc_block_begin/end`.
- A cancellation also raises the exported world-stop flag via a pending
  counter, so generated safepoint polls reach `npl_safepoint_poll` and
  observe cooperative termination. The counter is consumed exactly once by
  the target thread.
- Acceptance: `runtime/thread_object_test.c`,
  `runtime/thread_lifecycle_test.c` (both in `check_full_compiler.sh`),
  `src/compiler/tests/thread.npl` (26 assertions), and the
  `tests/fixtures/thread` nplc/npli differential fixture.

**Remaining evidence**

- Linux execution and late `dlopen` evidence (host is macOS arm64).
- `with-exception-handler` / `raise` are re-exported R7RS control
  operators, not first-class procedures in NPL.

Port, FFI block, directory handle and symbol-table synchronization, the
remaining long-loop safepoint polls, blocking libc I/O GC boundaries and
cross-thread user continuations are implemented.

## T2b: Virtual Threads (JVM-Style)

Append this after T2, before T3. NPL already has full multi-shot
continuations, so virtual threads can reuse the CPS machinery, but the
public virtual-thread scheduler must use a **restricted, one-shot** view.

### NPL vs JVM continuations

| Property | NPL `call/cc` | JVM `Continuation` |
|---|---|---|
| Representation | heap CPS closure from control conversion | copied stack frames |
| Extent | unlimited; can resume after return | only while the continuation is mounted/yielded |
| Shots | multi-shot; the same continuation can be invoked again | one-shot; resuming after completion is illegal |
| Delimitation | undelimited | delimited by the virtual-thread mount |
| Exposed as | first-class Scheme procedure | internal JVM object, not a Scheme value |
| Dynamic state | wind/handler/parameter chains captured by the runtime | carrier/thread state managed by the scheduler |

They are not the same primitive. NPL's is stronger (multi-shot), while the
JVM's is a stackful, one-shot suspension token. A virtual-thread scheduler
therefore adds a **scheduler continuation**:

- one-shot: a second resume raises an error;
- scoped to one virtual thread, never usable from another thread;
- not exported as `call/cc`;
- captures the virtual thread's dynamic-wind/handler/parameter chains, not
  the carrier's;
- resumes only at a scheduler safepoint.

A one-shot wrapper alone does not establish a scheduler boundary. T2b must
define and test a private CPS root continuation that returns to the carrier
loop and excludes carrier frames; this does not require implementing the
general SRFI 226 prompt API first. Before exposing virtual
threads, define whether user `call/cc` may escape the virtual thread's root,
how a saved continuation behaves after its thread completes, and how dynamic
state is restored when it is invoked. A scheduler token must never resume on
a different logical thread. This restriction does not apply automatically to
user continuations: SRFI 18 permits cross-thread invocation. Any restriction
in the separate virtual-thread API must be explicit and must not silently
change native SRFI 18 semantics. Do not claim the current CPS machinery
already provides these guarantees.

### Scheduler

- M carrier pthreads run an N-way run queue of virtual threads.
- A virtual thread may suspend only at a CPS boundary where no C or direct
  entry frame must remain active. `virtual-thread-yield!` transfers its
  one-shot scheduler continuation to the run queue and returns to the carrier
  loop; another carrier can mount it later.
- `thread-sleep!`, mutex waits, condition waits, and wrapped blocking I/O
  yield the virtual thread instead of blocking the carrier. Since current
  primitives and FFI calls are synchronous, a blocking wrapper must use a
  nonblocking/async completion path or a helper worker; it cannot suspend a
  carrier while retaining a C frame.
- The first validation runs multiple virtual threads on one carrier before
  adding work stealing. One virtual thread per carrier does not validate
  multiplexing.
- Native SRFI 18 threads remain 1:1 and can coexist with virtual threads.

### API

A separate library, for example `(npl thread virtual)`, exports
`virtual-thread?`, `make-virtual-thread`, `virtual-thread-start!`,
`virtual-thread-yield!`, `virtual-thread-join!`, and
`current-virtual-thread`. Do not change the SRFI 18 exports.

### GC

Suspended scheduler continuations and their dynamic state are heap roots owned
by their virtual-thread records. The initial CPS design retains no suspended
C stack: disallow suspension across synchronous native frames. If a later
substrate preserves or copies C stack chunks, register each chunk and its
saved register context with the conservative collector. Precise stack maps
are a separate optimization or a requirement of a future moving collector,
not a prerequisite for scanning the current non-moving heap.

### T2b implementation status (2026-11-25): increment A — substrate

`runtime/virtual-thread.c` adds the scheduler substrate behind the C API in
`runtime/thread-sched.h`; no public library or frontend change yet, so no
seed relay was needed.

- `BOX_VIRTUAL_THREAD` (tag 33) holds the thunk, a private CPS root, the
  one-shot suspended continuation, the saved dynamic environment (wind,
  handler, parameters, `parameter_extents`, `pending_extent_key`,
  `initial_handler`, wind id) and the end result/exception. `gc.c` visits
  every `NplVal` field; all virtual-thread records are marked from
  `npl_virtual_thread_mark_roots()` (called from the collector's root pass),
  so a suspended token and its dynamic state stay alive across collections.
- A carrier loop mounts one virtual thread at a time. `npl_vthread_park`
  stores the **raw CPS continuation** (`argv[argc-1]`) and returns *without*
  a tail request, so the trampoline breaks and the carrier regains control;
  no C stack frame is retained for a suspended thread. The token is cleared
  on mount (one-shot). `npl_gc_block_begin/end` parks an **idle** carrier,
  never a suspended virtual thread.
- The private root `vthread_done` and the virtual thread's own initial
  uncaught handler are C closures bound to the record. They record the
  end result/exception and break the trampoline without terminating the
  carrier, so a user `call/cc` continuation ends at the virtual thread's own
  boundary instead of escaping into the carrier loop. `logical_thread` (the
  T2b placeholder) now points at the mounted record.
- **Why the eventual `virtual-thread-yield!` cannot be a library `call/cc`:**
  a `define-c-primitive` C function never receives a continuation (primitive
  calls are synchronous; generated code applies the continuation itself), and
  a `call/cc`-based wrapper would run `dynamic-wind` before/after thunks on
  resume, which a scheduler yield must not do. Increment B therefore adds a
  control-transfer form (`_npl_vthread_park`) that emits
  `call $npl_vthread_park(l K)` + `ret 2`; that is a `src/compiler/**` change
  and needs a relay.
- First validation is **one carrier, N virtual threads**, as required before
  work stealing. `runtime/virtual_thread_test.c` runs three virtual threads
  that each yield three times, checks round-robin interleaving,
  per-virtual-thread parameter isolation across suspension, one-shot
  park/resume counts, and forces full collections both from inside a step
  and from a **concurrent collector pthread** (also under
  `NPL_GC_VERIFY=100` with a 64 KiB nursery) while other tokens have no live
  C frame. An A/B that removes the `gc.c` `BOX_VIRTUAL_THREAD` case fails
  with `parameter: invalid binding chain`, confirming the child traversal is
  essential. Blocking `pthread_join` waits in the harness and in shutdown
  use `npl_gc_exclude/include` (the `safepoint_test.c` pattern) so a world
  stop cannot wait forever on a thread with no poll site. M carriers and
  cross-carrier migration need parameter-ownership re-localization and are
  increment 3; `npl_virtual_thread_set_carriers` exists so the substrate can
  grow.

### Increment B — public API

`lib/npl/thread/virtual.sld` exports `virtual-thread?`,
`make-virtual-thread`, `virtual-thread-start!`, `virtual-thread-yield!`,
`virtual-thread-join!`, `current-virtual-thread`. Optional arguments stay NPL
policy (fixed-arity `define-c-primitive` wrappers), consistent with
`(npl thread)`.

`virtual-thread-yield!` is a **control-transfer operator**, not a primitive:
`control.npl` lowers an application to `(_npl_vthread_park <current-k>)`, and
`tail.npl` emits `call $npl_vthread_park(l K)` followed by `ret 2` with no
tail request. This is why the yield cannot be a library `call/cc` (see
increment A). `PRIMITIVE-CONTROL-SOURCE` supplies its value binding via
`(only (npl internal primitive) virtual-thread-yield!)`. The
`src/compiler/**` change was re-seeded with `tools/update_current_ssa.sh`
(351,300 lines, SHA `0fa6d339…`), fixed point PASS; the frozen bootstrap
chain is untouched.

`virtual-threads` is exposed as a runtime feature (`npl_platform_feature`,
`(features)`) with no compiler-side name hardcoding, so `cond-expand`
accepts it through the existing bootstrap hook. Acceptance is
`tests/unit/virtual-threads.npl` — **native-only**, because the differential
fixture runner requires identical nplc/npli output and there is no evaluator
surface for a carrier scheduler. It asserts all six exports (so the coverage
ratchet needs no baseline entry), round-robin interleaving of three virtual
threads on one carrier, `current-virtual-thread` inside a virtual thread,
and a first-class `virtual-thread-yield!` reference.

### Increment C — M carriers and cross-carrier migration

The single shared run queue became **one queue per carrier**. A newly started
virtual thread is assigned to a queue round-robin; every `npl_vthread_park`
hands it to the *next* carrier's queue, so with M > 1 each yield is a
deterministic cross-carrier migration (M = 1 preserves the original
round-robin validation). The default is 2 carriers;
`npl_virtual_thread_set_carriers` overrides it before the first enqueue, and
`npl_vthread_migration_count` exposes the observed migrations.

On a cross-carrier mount, `restore_state` calls
`npl_relocalize_parameter_bindings` (in `dynamic-context.c`) to re-stamp the
saved binding chain's `owner` for the new carrier. The chain is private to
the virtual thread (cloned at its first mount) and the saved
`parameter_extents` mapping points into the same chain, so one walk covers
both. Without it, a post-migration `parameter-set!` would COW-clone a node
instead of mutating in place, growing the chain on every step and changing
continuation re-entry behaviour. The C harness asserts the chain stays
length 1; removing the re-localization produces 9 failures on 4 carriers.

`runtime/virtual_thread_test.c` now takes the carrier count as `argv[1]` and
is run with **1 and 4** carriers from `check_full_compiler.sh`, each with a
concurrent collector thread and a 64 KiB nursery. It asserts the exact
migration count (0 for one carrier, one per resume for M > 1), one-shot
park/resume counts, per-virtual-thread parameter isolation, and GC survival
of suspended tokens. The NPL unit test was made race-free (workers return
their own observations; no shared mutable log or global ordering assertion)
so M carriers cannot make it flaky.

### Increment D — yielding blocking primitives (async completion)

A synchronous C primitive cannot suspend a virtual thread (it never receives
a continuation) and must not block a carrier while retaining a C frame.
T2b therefore uses a **helper-worker pool**: `npl_vthread_await(work,
continuation)` stores the raw CPS continuation, saves the dynamic state and
submits the work thunk to a helper queue instead of a carrier queue. A helper
pthread runs `npl_apply(work, 1, [helper_done])` on its own trampoline/C
stack; `helper_done` stores the result and re-enqueues the virtual thread to
the next carrier queue, where it resumes with that result. No carrier is
blocked, and the suspended virtual thread retains no C frame.

`virtual-thread-block!` is a control-transfer form alongside
`virtual-thread-yield!`: `control.npl` lowers it to
`(_npl_vthread_await <thunk> <current-k>)` and `tail.npl` emits
`call $npl_vthread_await(l work, l K); ret 2` with no tail request.
Outside a virtual thread the runtime runs the thunk synchronously.
`(npl thread virtual)` re-exports the operator (via `(npl internal
primitive)`) and adds `virtual-thread-sleep!`, which wraps the SRFI 18
`thread-sleep!` in a thunk. Mutex/condition waits and blocking I/O can be
wrapped the same way. Native SRFI 18 exports are deliberately untouched: a
change to `(npl thread)` would be visible to npli and break the
differential fixture, which has no carrier scheduler surface.

Work thunks run on helpers with their own dynamic state, so a thunk should
receive what it needs as arguments rather than rely on the virtual thread's
parameters; SRFI 18 mutex ownership inside a helper is a known limitation.
The helper's stack is published via `npl_thread_attach_exact` plus
`npl_gc_swap_stack_bottom`; idle helpers park with
`npl_gc_block_begin/end`, and the completion closure is explicitly rooted
for the whole trampoline run.

Acceptance: `runtime/virtual_thread_test.c` gained an async phase run under
1 and 4 carriers with a concurrent collector and a 64 KiB nursery. It
asserts that the work thunk ran with no virtual thread mounted (i.e. on a
helper), that the result was delivered, and the exact park/resume/migration
counts. `tests/unit/virtual-threads.npl` asserts `virtual-thread-block!`
and `virtual-thread-sleep!`, with `await` intrinsics visible in the emitted
SSA. The `src/compiler/**` change was re-seeded (352,219 lines, SHA
`1674d349…`), fixed point PASS.

Work stealing between the per-carrier queues and a CPU-derived carrier count
remain later tuning.

## PR #8 review fixes (2026-11-25)

The CodeRabbit and Gemini reviews on PR #8 were triaged against the code; all
confirmed functional defects were fixed, with regression tests. The branch
was also **rebased onto `origin/master`** (`b6f52fa`, PR #7) and the PR #7
deletions (`.agents/workstate.md`, the plan/skills files) were dropped;
`docs/thread.md` is linked from `docs/README.md`.

Confirmed and fixed (highest impact first):

- **Clock mismatch**: deadlines were `CLOCK_MONOTONIC` but `npl_sched_cond`
  used the default `CLOCK_REALTIME`, so every timed `thread-sleep!` /
  `thread-join!` / `mutex-lock!` returned immediately. `wait_loop` now
  converts the remaining monotonic delay to a realtime deadline, rechecks the
  monotonic deadline on `ETIMEDOUT`, and negative/NaN relative timeouts are
  normalized (`runtime/thread-lifecycle.c`, `runtime/thread-sched.c`).
  Regression: `tests/unit/thread-timeouts.npl` measures elapsed time.
- **Reversed parameter inheritance**: `clone_parameter_chain` prepended while
  walking head→tail, so a child thread saw the OUTER binding of nested
  `parameterize`. It now uses the order-preserving
  `npl_clone_parameter_bindings` (also made iterative). Regression included
  in `thread-timeouts.npl`.
- **Double-clone on cross-thread transfer**: the destination chain and the
  wind frames were cloned separately, so a `parameter-set!` in a before thunk
  was lost. `NplWindTransition` now carries a `(src . copy)` cache; shared
  bindings resolve to one localized node. Regression:
  `tests/unit/cross-thread-wind-parameters.npl` (A/B confirmed it fails
  without the cache).
- **STW deadlock under `npl_sched_lock`**: allocations (`list_cons`,
  `list_remove`, `npl_thread_exception`) and missing write barriers happened
  while holding the scheduler lock. `npl_sched_lock_enter()` is now
  trylock + safepoint poll; waiter/owned lists use pre-allocated cells and
  allocation-free `list_link`/`list_unlink` with write barriers; exceptions
  are allocated outside the lock.
- **SRFI 18 edge cases**: mutex waiters retired on timeout and cancellation,
  foreign `mutex-unlock!` removes the mutex from the ACTUAL owner's list (and
  abandonment verifies the current owner), `condition-variable-signal!`
  dequeues the woken waiter, `mutex-state` snapshots under the lock,
  `thread-terminate!` on the primordial thread ends the process instead of
  hanging, pre-registration cancellation is replayed after `setjmp`,
  `thread_entry` detaches its pthread, `make-thread` validates its thunk,
  explicit `#f` names are preserved, and exact rationals are valid timeouts.
- **Cancellation bookkeeping**: `cancel_owed` replaces the zero-initialized
  `cancel_consumed` so an uncaught raise in a fresh thread cannot consume
  another thread's pending sweep; detach releases the contribution; attach
  waits for an active world stop before admitting a mutator.
- **Virtual threads**: `g_all` no longer retains terminated records (pruned
  in `finish_vt` under `g_lock` — an earlier mark-time compaction proved
  racy and was replaced); `npl_vthread_start` claims `NEW` under the lock and
  starts carriers immediately (the C harness uses an explicit defer flag);
  `virtual-thread-join!` from inside a virtual thread parks via
  `virtual-thread-block!` so it cannot block a carrier; join re-raises the
  target's uncaught exception; helper workers install an initial uncaught
  handler and the pool grows when all helpers are busy; the arbitrary
  `+4096` helper stack bound was removed. Regression:
  `tests/unit/virtual-thread-edge.npl`.
- **GC / reader / ports**: `npl_byte_ready_p` and the port entry points now
  validate under `NPL_SHARED_GUARD()`; `npl_utf8_to_string` roots the source
  bytevector and registers a longjmp-safe cleanup; reader and numeric scratch
  buffers are freed on cancellation via a new `npl_cleanup_push/pop` stack
  released at every longjmp landing; parameter-owner identity is the
  monotonic `NplThreadContext.id` (no ABA after a context is freed);
  `npl_raise` resolves an empty captured handler chain to the invoking
  worker's initial handler; allocator trigger counters use relaxed atomics
  and one-time init publishes `g_runtime_initialized` only when complete
  (other threads wait in `pthread_once`).

Deferred with reasons (documented in the PR response): a global shared lock
still held across blocking `fread`/`fwrite` (needs per-port lifetime
management), `parameter_extents` weak pruning (needs GC integration to keep
continuation re-entry state), the narrow `src->value` snapshot race (needs a
parameter-write synchronization protocol), platform stack-window sizing for
native workers, `npl_gc_stop_requested` atomics (C99 + frozen QBE SSA
constraint), and Linux/late-`dlopen` execution evidence.

## PR #8 review round 2 (2026-11-25)

The second Gemini review was verified STALE (`head_sha=7ab3205`, the pre-rebase
head); nearly all of its items restate round-1 findings already handled above.
The second CodeRabbit review (13:44) was against `da0fdf3` and found real
issues, including regressions introduced by round 1. All valid items fixed:

- **Coordinator / GC**: `npl_safepoint_poll` now skips parking and snapshot
  state when `context->blocked` (the new trylock poll had let a blocked waiter
  satisfy the stop quorum and then lose its published stack); a `blocked_counted`
  flag enforces `g_blocked_mutators ⊆ g_active_mutators` so an excluded mutator
  cannot reduce the expected parked count twice; `cancellation_requested` is
  exchanged atomically with its writer; an `armed` flag prevents forwarding a
  cancellation before `thread_entry`'s `setjmp` (a poll in that window could
  longjmp through a zeroed `jmp_buf`); skipped trampoline argument buffers are
  freed on abnormal worker exit; the remembered set re-checks
  `NPL_FLAG_REMEMBERED` under the heap lock.
- **Reader recovery**: `npl_read_result` now saves shared-lock/cleanup depth
  on entry and releases only to that depth on recovery, so a caught read error
  cannot free an outer caller's scratch buffer or drop its shared lock.
- **Handlers**: the primordial thread now has an initial exception handler
  (`primordial_uncaught`), so `current-exception-handler` returns a procedure
  on every thread and a foreign continuation's `INITIAL_HANDLER` frame is
  resolved against the invoker's own handler rather than running the source
  virtual thread's `finish_vt`.
- **Parameter ownership**: binding owner is now the LOGICAL virtual thread
  (`CTX->logical_thread`) rather than the carrier's context id, restoring COW
  isolation between virtual threads sharing one carrier; the two malloc'd
  `nodes` arrays are registered with the cleanup stack; `make-thread` snapshots
  the parent's bindings at creation time (SRFI 18 inheritance point).
- **SRFI 18 / lifecycle**: a terminated explicit `mutex-lock!` owner on a
  not-abandoned mutex returns `#t` (SRFI 18 raises only if it was already
  abandoned); `make-thread` accepts parameter-object thunks and boots them
  through a real trampoline; wait descriptors are published only during an
  active wait and cleared afterwards; thread finalization resolves a pending
  cancellation under the scheduler lock; an independent process-wide root
  (`runtime/thread-start-root.c`) covers the pthread-start handshake.
- **Virtual threads**: `registry_add` moved from creation to start so
  unstarted discarded records are collectable; helper-pool growth compares
  unclaimed queued work against available helpers (a broadcast does not
  decrement the idle count until helpers resume).
- **Tests / docs**: `tests/unit/review-fixes.npl` covers the observable items;
  `virtual-thread-edge.npl` adds a grandchild so both carriers are occupied by
  joiners; `safepoint_test.c` publishes its stack bound before the first
  allocation; `virtual_thread_test.c` drops the `+4096` slack;
  `thread_object_test.c` stores heap objects to exercise GC field tracing; the
  condition-variable example in `docs/thread.md` re-locks after each wake.

Skipped with reasons (verified against current code and SRFI 18): repeated
foreign re-entry correctly REINSTATES the saved environment (a previous
invocation's mutation is not part of the capture — SRFI 18 6.4), the legacy
4-argument `npl_wind_transfer` shim is deliberate ABI tolerance, the scoped
one-argument parameter update is implementation-dependent (writing a shared
default would break thread isolation), and `npl_gc_stop_requested` atomicity
plus the `npl_gc_block_begin` register-snapshot refactor remain deferred.

`runtime/thread-start-root.c` was extracted to keep `thread-lifecycle.c` under
the 800-line soft limit. No frontend change, so the seed is unchanged.

## T3 SRFI 230

### Contract and public boundary

Provide `(npl atomic)` with a re-export-only `(srfi 230)` alias. Keep optional
arguments, order validation, and syntax in NPL; C supplies indivisible memory
operations and their GC integration. Neither library depends on a native or
virtual thread representation. The complete surface is:

| Family | Exports / semantics |
|---|---|
| Orders | `memory-order` syntax, `memory-order?`; symbols `relaxed`, `acquire`, `release`, `acquire-release`, `sequentially-consistent` |
| Flag | `make-atomic-flag`, `atomic-flag?`, `atomic-flag-test-and-set!`, `atomic-flag-clear!`; initially clear; test-and-set returns the previous state as a boolean |
| Box | `make-atomic-box`, `atomic-box?`, `atomic-box-ref`, `atomic-box-set!`, `atomic-box-swap!`, `atomic-box-compare-and-swap!` |
| Fixnum box | Corresponding `make-atomic-fxbox`, `atomic-fxbox?`, ref/set/swap/CAS, plus `atomic-fxbox+/fetch!`, `atomic-fxbox-/fetch!`, `atomic-fxbox-and/fetch!`, `atomic-fxbox-ior/fetch!`, `atomic-fxbox-xor/fetch!` |
| Pair | `make-atomic-pair`, `atomic-pair?`, `atomic-pair-ref`, `atomic-pair-set!`, `atomic-pair-swap!`, `atomic-pair-compare-and-swap!` |
| Fence | `atomic-fence` with a required order argument |

All optional orders default to sequential consistency. `memory-order` checks
the **name** of its identifier, independently of lexical binding, and expands
to the corresponding symbol; invalid names are syntax violations. Reuse the
frontend's syntax validation facilities, not binding-sensitive literal
matching alone. `memory-order?` also recognizes those ordinary symbols.

CAS returns the observed **old contents**, on both success and failure, not
a success boolean. Box CAS uses `eq?`; fxbox CAS uses fixnum numeric equality;
pair CAS compares both expected fields using `eq?` and returns two old values.
Pair ref and swap also return two values from one indivisible snapshot.
Despite their spelling, all five `/fetch!` operations return the **previous**
fixnum. There are no atomic vectors, weak/spurious CAS, two-order public CAS,
or lock-free progress guarantees in this SRFI.

Use the existing fixnum range and tagged representation. Arithmetic must not
invoke signed C overflow or silently change the box into a bignum. T3-A must
record NPL's out-of-range policy explicitly (proposed: reject before changing
the box, using checked arithmetic); do not describe wrapping as required by
SRFI 230, which does not specify it. Test both range boundaries.

### Representation, ordering, and GC protocol

Start with distinct opaque flag, box, fxbox, and pair objects. Add their
child visitors to `runtime/gc.c`; a box has one managed edge, a pair has two,
and flags/fxboxes have none. Reuse the existing `values` primitive ABI for
pair results. Root both observed fields before any result-packet allocation.

**T3-A/B baseline:** serialize all public atomic operations, including
fences, through one dedicated atomic-domain mutex. This deliberately
strengthens weaker orders and gives one total order across flags, boxes,
fxboxes, and pairs. It is a correctness/reference implementation, not a
claim of lock-free operation. Do not reuse the global port lock: a blocked
pipe must not stop an unrelated atomic counter. Extract the reusable
trylock/poll acquisition mechanism from existing synchronization code if
needed; keep the lock domain and its lifetime separate.

The short critical section must not allocate managed objects, run Scheme
callbacks, poll, or raise. Validate and allocate/root result storage before
entry, or snapshot into rooted locals and allocate after exit. A waiter polls
only while it holds no atomic lock. This preserves STW progress and makes
cancellation occur outside the indivisible operation. Avoid a new general
longjmp cleanup scheme when this no-unwind critical-section rule suffices.
Arithmetic range failure is recorded inside the lock and raised after release.

For pointer writes, the indivisible update and remembered-set publication
must finish before the next safepoint or safe-blocked transition. Audit
`npl_gc_write_barrier`: its unlocked `header.flags` read cannot simply be
copied into a new multi-writer atomic path. Extract a heap-locked remember
helper shared with `alloc.c`, with flag checks and insertion under the heap
mutex. Establish order **atomic-domain lock → heap lock**; no reverse path,
collector acquisition of the atomic lock, or polling while holding either.
The collector scans atomic children only after all mutators have parked.
Root receiver, desired values, and observed return values throughout; a failed
CAS still has to return a live observed object. Conservatively remembering
an old pointer-bearing atomic object is acceptable for the first increment.

The initial implementation accepts all five recognized orders and may use
stronger ordering for every operation. Do not pass arbitrary symbols or
invalid load/store/failure combinations directly to a compiler intrinsic.
For a later specialized C99 implementation, use the GCC/Clang `__atomic_*`
boundary already used in `coordinator.c` and `alloc.c`, with feature probes:

| Requested order | Load | Store / flag clear | RMW success | CAS failure |
|---|---|---|---|---|
| relaxed | relaxed | relaxed | relaxed | relaxed |
| acquire | acquire | seq-cst strengthening | acquire | acquire |
| release | seq-cst strengthening | release | release | relaxed |
| acquire-release | seq-cst strengthening | seq-cst strengthening | acq-rel | acquire |
| sequentially-consistent | seq-cst | seq-cst | seq-cst | seq-cst |

This is an NPL mapping policy for combinations without a direct C operation,
not an extra SRFI restriction. Fences require a compiler/CPU ordering
boundary as well as an ordering proof for the locked fallback. A fence alone
does not make unsynchronized ordinary mutation safe.

Do not independently replace scalar operations with hardware atomics while
leaving pairs on an unrelated mutex and assume sequential consistency across
both domains. T3-D needs a written mixed-domain ordering argument and litmus
tests; retaining a common seq-cst serialization domain is a valid first
optimization. A pair always uses one lock or one proven double-width atomic
operation, never two independent word stores. Double-width CAS and `libatomic`
are optional target capabilities, not new minimum platform requirements.
Pointer atomics may still take the GC heap lock; hardware CAS alone does not
make the whole Scheme operation lock-free. ABA is not solved by GC: the
SRFI's pair can carry a user version, but version wrap and logical reuse remain
application concerns.

### Increments and acceptance

| Increment | Deliverable | Required evidence |
|---|---|---|
| T3-A | NPL order syntax/defaults, flag/fxbox C substrate, dedicated lock and numeric policy | Every order, omitted/extra arguments, renamed imports, shadowed order names, wrong types, old-value results and range errors; common nplc/npli fixtures |
| T3-B | Arbitrary-value boxes and whole pairs, GC visitors and shared remember helper | Pair two-value arity; success/failure CAS; identity distinct from `equal?`; young values reachable only from an old atomic object survive minor/full GC |
| T3-C | Complete alias/export/feature surface and concurrency validation | Linearizability histories, publication/fence tests, concurrent collector stress, static/shared runtime identity, supported-host execution |
| T3-D | Measured scalar fast paths and optional pair specialization | Same fixtures/histories as locked baseline; ordering argument across all object kinds; contention and allocation benchmarks |

Proposed files: `lib/npl/atomic.sld`, `lib/srfi/230.sld`,
`runtime/atomic.c` (split pointer/pair mechanics if needed), and
`runtime/atomic_test.c`. Reuse `library-primitives.npl`, the primitive table,
`runtime/platform.c`, library export-origin checks, and the fixture manifest.
Do not advertise `srfi-230` until T3-C; flag/fixnum-only support is partial.
No new backend atomic instruction is necessary for T3-A–C.

### T3-A/B/C implementation status (2026-11-26)

T3-A, T3-B and T3-C are implemented. T3-D has a first measured scalar fast
path; optional pair specialization and the full B6/B7 benchmark matrix remain
ahead.

- `lib/npl/atomic.sld` and the re-export-only `lib/srfi/230.sld` expose the
  complete SRFI 230 surface. `memory-order` is a procedural library macro: it
  inspects the identifier's name through `syntax->datum`, so a shadowed or
  renamed `acquire` still selects acquire, and invalid names fail at compile
  time. Optional orders and arity checks stay in NPL wrappers; C receives a
  validated order code.
- `runtime/atomic.c` adds `BOX_ATOMIC_FLAG`, `BOX_ATOMIC_BOX`,
  `BOX_ATOMIC_FXBOX` and `BOX_ATOMIC_PAIR`. One dedicated atomic-domain mutex
  serializes every operation, including fences. Waiters use trylock plus a
  safepoint poll; the critical section never allocates, calls Scheme, polls or
  raises. Type/order validation precedes the lock and arithmetic overflow is
  raised after release, so an unwind cannot leave the domain locked.
- Fixnum arithmetic is checked in the 62-bit fixnum range and rejects before
  changing the box. CAS returns the observed old value(s) in both outcomes;
  pair CAS/ref/swap use the existing `values` ABI. Pair results root both
  observed fields before allocating the result packet.
- `runtime/gc.c` visits a box's one edge and a pair's two edges.
  `runtime/alloc.c` extracts `remember_locked` from
  `npl_gc_remember_internal` and adds `npl_gc_write_barrier_atomic`, which
  checks generation flags under the heap lock. The lock order is
  atomic-domain -> heap; no path reverses it or polls while holding either.
- `srfi-230` is advertised by `(features)` / `cond-expand`; `docs/atomic.md`
  records the range policy and the deliberately locked ordering baseline.
- Acceptance: `runtime/atomic_test.c` (contended old-value permutation,
  release/acquire publication, pair snapshots, concurrent collector races,
  failed-CAS retention, old->young barriers; run normally and with
  `NPL_GC_NURSERY_BYTES=65536 NPL_GC_VERIFY=100`), `tests/unit/atomic.npl`
  (every export, order syntax, renamed/shadowed names, errors and both fixnum
  boundaries), `tests/unit/atomic-threads.npl` (native and virtual workers on
  M=1 and M=4 carriers), the `tests/fixtures/atomic` nplc/npli differential
  fixture, and `src/compiler/tests/srfi-230.npl`.
- Static/shared identity is exercised by the existing link-mode gate; the
  atomic code has no independent TLS or per-executable state. Host execution
  is macOS arm64; the repository CI Linux gate also passed for this revision.
- T3-D: scalar operations with `relaxed`, `acquire`, `release` and
  `acquire-release` orders use per-location `__atomic_*` operations with the
  design's load/store/RMW/CAS mapping. Sequentially consistent operations,
  pair operations and fences stay in the common locked domain, so the default
  order still has one total order across every object kind. Weaker orders are
  not promised that cross-object total order, which matches the C11/SRFI
  ordering contract. `NPL_ATOMIC_FORCE_LOCKED=1` restores the all-locked
  reference path. `runtime/atomic_bench.c` is a small measurement probe, not
  the repository benchmark harness; optional pair specialization and the full
  B6/B7 matrix remain ahead.
- `docs/atomic.md` records the mixed-domain argument and the measured
  fast-path boundary.

Concurrent tests use start gates and bounded joins, not sleep-based ordering:

- N native workers increment one fxbox; verify exact final count and the
  permutation of old values. Also test independent boxes to expose accidental
  global serialization in performance measurements.
- Publish immutable heap payloads with release/acquire; verify all initialized
  fields. Test fence-mediated publication and seq-cst store-buffering across
  different objects. Relaxed tests assert atomicity only, not visibility
  guarantees that relaxed ordering does not provide.
- Record small operation histories (invocation/response/value) and check for
  a sequential history preserving real-time order. Mixed box/pair histories
  and torn-pair sentinels catch bugs a final counter cannot.
- Race pointer swaps/CAS with minor/full GC under
  `NPL_GC_NURSERY_BYTES=65536 NPL_GC_VERIFY=100`; retain failed-CAS results
  after replacement. Include cancellation of a lock waiter and GC while
  other workers contend. Instrument critical windows in C harnesses.
- Repeat with native/virtual workers sharing the same atomic objects, M=1
  and M=4 carriers. Check normalized results, not scheduling order.

Litmus repetition can expose an ordering bug but is not a proof of absence;
review the C/compiler mapping and generated code on each supported target.

## T4 SRFI 226

### Scope, compatibility, and delivery policy

SRFI 226 is a control-system project, not just prompts and marks. Its R7RS
packaging uses `(srfi 226)` and component names in the singular where
applicable, per the specification's library erratum. Build implementation
libraries under `(npl control ...)` and re-export component aliases such as
`(srfi 226 prompt)`, `(srfi 226 continuation)`, and
`(srfi 226 continuation-mark)`. Verify the complete name/export inventory
against the reference before publishing each component.

| Component group | Required surface / semantic change | Existing boundary to reuse |
|---|---|---|
| prompt | Unique/default tags, prompt installation, abort, continuation conditions | CPS transfer, dynamic-context transition, native root completion |
| continuation / inspection | Non-composable and composable capture, `call-in-continuation`, `call-in`, `return-to`, barriers, availability, predicates, `dynamic-wind`, `unwind-protect` | Shared `control.npl`, trampoline and wind transition |
| shift-reset | `reset`, `shift`, `reset-at`, `shift-at` | NPL derived forms over prompts/composition |
| continuation-mark | Mark syntax, immediate lookup, snapshots, list/list*/iterator/first queries, opaque keys | Shared core tail-context information and GC-managed dynamic state |
| parameter / fluid | Ordinary and thread parameters, parameterizations, `temporarily`; fluid definitions, fluidified bindings, fluid `set!`, `fluid-let`/`fluid-let*` | Parameter objects, shared hygienic frontend and assignment resolution |
| call-in-initial-continuation / promise | Isolated initial continuation; zero/multiple values, stored exceptions, creation-time parameterization, atomic force bookkeeping | Root protocol, existing lazy library and T3 |
| exception / condition | Handler-stack access, continuable/non-continuable raises, guard; parameterization/mark/handler condition payloads | Existing handlers and condition/error representation |
| time / thread | `seconds+`, SRFI 226 thread outcomes, extensible thread type, scheduled termination, mutex/CV behavior | SRFI 18 scheduler, deadline conversion, start roots and lifecycle |
| thread-local / interrupt | Inheritable locals; interrupt level, masking syntax, queued `thread-interrupt!` | Logical-thread identity, safepoints and waiter wakeup mechanisms |

Do not alias all of SRFI 18 to SRFI 226. In particular, ordinary SRFI 226
parameters use shared cells in a parameterization; **thread parameters** use
inheritable thread-local cells. Current NPL parameters use thread-owned COW
bindings. The new API must distinguish these kinds while preserving existing
`(scheme base)` / `(srfi 18)` behavior. SRFI 226 promises also differ from
R7RS promises, and thread joins have multiple-value/continuable-condition
semantics rather than SRFI 18's `timeout-val` surface.

SRFI 226 timeouts are absolute time objects or `#f`, not SRFI 18's relative
real-number shorthand. `seconds+` constructs a shifted absolute time;
reuse the monotonic deadline mechanism without exporting an epoch conversion.
The thread/mutex/CV component also omits SRFI 18 name and specific fields.
Keep these argument/export differences in the NPL adapter and exercise them
with negative fixtures, instead of inheriting the wider SRFI 18 surface.

The SRFI permits adapting condition types on a system without R6RS conditions.
Write an explicit NPL condition mapping (constructors, predicates, accessors,
serious/non-serious behavior and shared identity across sublibraries). Do not
introduce a full R6RS subsystem just for spelling, or silently omit required
observable behavior. Audit the extensible `thread` record requirement against
NPL record inheritance before claiming the thread component complete. The
R7RS adaptation and any limitation must be documented and tested.

Partial releases expose completed components and their import availability.
The composite `(srfi 226)` and `srfi-226` feature wait for the entire inventory.
T4 control work can proceed after the correctness prerequisites without
waiting for T3-D; T4 promise/thread synchronization uses T3-C or shared runtime
mechanisms with equivalent guarantees.

### Shared continuation model

The compiler now emits a `BOX_CONTINUATION_SEGMENT` for each non-direct user
procedure call boundary while a prompt is active. A segment is a **local CPS
continuation plus a persistent semantic frame**: it holds the raw CPS closure,
the parent segment, a stable frame id, and the nearest prompt. This makes
non-tail call boundaries explicit without replacing the CPS closure, and the
same representation is used by compiled code and the evaluator's native
control operators.

1. A prompt frame records its tag, abort handler, outer continuation, parent
   frame, origin segment, and barrier flag. Nested prompts form an immutable
   heap chain captured by `BOX_DYNAMIC_CONTEXT`. The local CPS terminal returns
   through the installed prompt boundary; the per-thread one-shot override is
   the persistent meta-continuation used for composable invocation.
2. Capture records the local segment, prompt prefix, dynamic context, and
   delimiter. A callable wrapper carries an explicit kind for
   `continuation?` / `non-composable-continuation?`; ordinary closures and
   scheduler tokens do not satisfy these predicates.
3. Non-composable invocation removes the unshared current prefix up to the
   matching prompt or shared active call, then installs the saved prefix.
   Composable invocation appends a fresh activation of the captured prefix
   to the caller; its terminal returns to the caller. Captured successor links
   are not mutated during application.
4. Multi-shot applications share immutable segment/prompt descriptions but
   have independent activation/return chains. Parameter cells follow their
   specified sharing policy.
5. Barrier identity is prompt-frame identity in the captured and current
   prompt chains. Non-tail frame identity is available from segment parents;
   continuation marks still need a persistent mark table on those frames and
   an evaluator frame adapter for direct-style evaluated closures.

Implement segment/control policy and derived operators in focused NPL
modules, using existing records and closures where possible. C is limited to
GC roots, callable/runtime representation hooks, context mount/unmount and
trampoline/FFI boundaries. Reuse and extract `schedule_wind_transition` and
parameter localization, rather than writing a second wind interpreter. If
that machinery must gain segment-aware state, keep one transition engine
and test legacy `call/cc` through it. Any unavoidable compiler/evaluator
representation adapters share the same semantic operations and fixtures.

`src/compiler/frontend/control.npl` keeps the CPS expression walk at 800
lines; the segment/capture/graft lowering moved to
`src/compiler/frontend/control-capture.npl` (178 lines). Similarly,
`thread-lifecycle.c` is at 833 lines (one B0 line over the pre-existing 832,
left as a follow-up extraction) and `virtual-thread.c` is at 762 after the
saved-state/root split into `virtual-thread-root.c`; new interrupt/lifecycle
adapters and scheduler state belong in separate focused modules. Proposed NPL modules include control prompts, continuations, marks,
and parameters, plus a small frontend control-context module. These names
are a layout proposal, not permission to duplicate existing facilities.

The evaluator in `lib/scheme/internal/evaluator-compile.scm` uses the shared
frontend and native control operators. Preserve that architecture. For marks,
its evaluator helper calls must not appear as extra user continuation frames;
carry source core-form tail/frame metadata through its application adapter.
Test calls crossing native/evaluated procedures in both directions, including
capture on one side and invocation on the other.

### Prompt, transfer, barrier, and mark rules

- Install the default prompt in every Scheme initial continuation. A custom
  abort removes through the nearest matching prompt and calls its handler
  outside that removed prompt. The default handler accepts a thunk,
  reinstalls the prompt and invokes it. Preserve zero and multiple values.
- Missing delimiters raise a continuation violation with the correct tag.
  Non-composable application requires its delimiter at invocation; composable
  application must not acquire that same extra restriction. Capture requires
  the delimiter for both kinds.
- `call-in-continuation` transfers first and then calls its procedure in the
  resulting continuation. Evaluating that procedure before invoking `k` is
  incorrect for parameters, handlers, marks and winders.
- A barrier rejects a transfer that would introduce a protected frame;
  escaping outward remains possible. Track identity/common prefixes, not
  just a barrier-depth integer. Distinguish a language barrier from a runtime
  prohibition on retaining a synchronous native frame.
- Wind exits run inner-to-outer and entries outer-to-inner in their own
  environments. A before/after thunk may raise, capture, abort or change the
  available prompt; revalidate the remaining transfer after it returns.
  Preserve the PR #8 cross-thread initial-handler rebinding rule.
- Marks attach to **semantic continuation frames**. Tail calls share a frame;
  same-key replacement in that frame must not grow a list on every tail call.
  Non-tail calls create a distinct frame. A mark-set snapshot preserves its
  captured view across later writes and continuation reuse, bounded by the
  requested prompt. Use copy-on-write/persistent metadata, not mutation of a
  shared captured mark table.
- Immediate lookup only sees the immediate frame; list, list*, first, and
  iterator queries preserve specified nearest-first order and absent-value
  behavior. Test duplicate keys, multiple keys per frame, nested prompts,
  empty results, and keys whose printed names match but identities differ.

### Dynamic state, thread identity, and interrupts

Separate three categories explicitly in the shared context:

| State | Capture / ownership rule |
|---|---|
| Prompts, semantic frames/marks, winds, handlers, parameterization | Continuation state; segment transfer reinstates or composes it |
| Thread-local storage, pending interrupt queue, native/virtual identity | Logical-thread state; invoking a foreign continuation does not replace the invoking thread |
| C stack bounds, register snapshots, active `jmp_buf`, helper/carrier loop | Native substrate; never captured as a portable Scheme continuation |

A thread parameter selects the invoking logical thread's cell even when its
parameterization arrived in a foreign continuation. Inheritable thread locals
snapshot the creator's value at creation; non-inheritable ones start from their
initial value. Virtual-thread migration keeps the same identity. Ordinary
parameters retain cell sharing specified by SRFI 226; explicitly synchronized
shared mutation is necessary. Close the existing `src->value` snapshot race
before extending cross-thread parameter behavior.

Fluids must use shared binding/assignment expansion, including renamed imports,
lexical shadowing, and parallel versus sequential fluid binding. Reuse the
existing parameter converters and exception paths; do not implement a second
name-based variable lookup in the evaluator.

For promises, reuse `lib/scheme/lazy.sld`, the expansions in
`src/compiler/frontend/derived.npl`, and the promise machinery in
`runtime/control.c` where their semantics match; extract a common
value/exception completion cell. SRFI 226 captures
parameterization at `delay`, evaluates in an initial continuation, stores
zero/multiple values or an exception, and requires atomic force bookkeeping.
Do not hold a mutex while evaluating the body. Re-entry and concurrent forcing
must agree on the committed result and remain safe if the body escapes or is
terminated; an unconditional RUNNING wait can deadlock recursive forcing.
Use the reference's result arbitration semantics, not a stronger undocumented
promise that the body runs exactly once. Tail forcing must use bounded space.

For threads, reuse one scheduler/lifecycle state machine and translate public
arguments and outcomes in NPL. Add a result packet and condition mapping where
needed, preserving existing SRFI 18 behavior. A thin extensible NPL wrapper
may refer to the scheduler handle if necessary, but `current-thread`, mutex
ownership and condition predicates must agree on object identity. Decide and
test the identity bridge before exposing the extensible record. SRFI 226's
public thread component initially uses native threads; adapting virtual
threads to the same complete semantics is a separate acceptance milestone.

Interrupt delivery is cooperative Scheme control transfer, not a signal
handler calling NPL. Queue and root the thunk on the target logical thread;
safepoints request delivery, but dispatch occurs at a legal CPS boundary with
no runtime locks held. Splice a continuation that records delivered values,
calls the interrupt thunk, discards its results, then forwards the recorded
values. An arbitrary direct C poll must not call the thunk synchronously.
Masking uses the thread parameter `current-interrupt-level`; nested enable/
disable cannot make it negative. Masking interrupts does not disable STW.

A condition waiter can wake for an interrupt; maintain the existing unlock/
enqueue atomicity and do not automatically re-lock or silently resume waiting
after user code changed the condition. Keep cancellation, interrupt delivery,
and `thread-schedule-terminate!` as distinct requests. Tests must cover queued
interrupts during GC, nested masking, blocked waits, completed targets, and
interrupt thunks that raise or transfer control. Eventual delivery needs a
progress policy for long direct loops: add a CPS handoff/deoptimization point
where required, rather than claiming the current GC poll alone implements it.

The private T2b suspension token remains one-shot and logical-thread-bound.
Yield/await save and restore the added control state without invoking winders.
Helpers retain their documented independent dynamic state. Public user
continuations keep their multi-shot semantics; neither scheduler tokens nor
carrier frames become SRFI 226 continuations. Exercise invocation after the
source thread exits and across virtual/native boundaries before documenting
those combinations as supported.

### T4 increments and acceptance

| Increment | Deliverable | Exit tests |
|---|---|---|
| T4-A | Export/compatibility inventory, NPL control-module extraction, segmented continuation prototype and shared evaluator adapter | Existing call/cc/parameters/wind/exception fixtures unchanged; nested segment return and multi-shot composition prototype; native/evaluated interop |
| T4-B | Prompts, non-composable/composable capture, call-in, barriers, predicates, derived shift/reset and unwind-protect | Reference examples (including 990 vs 6930), repeated composition, default-handler reinstallation, missing tags, barrier escape/re-entry, zero/multiple values, re-entrant winders |
| T4-C | Semantic frame metadata and complete mark API | Tail mark replacement stays bounded over a long loop; non-tail accumulation, immediate lookup, captured snapshots, all query/iterator variants, compiler/evaluator parity |
| T4-D | Parameter kinds, parameterizations, fluids, thread locals | Converters and assignment hygiene; shared ordinary cells vs isolated thread cells; creation-time inheritance; cross-thread and migration cases; snapshot-race stress |
| T4-E | Initial continuations, promises, exceptions and condition payloads | Creation vs forcing environment, result/exception arbitration under concurrent/re-entrant force, tail-force space, handler-stack and condition identity, initial-continuation isolation |
| T4-F | Time/thread adapters, extensible-thread contract, scheduled termination and interrupts | Multiple-value joins, continuable thread conditions, inherited locals, masked/eventual delivery, interrupt wakeup of CV wait, GC/cancel/interrupt races |
| T4-G | Full R7RS library/export audit, composite alias/feature, documentation and target evidence | Common semantic fixtures plus native concurrency/virtual migration suites, static/shared linking, fixed point, full acceptance gates |

T4-C and T4-D can be developed independently after T4-B's representation is
stable; T4-E needs both; T4-F needs T4-D/E and the interrupt CPS handoff. Each
increment ports applicable executable examples from the reference into common
fixtures. Native-only stress supplements differential tests; it does not
replace them. Compare normalized observable results, not interleaving traces.

### T4-A/T4-B implementation status (2026-11-27)

The frontend now emits a compiler-visible `BOX_CONTINUATION_SEGMENT` at every
non-direct user procedure call while a prompt is active. A segment wraps the
local CPS continuation, records its semantic parent and nearest prompt, and
carries a stable frame id. `NplDynamicContext` captures the current segment;
the existing wind/context transfer restores it; `NplThreadContext` roots it;
virtual-thread save/restore carries it. Prompt frames record their origin
segment and a barrier flag.

- Prompt tags, a dynamic prompt stack, default/custom abort handlers,
  `call-with-continuation-prompt`, `abort-current-continuation`, and the
  `continuation-violation` record are implemented. The default handler
  reinstalls the prompt before invoking its thunk.
- Non-composable invocation cuts back to the nearest matching prompt and
  reinstates the captured prefix there; composable invocation appends the
  prefix and returns its values to the caller. The reference 990/6930
  examples, nested prompts, multi-shot `(k (k ...))`, multiple values and
  re-entrant winders pass. `continuation?`, `non-composable-continuation?`,
  `continuation-prompt-available?`, `unwind-protect`, `reset`, `reset-at`,
  `shift` and `shift-at` are implemented.
- `call-with-continuation-barrier` installs an inaccessible barrier prompt.
  Capture records the prompt prefix; non-composable invocation rejects a jump
  into a barrier frame absent from the invoker's chain, while outward escapes
  remain allowed. A composable capture whose prefix includes a barrier raises.
  Barrier identity is frame identity, not a depth counter.
- `call-in-continuation`, `call-in` and `return-to` use a control packet. The
  captured continuation performs its normal transfer, then installs this
  activation's prompt override in the target environment and either delivers
  values or calls the supplied procedure with the saved successor.
  Prompt-delimited non-composable call-in uses the cut-prompt override;
  composable call-in uses the invocation's return-k. Each invocation has an
  independent activation; captured descriptions stay immutable.
- T4-B0 composable composition: the packet also carries the invoking prompt
  chain. The transfer installs it as an activation-local splice on the
  delimiter frame, so the captured prefix keeps its own winds, handlers,
  parameters and inner prompts while prompts outside the capture remain
  visible for availability queries and abort routing. The splice is part of
  `NplDynamicContext`, removed by the prompt terminal, and dropped on a
  non-local exit with the abandoned activation.
- `prompt_overrides` is captured by `NplDynamicContext` and installed after
  the transfer. A non-local exit restores the target context and drops the
  abandoned activation's overrides, so an aborted composition leaves no stale
  return state.
- User-visible continuations are runtime `BOX_CONTINUATION` objects carrying
  kind, origin, tag, prompt prefix and graft entry, shared by compiled code
  and evaluated library instances. The per-library strong registry is gone.
- Program, native-thread and virtual-thread initial continuations install a
  runtime default prompt. Omitted-tag control captures are delimited there;
  full R7RS `call/cc` remains undelimited.
- T4-B0 root abort: the root prompt records the dynamic environment outside
  itself. An abort transfers there first (running wind after thunks and
  restoring root handlers/parameters/prompts) and then runs the default
  handler through the shared CPS path. Generated main runs the program under
  `npl_program_run`, whose C landing pad also unwinds a root abort raised in a
  nested synchronous callback; native threads use their termination jump and
  virtual threads the carrier mount landing pad. A single handler value is the
  thread result; zero/multiple values stay represented by the runtime values
  object.
- T4-B0 wind environments: each `NplWindFrame` records the prompt chain and
  current segment of its extent. Before/after thunks run with that environment,
  so a transfer out of an inner prompt no longer exposes it to an outer after
  thunk.
- Prompt frames and the current segment live in `NplThreadContext` and are
  captured by `NplDynamicContext`. The prompt terminal consumes a per-thread
  one-shot override. There is no helper native thread, no cross-thread logical
  identity change and no carrier join. Outer winders stay in their original
  extent, prompt frames survive `call/cc`, and virtual-thread save/restore
  carries both chains.
- Remaining T4-A/T4-C work: evaluated user closures are direct-style native
  procedures, so continuation marks over arbitrary evaluated helper frames
  need the T4-C evaluator frame adapter. Runtime-created transition
  continuations (wind thunks, parameter converters, promise work) do not yet
  install a compiler segment, so captures inside those thunks have incomplete
  frame metadata. The interactive REPL uses the process root prompt; a
  per-evaluation REPL boundary belongs with later initial-continuation work.
- T4-D subset/D0: `(npl control parameters)` and `(srfi 226 parameter)` add
  `parameter?`, first-class parameterization snapshots (`current-
  parameterization`, `parameterization?`, `call-with-parameterization`) and
  `temporarily` over the existing parameter machinery. A parameterization
  shares the binding chain, so reinstalling the same snapshot preserves
  in-place cell updates; a set! of a parameter with no binding extends the
  snapshot's own chain, so the next installation sees it. A cell already
  shared with an enclosing parameterization is mutated in place and is
  visible outside the snapshot. D0 replaces this with an explicit
  parameter-to-cell mapping plus synchronization; cross-thread shared cells,
  thread parameters and the fluid syntax then follow.
- Not implemented: continuation marks, thread parameters/fluids, promises,
  exception/condition payloads, thread/interrupt adapters, and the composite
  `(srfi 226)` / `srfi-226` feature. The component aliases are partial
  releases.
- Acceptance: `src/compiler/tests/control.npl` (47 assertions, including
  invoking-prompt composition, abort routing and wind-thunk prompt
  environments), the `tests/fixtures/control` nplc/npli differential fixture
  (barrier, call-in and composition cases included),
  `tests/unit/control-virtual.npl` on M=1/M=4 (composition, barriers and
  call-in), `tests/unit/control-default-prompt.npl` for
  program/thread/virtual-thread roots, `tests/unit/control-b0.npl` for
  root-return ABI, root abort landing pads (including synchronous callbacks)
  and snapshot cells, and the existing call/cc, parameter, wind and exception
  suites.
- The frontend/backend segment emission changes the compiler's own SSA. The
  seed was relayed with the codegen-transient mode of
  `tools/update_current_ssa.sh`; the installed seed is gen2 with
  gen2 == gen3 (441,853 lines, SHA-256
  `32ffe87c5ed356edc0ac0d4f80ed71a4d695c991d24ebba329b45b5ed434bb37`).

No changes to permanent `bootstrap/bootstrap.ssa`, stage0, or
`bootstrap/runtime/`. Frontend changes relay only `bootstrap/current.ssa`
through `tools/update_current_ssa.sh` and must preserve canonical fixed-point
SSA. Runtime-only increments still test both static and shared link modes.

## Correctness prerequisites and later optimization

### P0: close known correctness gaps and establish measurements

These are release gates, not performance shortcuts. Record failures and
remaining platform limits explicitly; passing a throughput benchmark does
not close them.

| Work item | Planned change / gate | Acceptance evidence |
|---|---|---|
| Parameter snapshot race | Synchronize reads of source binding values with setters; allocate copies outside the lock, preserve binding identity and chain order; prerequisite for T4-D | Concurrent source mutation/capture/inheritance with forced GC, plus all cross-thread wind fixtures |
| Generated stop-flag memory model | Audit current plain QBE loads versus C atomic stores; choose a proven target emission or runtime accessor, and migrate current seed through the supported relay | Optimized direct/backedge loops observe GC/cancel requests on executed targets; inspect emitted machine code; freeze permanent bootstrap unchanged |
| Blocking register snapshot | Refactor `npl_gc_block_begin` snapshot/lifetime boundary so the published register/stack range remains valid for the whole blocked interval | Register-only and stack-only roots across a blocked libc call and concurrent collections; nested exclusion/blocking and cancellation |
| Worker stack bounds | Derive usable bounds/window from platform APIs, including small stacks and guard pages | Deep direct/CPS recursion and overflow recovery on macOS/Linux worker/helper/carrier paths |
| Execution coverage | Run Linux acceptance and static/shared runtime identity; add an actual late-dlopen client using the supported accessor path | One runtime registry/heap/TLS identity; no duplicate roots or unsupported direct TLS assumption |

Use `tools/check_linux.sh` and the existing Linux setup rather than a new
platform harness. An unavailable host/loader scenario remains missing evidence,
not a passing test. Sanitizers are useful on isolated C mechanisms where the
conservative collector permits them; do not suppress all reports to obtain a
clean label. ThreadSanitizer cannot establish generated QBE memory ordering.

### P1–P4: optimize one measured bottleneck at a time

| Priority | Change and design boundary | Experiment / acceptance |
|---|---|---|
| P1a | Replace the global shared-object lock with per-port locks and lifetime leases; serialize access to the same port, borrow the native handle during I/O, and coordinate close with active borrowers | One blocked pipe must not stall unrelated ports, symbol interning or FFI; race close/read/write/error/cancel; GC progresses while blocked |
| P1b | Separate symbol interning and FFI/directory lifetime locks using the same reusable acquisition/cleanup mechanisms | Parallel independent resources scale; no check-use-free race; nested reader recovery retains outer locks/cleanup |
| P1c | Reclaim `parameter_extents` entries with GC-aware weak/ephemeron-style reachability | Long enter/leave loop reaches bounded live memory after full GC, while a retained continuation still re-enters with its mutations intact |
| P2a | Local enqueue by default, idle-carrier work stealing, bounded steal attempts and wakeups | Skewed and mixed-duration work improves without duplicate/lost runs; parked carriers wake; deterministic test mode retains forced migration |
| P2b | CPU-derived configurable carrier count, respecting available CPUs/container limits where exposed | Sweep M rather than assume more carriers are better; separate latency and CPU-throughput defaults; preserve M=1 correctness |
| P2c | Bounded helper resources and timer queue; completion messages own roots and wait state | Thousands of sleeps stop creating proportional native threads; nested helper dependencies/join chains cannot exhaust the pool and deadlock |
| P3 | Nonblocking I/O with epoll/kqueue behind a common completion protocol; explicit virtual-thread wait adapters | Exactly-once resume across readiness/timeout/cancel/close; fd-reuse generation checks; partial read/write, EOF/EINTR; carriers stay available |
| P4 | T3 scalar fast paths; mark/prompt lookup caching; allocation/GC-lock improvements only if profiles justify them | Same semantics and GC barriers as baseline; lower measured cost at target workload; single-thread/compiler regressions bounded |

P1c cannot merely delete every extent on scope exit: captured continuations
need its local binding identity. Do not keep keys strongly reachable through
the mapping's own value/back-links and call that a weak table. Define the
mark/fixpoint/prune phase, account for active and captured extents, and accept
conservative false retention as measured rather than promise exact reclamation.

P2c cannot simply cap today's growing helper pool: a helper waiting on work
queued behind it can deadlock. Timer, virtual join, and scheduler-aware waits
should leave the pool first; remaining synchronous/FFI dependencies need an
explicit compensation or admission policy and tests. Preserve the existing
helper dynamic-state contract until an explicit context-transfer API exists.

P3 starts with sockets/pipes and a timer heap. Regular files and arbitrary FFI
remain helper-backed where readiness cannot provide the desired asynchronous
behavior. A mutex acquired by a helper is owned by that helper today; transparent
virtual mutex waits require logical-owner wait descriptors, not wrapping
`mutex-lock!` in another helper thunk. Preserve SRFI 18 semantics and keep
its native thread kind separate until equivalence is demonstrated.

Work stealing and netpolling do not require full T4. Neither requires copying
C stacks. Stackful suspension, moving GC, precise stack maps, and lock-free
scheduler queues remain separate proposals with their own evidence burden.
Keep a lock-based queue/reference path until profiling demonstrates a need.

## Benchmark plan

### B0: reproducible baseline and harness reuse

Capture baseline `bf6d497` before implementation; also compare each increment
with its immediate parent. The current `tools/bench.py` measures process wall/
CPU time, warms once, reports median/min/max, separates compilation, and checks
warm-run output across native/npli/Guile/Racket. Reuse those workloads and data
conventions. It does not yet measure steady-state latency distributions,
per-run correctness, RSS, thread counts, GC phases, or scheduler queues.

Add concurrency workload orchestration in NPL (proposed
`tools/bench-threads.npl` plus `bench/threads/` and `bench/control/`), reusing
existing process/time libraries and test fixtures. A small C probe may expose
missing OS counters/monotonic nanoseconds; it must not own benchmark policy.
Extend the existing host wrapper only for collection/reporting that it already
handles. Do not create a second general Scheme benchmark runner.

Each sample must record: git SHA and dirty status, seed hash, build/link mode,
compiler/C compiler/QBE flags, OS/architecture/CPU availability, carrier/helper
configuration, GC settings, workload parameters, seed, warmup count, duration,
raw timings/counters, checksum, exit status and timeout. Save structured raw
JSON/CSV plus a generated Markdown report. Keep failed/timed-out samples as
failures, not discarded slow outliers. Proposed report home:
`bench/results/threads/<platform>/<revision>/`; commit curated reports and
metadata, retaining bulky raw artifacts separately with a reproducible command.

Use a workload start barrier after workers are ready for steady-state tests;
measure creation/start/join separately. Fixed work measures throughput and
speedup; fixed-duration or fixed-arrival-rate work measures latency and overload.
For each configuration, calibrate once to roughly 1–5 seconds per sample,
freeze the work count for baseline/candidate, warm up at least 3 times, and
collect at least 15 independent process samples. Interleave baseline/candidate
runs to reduce thermal/load drift. Report median, dispersion and a confidence
interval across independent samples; millions of operations in one process
are not millions of independent trials.

Do not derive p99 from 15 process timings. Collect per-operation or sampled
latency histograms with sufficient events (target at least 100,000 for p99,
report count and sampling rate), and report their run-to-run variability.
Compare instrumented and uninstrumented overhead. Disable tracing and GC
verification for performance runs; use them in separate correctness runs.

### Workload matrix

Let P be available CPU capacity, M carrier count, N logical/native workers,
and K total operations. Always include M=1 and N>M. Sweep M=1,2,4,P and, when
resources permit, 2P; deduplicate equal values. Use increasing N within an
explicit memory/thread cap and record skipped sizes. Never attempt 100,000
native threads just because that size is appropriate for parked virtual work.

| ID | Workload and parameter sweep | Metrics / purpose |
|---|---|---|
| B1 | Existing loop/fib/tak/closures/lists/vectors/strings/callcc; compiler self-build | Single-thread wall/CPU, compile time, SSA size; detect poll/control-representation overhead |
| B2 | Native create/start/join; virtual create/start/complete; batches N=1,10,100,1k and larger virtual-only sizes | Lifecycle latency, creations/s, peak native threads, RSS/live bytes per parked task |
| B3 | Yield ring and independent CPU tasks, N=M,10M,100M; uniform and one deliberately skewed queue | Switches/s, yield-to-resume p50/p95/p99, steals/migrations, queue depth, per-carrier utilization |
| B4 | Sleep fan-out, nested virtual joins, mixed CPU and blocking helper jobs | Timer wake overshoot, runnable-task latency, helper count/queue depth, carrier availability; detect pool exhaustion |
| B5 | Uncontended mutex; counter contention; two-worker ping-pong; CV signal/broadcast | ns/op, handoffs/s, wait p99, wake amplification, CPU spent waiting, bounded-run starvation observations |
| B6 | Atomic flag/fxbox/box/pair ref/set/swap/CAS/fetch/fence; one hot object versus disjoint objects; all orders | ns/op, aggregate throughput, CAS retries, failed-CAS cost, pointer barrier/allocation cost; locked vs optimized paths |
| B7 | Heap allocation with N mutators, pointer publication, idle/blocked workers; default and small nursery | Bytes/s, allocations/s, request-to-last-park, mark/sweep/total pause p50/p95/p99, heap-lock wait, post-GC live bytes |
| B8 | Prompts/abort; non-composable and composable capture/apply, multi-shot reuse; depth 0,1,10,100,1k | ns/op, bytes/capture/application, depth scaling, retained heap; creation and invocation reported separately |
| B9 | Same-frame tail marks vs non-tail marks; query depth/key count; parameter/fluid/TLS lookup and extent loops | Time/op, live metadata after full GC, mark/query scaling, bounded-space tail behavior, weak-prune effectiveness |
| B10 | Independent pipes plus one stalled pipe; socket echo and mixed CPU/I/O; payload 64 B,1 KiB,64 KiB | Requests/s, bytes/s, latency p99, CPU, blocked carriers; shared lock vs per-port vs netpoller |
| B11 | Concurrent promise forcing; interrupt delivery at several mask durations; cross-thread continuation transfer | Completion arbitration cost, interrupt request-to-delivery, transfer/wind cost, result consistency and retained state |
| B12 | Soak: create/yield/block/capture/cancel with periodic full GC and resource close | Live heap/RSS slope, open descriptors/native threads, stalled progress; separate correctness run, not a throughput score |

B6 includes cache-line-separated and deliberately adjacent independent atomic
objects to identify false sharing. B8/B9 include zero-feature controls so the
cost of merely enabling the new representation is visible. B7 separates time
waiting for a safepoint from actual collector work. RSS includes stacks and
allocator retention; pair it with live heap and registered-thread counts before
calling a change a leak or a memory reduction.

For B10 use both a closed-loop throughput test and scheduled arrivals for
latency: timestamp from intended arrival and include queueing delay to avoid
coordinated omission. Avoid terminal output in timed regions; buffer per-worker
results and validate checksums/operation counts afterwards. For every workload,
state whether payload allocation and GC are part of the timed work.

### Comparisons and decision gates

Primary comparisons are same-host NPL baseline/candidate, native vs virtual
where the work and synchronization semantics match, and locked vs optimized
atomics. Define speedup as T(1)/T(N) for fixed work and efficiency as speedup/N
for N CPU workers; do not apply this formula to waiting-task concurrency.
Report both latency and total CPU: moving work to helpers can lower wall time
while increasing native-thread and CPU cost.

Retain Guile/Racket comparisons for compatible single-thread workloads.
Optionally add JVM virtual-thread or Go goroutine workloads for lifecycle,
sleep and I/O capacity, with equal work, payloads, concurrency, warmup and
machine limits. They are system-level reference points, not SRFI conformance
or language-speed rankings. Record runtime versions and GC configurations;
do not mix JVM warmup samples into NPL steady-state ratios.

Proposed gates, to calibrate against B0 noise before enforcing them:

- Every performance sample passes its result invariant and completes within a
  recorded timeout. Semantic tests, GC verification and bounded-space tests
  are hard gates regardless of throughput.
- Investigate a repeatable >5% single-thread median regression or >10% p99
  latency regression with a confidence interval excluding noise. These are
  review triggers, not universal constants or automatic CI failures on shared
  runners. Run performance gates on a stable dedicated machine.
- Accept an optimization for a demonstrated target benefit (proposed target:
  at least 10% beyond noise) or a clear resource/progress improvement, with
  no unexplained regressions. Record the workload and tradeoff; do not average
  away a starvation, memory-growth, or unrelated-port blocking regression.
- CI runs short correctness/progress workloads and artifact-format checks.
  Nightly/dedicated runs collect the full matrix; pre-release runs include
  Linux and macOS, static/shared modes, P0 cases and B12 soaks. Report target
  execution separately from target code-emission evidence.

### Implementation order and review artifacts

1. **B0 + P0 audit:** capture reproducible current measurements, add only the
   counters needed to explain them, and turn correctness debts into failing
   acceptance cases. Fix affected prerequisites before claiming support.
2. **T3-A → B → C:** deliver complete SRFI 230 against the locked baseline;
   record B1/B5/B6/B7. This establishes a reference for future fast paths.
3. **T4-A → B → C/D → E → F → G:** ship independently testable components;
   record B1/B8/B9 at the representation change, B11 at promise/interrupt work,
   and keep native/evaluator fixtures common throughout.
4. **P1 → P2 → P3, with T3-D/P4 driven by profiles:** port/retention work may
   proceed alongside T4 after shared state boundaries settle. Compare each
   optimization independently and keep a reference configuration for diagnosis.
5. **Release evidence:** full acceptance, fixed point when frontend changes,
   export/feature audit, platform matrix, benchmark report, and explicit
   remaining limits. Performance numbers are measured deliverables, not
   promises made by this design document.

Each implementation PR records its semantic contract, reused modules, any
necessary duplication, exact checks and benchmark commands, raw artifact
locations, and before/after results. Use the existing `tools/run_tests.sh`,
`tools/check_full_compiler.sh`, relevant C harnesses and differential fixtures;
run the smallest relevant checks first, then the required full gates. Keep
only the active uncommitted task in `.agents/focus.md`; commits/PRs hold the
completed work and evidence.

## Public Boundary

- Runtime C owns pthreads, stacks, GC roots, and safepoints.
- `(npl thread)` is the public NPL API; users never see `pthread_t`.
- Core native packages may use runtime mutex primitives, but not raw
  pthreads, so a later scheduler can replace the substrate.
- External packages stay single-threaded unless the host explicitly passes
  thread callbacks; do not call the runtime allocator from a foreign thread.

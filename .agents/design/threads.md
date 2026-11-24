# Threads and the Runtime

Status: proposal, revised 2026-11-24. T1/T2 direction; no threading code yet.

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
  atomics; unsynchronized shared mutation is outside the contract.
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
  cancellation is cooperative, never asynchronous `pthread_cancel`. The
  cancellation request is observed at the same generated polls used for
  safepoints, and runtime-managed blocking waits must be woken so the thread
  can observe it. SRFI 18 requires another thread's termination to complete
  before `thread-terminate!` returns: acknowledge completion, abandon owned
  mutexes, and publish the terminated-thread exception for join. Scheduler
  termination does not run dynamic-wind after thunks. Specify how outstanding
  native calls finish before resources are released; an uninterruptible call
  can delay termination. Preserve the distinct normal-result, uncaught-error,
  terminated-thread, and timeout outcomes of join.

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
  be invoked by another thread: restore their dynamic state on the invoking
  thread, without transferring native stack/unwind frames. This needs explicit
  runtime support and cross-thread continuation tests before claiming SRFI 18.
- Blocking primitives (`thread-sleep!`, mutex/condition waits) park the
  native thread through the T1 safe blocked-state transition; GC continues
  while it is parked only after its saved context and roots are published.

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

## T3 SRFI 230

Atomics need runtime support regardless of scheduler. [SRFI 230](../../docs/reference/srfi-230.html)
defines atomic flags, boxes, fixnum boxes, and pairs, with sequentially
consistent default ordering and explicit memory-order options; it does not
define atomic vectors. Boxes and pairs accept arbitrary NPL values, so their
stores must participate in GC root/edge discovery and remembered-set barriers.
A non-pointer-only first increment is partial support, not full SRFI 230.
Keep the atomic API independent of the thread representation.

## T4 SRFI 226

Prompts, continuation marks, fluids, and the thread libraries need a
portable continuation representation. Do not claim SRFI 226 before T1-T3
and the prompt/mark model are implemented.

## Later M:N Option

T2b is the M:N direction. The remaining work after T2b/T4 is scale and
substrate tuning:

- Tune the T2b run queue and carrier pool; add work stealing and a netpoller.
- If suspension ever retains C stacks, register their bounds and saved
  registers with the existing conservative collector; use precise maps only
  if a later collector or performance requirement needs them.
- Keep SRFI 18 native threads as a separate thread kind, or map both kinds
  to the same thread object with a `native?` field.
- Keep the scheduler interface stable; user code and SRFI 18 should not
  observe the substrate change.

## Public Boundary

- Runtime C owns pthreads, stacks, GC roots, and safepoints.
- `(npl thread)` is the public NPL API; users never see `pthread_t`.
- Core native packages may use runtime mutex primitives, but not raw
  pthreads, so a later scheduler can replace the substrate.
- External packages stay single-threaded unless the host explicitly passes
  thread callbacks; do not call the runtime allocator from a foreign thread.

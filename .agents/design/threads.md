# Threads and the Runtime

Status: proposal, 2026-11-23. T1/T2 direction; no threading code yet.

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

This is the JVM model before virtual threads, and the safe first step for
Go-like scheduling. SRFI 18 does not require M:N; it requires threads,
mutexes, condition variables, joins, and fair scheduling.

## Why Not M:N First

- The collector is non-moving but scans each thread's C stack
  conservatively. Green threads need growable or segmented stacks, stack
  copying, and precise stack maps before the collector can trust them.
- Preemption needs safepoints in generated code and a stop-the-world
  protocol. NPL currently has a trampoline and no safepoint table.
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

- **Thread registration.** Every runtime thread has a record with stack
  bounds, roots, dynamic-wind/handler/parameter chains, and a native handle.
  The primordial thread registers at startup.
- **GC.** Start with a stop-the-world collector: all threads reach a
  safepoint, then one collector scans every registered stack and root set.
  Allocation and remembered-set updates are mutex-protected until the
  collector is parallel.
- **Dynamic state.** Move `g_current_wind`, `g_current_handler`,
  `g_current_parameters`, and continuation state from runtime globals into
  the thread record. The public parameter/handler APIs read the current
  thread.
- **Synchronization.** Mutex and condition-variable primitives over
  pthreads, with timeout and error mapping. SRFI 18 builds on these; it does
  not call pthreads directly.
- **Blocking.** A native blocking call marks the thread blocked, releases
  the runtime lock, performs the call, then reacquires. The first version
  keeps the GC from running while a thread is in an unknown C call unless
  the call is wrapped.
- **Termination.** Join waits for normal return or an uncaught raise;
  cancellation is cooperative at safepoints first, not asynchronous
  `pthread_cancel`.

## T2 SRFI 18

- `(npl thread)` / `(srfi 18)` exports `thread`, `make-thread`,
  `thread-start!`, `thread-join!`, `thread-yield!`, `thread-sleep!`,
  `thread-terminate!`, mutexes, condition variables, and
  `current-thread`.
- Threads created by `thread-start!` are 1:1 with pthreads in T2. The
  scheduler interface is the only place that knows this.
- `thread-specific` and `thread-specific-set!` use a per-thread table.
- Blocking primitives (`thread-sleep!`, mutex/condition waits) park the
  native thread; GC continues while it is parked.

## T3 SRFI 230

Atomics need runtime support regardless of scheduler: fixnum/cell
compare-and-swap, atomic vectors, and documented memory order. Keep the
atomic API independent of the thread representation.

## T4 SRFI 226

Prompts, continuation marks, fluids, and the thread libraries need a
portable continuation representation. Do not claim SRFI 226 before T1-T3
and the prompt/mark model are implemented.

## Later M:N Option

If M:N is wanted after T2/T4:

- Add a run queue, carrier threads, work stealing, and a netpoller.
- Make NPL thread stacks growable or copyable, and give the collector
  precise stack maps.
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

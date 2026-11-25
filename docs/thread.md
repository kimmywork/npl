# Threads and SRFI 18

`(npl thread)` implements [SRFI 18](reference/srfi-18.html), with `(srfi 18)`
as a re-export-only alias. The feature identifier `srfi-18` is advertised by
`(features)` and accepted by `cond-expand`.

## Execution model

Each NPL thread created by `thread-start!` runs on one native pthread (1:1).
The scheduler interface — create, start, join, yield, block, terminate — is
the only layer that knows the substrate is 1:1; a later M:N substrate can
replace it without changing user code. SRFI 18 imposes no fairness
constraints; NPL provides cooperative scheduling at generated safepoint
polls (direct procedure entries, loop backedges, allocation, trampoline
dispatch) plus the blocking primitives.

A new thread inherits its creator's dynamic environment with **fresh**
per-thread parameter bindings, a fresh initial exception handler and an
empty `dynamic-wind` stack. Uncaught raises are captured as an
`uncaught-exception` object and re-raised by `thread-join!`.
`thread-terminate!` is cooperative (never `pthread_cancel`): the request is
observed at a safepoint poll, owned mutexes are abandoned, and the
termination is acknowledged before `thread-terminate!` returns. When the
primordial thread terminates, the process ends and all threads with it.

## API

```scheme
(import (npl thread))

(define t (make-thread (lambda () 42) 'worker))
(thread-start! t)
(thread-join! t)                 ; => 42
```

Exports: `current-thread`, `thread?`, `make-thread`, `thread-name`,
`thread-specific`, `thread-specific-set!`, `thread-start!`, `thread-yield!`,
`thread-sleep!`, `thread-terminate!`, `thread-join!`; `mutex?`, `make-mutex`,
`mutex-name`, `mutex-specific`, `mutex-specific-set!`, `mutex-state`,
`mutex-lock!`, `mutex-unlock!`; `condition-variable?`,
`make-condition-variable`, `condition-variable-name`,
`condition-variable-specific`, `condition-variable-specific-set!`,
`condition-variable-signal!`, `condition-variable-broadcast!`;
`current-time`, `time?`, `time->seconds`, `seconds->time`;
`current-exception-handler`, `with-exception-handler`, `raise`;
`join-timeout-exception?`, `abandoned-mutex-exception?`,
`terminated-thread-exception?`, `uncaught-exception?`,
`uncaught-exception-reason`.

### Timeouts

`thread-join!`, `thread-sleep!`, `mutex-lock!` and `mutex-unlock!` accept a
`time` object (absolute point on a monotonic clock), an exact or inexact
real (relative seconds), or `#f` for no timeout. `thread-sleep!` rejects
`#f`. A timeout already reached checks the synchronization condition first
and does not block the thread. `thread-join!` returns its `timeout-val` on
timeout, or raises a `join-timeout-exception` when none is supplied.

### Mutexes and condition variables

A mutex has four states: unlocked/not-abandoned, unlocked/abandoned,
locked/not-owned and locked/owned. Mutexes are **not recursive**. When a
thread terminates while owning a mutex, that mutex becomes
unlocked/abandoned and the next successful `mutex-lock!` raises an
`abandoned-mutex-exception` after the state change.

SRFI 18 has **no `condition-variable-wait!`**. Waiting is:

```scheme
(mutex-lock! m)
(let loop ()
  (if (condition-is-true?)
      (begin (do-work) (mutex-unlock! m))
      (begin (mutex-unlock! m cv) (loop))))
```

`mutex-unlock!` with a condition variable atomically unlocks the mutex and
enqueues the thread; it does **not** re-lock on wake, so an explicit
`mutex-lock!` is required. `condition-variable-signal!` wakes one waiter and
`condition-variable-broadcast!` wakes all.

### Time objects

SRFI 18 `current-time` returns a `time` object distinct from SRFI 19's
`(npl time)` time structure; importing both libraries makes `current-time`
and `time?` conflict. `time->seconds` returns an inexact number of seconds
since an implementation-defined reference point; `seconds->time` converts
back.

## GC and shared state

The collector is a parallel stop-the-world conservative collector. Each
mutator publishes a stack pointer and callee-saved register snapshot at a
safepoint and parks; the collector scans every parked thread's C stack,
register snapshot, root stack and trampoline frames before sweeping. A
thread in an unwrapped C call keeps GC excluded; runtime-managed blocking
waits publish their context first.

Concurrent writes to a shared mutable NPL object must use NPL mutexes or
atomics — unsynchronized shared mutation is outside the contract. Ports are
the exception: the runtime serializes access to the same port.

## Current limitations

- `with-exception-handler` and `raise` are re-exported R7RS control
  operators, not first-class procedures.
- Ports, FFI blocks, directory handles and the symbol table are serialized
  by one shared lock with a trylock+poll acquisition and a `longjmp` release
  hook. Blocking libc I/O (`fread` / `fwrite` / `fclose` / `fflush` and
  `run-process`'s `waitpid`) now publishes its context and parks through the
  safe blocked-state transition, so a collection on another thread does not
  stall.
- SRFI 21 priority/quantum procedures and the three-argument `make-thread`
  are not part of SRFI 18 and are not exported.
- SRFI 226 prompts/continuation marks are a separate, later increment (T4).
- Linux execution and late `dlopen` are untested (host is macOS arm64).

## Cross-thread continuations

A continuation captured with `call/cc` on one thread can be invoked on
another, including after the source thread has ended. Invocation reinstates
the saved dynamic environment (parameters such as `current-output-port`, the
`dynamic-wind` stack, and user exception handlers) on the invoking thread:
an invocation inside `(parameterize ((p 7)) ...)` still sees the values that
were in effect where the continuation was captured, and every `dynamic-wind`
before/after thunk runs in the environment of its own extent. No native stack
or unwind frames are transferred; `current-thread` remains the invoking
thread, and reaching the initial continuation or the initial exception
handler terminates the invoking thread (not the source thread). The source
thread's record and `jmp_buf` are never touched by the invocation.

This is native-runtime behaviour; the evaluator (`npli`) shares the
single-thread continuation semantics but is not used to exercise
cross-thread scenarios.

## Virtual threads

`(npl thread virtual)` provides JVM-style virtual threads multiplexed over
carrier pthreads by the runtime (the `virtual-threads` feature is present).
This is a separate substrate: native SRFI 18 threads remain 1:1 and the two
kinds coexist.

```scheme
(import (scheme base) (npl thread virtual))

(define t (make-virtual-thread
            (lambda ()
              (virtual-thread-yield!)   ; scheduler safepoint
              42)))
(virtual-thread-start! t)
(virtual-thread-join! t)                ; => 42
```

`virtual-thread?` recognizes a virtual thread object, `current-virtual-thread`
returns the currently mounted one (or `#f` outside a virtual thread), and
`make-virtual-thread` accepts an optional name.

A virtual thread may suspend only at a CPS boundary.
`virtual-thread-yield!` hands its one-shot scheduler continuation to the run
queue and returns control to the carrier loop; the suspended computation and
its dynamic environment are heap objects, so no carrier is blocked and no C
stack frame is retained. The scheduler continuation is internal, one-shot,
and never exposed as `call/cc`: native `call/cc` keeps its full SRFI 18
multi-shot, cross-thread semantics. Yielding is transparent to
`dynamic-wind` — it does not run before/after thunks. `virtual-thread-join!`
waits for the virtual thread's end result.

The runtime multiplexes virtual threads over **M carriers**, each with its
own run queue. A yield hands the virtual thread to the next carrier, so
suspended work migrates; ownership of the virtual thread's parameter bindings
is re-stamped on each cross-carrier mount, keeping in-place
`parameter-set!` semantics. The default is two carriers.

A synchronous blocking call must not block a carrier. `virtual-thread-block!`
parks the virtual thread and runs a thunk on a helper worker; when the work
finishes, the virtual thread resumes on the next carrier with its result:

```scheme
(virtual-thread-block! (lambda () (thread-sleep! 0.1)))
```

`virtual-thread-sleep!` is the convenience wrapper for SRFI 18
`thread-sleep!`; mutex/condition waits and blocking I/O thunks can be
wrapped the same way. The thunk runs on a helper with its own dynamic state,
so pass values explicitly as arguments; SRFI 18 mutex ownership inside a
helper is a known limitation. Native SRFI 18 threads are unaffected.

Work stealing between carrier queues and a CPU-derived carrier count are
later tuning. Virtual-thread behaviour is accepted by native tests with one
and four carriers; the evaluator (`npli`) does not provide a carrier
scheduler surface.

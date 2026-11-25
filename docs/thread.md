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
- Cross-thread invocation of a user continuation captured with `call/cc`
  is not yet supported.
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

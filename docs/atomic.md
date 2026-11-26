# Atomic operations (SRFI 230)

`(npl atomic)` implements [SRFI 230](reference/srfi-230.html); `(srfi 230)`
is a re-export-only alias. The feature identifier `srfi-230` is advertised by
`(features)` and `cond-expand`.

```scheme
(import (scheme base) (npl atomic))

(define counter (make-atomic-fxbox 0))
(atomic-fxbox+/fetch! counter 1)
(atomic-fxbox-ref counter)       ; => 1
```

The library is independent of `(npl thread)` and `(npl thread virtual)`: it
only exposes an atomic object model, so native and virtual workers can share
the same boxes and pairs.

## Surface

| Family | Operations |
|---|---|
| Orders | `(memory-order name)` syntax, `memory-order?`; symbols `relaxed`, `acquire`, `release`, `acquire-release`, `sequentially-consistent` |
| Flag | `make-atomic-flag`, `atomic-flag?`, `atomic-flag-test-and-set!`, `atomic-flag-clear!` |
| Box | `make-atomic-box`, `atomic-box?`, `atomic-box-ref`, `atomic-box-set!`, `atomic-box-swap!`, `atomic-box-compare-and-swap!` |
| Fixnum box | the corresponding constructor/predicate/ref/set/swap/CAS, plus `atomic-fxbox+/fetch!`, `atomic-fxbox-/fetch!`, `atomic-fxbox-and/fetch!`, `atomic-fxbox-ior/fetch!`, `atomic-fxbox-xor/fetch!` |
| Pair | `make-atomic-pair`, `atomic-pair?`, `atomic-pair-ref`, `atomic-pair-set!`, `atomic-pair-swap!`, `atomic-pair-compare-and-swap!` |
| Fence | `atomic-fence` with a required order |

Every operation with an optional order argument defaults to
`sequentially-consistent`; `atomic-fence` requires an explicit order.
`memory-order` checks the **spelling** of its identifier, not its lexical
binding, and expands to the corresponding ordinary symbol. Invalid names are
compile-time syntax errors.

CAS returns the observed old contents on both success and failure, never a
success boolean. Box CAS uses `eq?`; fixnum-box CAS compares fixnums; pair CAS
compares both fields with `eq?` and returns the two observed values. Pair ref
and swap also return two values from one indivisible snapshot. All five
`/fetch!` operations return the previous fixnum.

## Ordering and implementation status

The initial T3 reference implementation serialized **all** public operations,
including fences, through one dedicated atomic-domain mutex. T3-D keeps that
domain for sequentially consistent operations, pair operations and fences, but
gives scalar flag/box/fixnum-box operations with `relaxed`, `acquire`,
`release` or `acquire-release` a per-location `__atomic_*` fast path. The
mapping follows the design table: loads strengthen an unavailable release
order to sequentially consistent, stores strengthen acquire, CAS failure uses
relaxed for a release RMW, and so on.

The mixed-domain argument is:

- every sequentially consistent scalar operation, every pair operation and
  every fence uses the same locked domain, so the public default still has one
  total order across all object kinds;
- weaker scalar operations are only promised their C11 acquire/release or
  relaxed semantics on that location, not membership in the sequentially
  consistent total order (SRFI 230 does not promise one for weaker orders);
- a pair is always one lock, never two independent word stores;
- pointer stores still complete their remembered-set publication before the
  next safepoint or blocked transition.

`NPL_ATOMIC_FORCE_LOCKED=1` selects the all-locked reference path for
comparison.

The mutex is acquired with trylock plus a safepoint poll, so a world stop never
waits on a thread blocked in `pthread_mutex_lock`. The critical section never
allocates managed objects, runs Scheme callbacks, polls or raises. Type and
order validation happen before the lock; arithmetic range failure is recorded
inside and raised after release. A lock waiter can therefore be cancelled at
its next safepoint without leaving the atomic domain locked.

`runtime/atomic_bench.c` is a small measurement probe, not the repository
benchmark runner. A representative macOS arm64 run (`5,000,000` scalar ops,
`1,000,000` pair swaps) gave roughly:

| Operation | Weak-order fast path | Forced locked |
|---|---:|---:|
| `box-ref` relaxed | 2.5 ns/op | 6.7 ns/op |
| `box-swap!` relaxed | 4.2 ns/op | 7.8 ns/op |
| `fxbox+/fetch!` relaxed | 5.0 ns/op | 7.0 ns/op |
| flag set+clear relaxed | 5.7 ns/op | 13.5 ns/op |
| pair swap (always locked) | ~50 ns/op | ~50 ns/op |

These are process-level probes, not the B6/B7 matrix or a general throughput
claim. Optional pair specialization remains future work.

## Fixnum range policy

Fixnum boxes store the runtime's signed 62-bit fixnum range. Constructor,
set, swap, CAS and `/fetch!` operands must be fixnums. Addition and subtraction
are checked; a result outside the range raises an error **before** the box is
changed. Bitwise operations also validate their result. SRFI 230 does not
specify overflow behavior, so wrapping is not promised.

## GC integration

A box has one managed edge and a pair has two; flags and fixnum boxes have no
managed edges. Pointer stores use the atomic domain's write-barrier path, which
takes the heap lock and re-checks generation flags and remembered-set
membership under it. The lock order is always atomic-domain lock then heap
lock; no path takes them in reverse or polls while either is held.

Pair results are allocated after the atomic operation, with both observed
fields rooted first. A failed CAS therefore still returns a live observed
object after a collection. The C acceptance harness runs the same histories
under `NPL_GC_NURSERY_BYTES=65536 NPL_GC_VERIFY=100`.

## Verification

- `tests/fixtures/atomic` — deterministic nplc/npli differential surface.
- `tests/unit/atomic.npl` — every export, order syntax, renamed/shadowed
  names, wrong types, extra arguments and fixnum boundaries.
- `tests/unit/atomic-threads.npl` — contended counter permutation,
  release/acquire publication, and native/virtual workers on M=1 and M=4
  carriers.
- `runtime/atomic_test.c` — C substrate, pair snapshots, publication,
  concurrent collector races, failed-CAS retention and GC barriers; it runs
  with both the fast-path and `NPL_ATOMIC_FORCE_LOCKED=1` configurations.
- `runtime/atomic_bench.c` — T3-D locked-versus-fast measurement probe.

The implementation is host-executed on macOS arm64; the repository CI Linux
gate also passed for the T3-A/B/C revision. The T3-D weak-order paths are
measured with the probe above; the full B6/B7 benchmark matrix and optional
pair specialization remain future evidence.

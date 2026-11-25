/*
 * coordinator.c — T1.3 stop-the-world coordination.
 *
 * A collection asks every mutator to publish its callee-saved registers and
 * the low end of its scanable C stack, then park.  Only after all active
 * mutators are parked does the collector scan their stacks, register
 * snapshots, root stacks and trampoline frames; nobody resumes until the
 * sweep is done.  Generated code polls with npl_safepoint_poll() (one global
 * load and a branch on the common path).
 *
 * Lock order (design/threads.md "GC and lock order"):
 *   - g_coord_lock: coordinator state, park/acknowledge, blocking transitions.
 *   - g_heap_lock:  allocator metadata, mark/sweep, remembered set.
 *   - g_collect_lock: serializes collectors.
 * The collector never waits for mutators to park while holding g_heap_lock:
 * a poll happens before any heap lock is taken.  Root registration and
 * remembered-set writes are serialized by g_heap_lock; root stacks are
 * per-thread and need no lock of their own.
 *
 * A monotonically increasing epoch identifies each world stop.  A thread
 * parked at a poll records the epoch it parked for; a thread blocked in a
 * runtime-managed wait publishes its snapshot once and is treated as parked
 * for every later epoch, so start_world never waits on a long sleep.
 *
 * An unwrapped C call keeps GC excluded (npl_gc_exclude); a wrapped
 * blocking call publishes its snapshot and parks before it releases
 * exclusion (npl_gc_block_begin / npl_gc_block_end).
 */

#define _POSIX_C_SOURCE 200809L

#include "npl.h"
#include "thread.h"
#include "thread-sched.h"
#include <pthread.h>
#include <setjmp.h>

static pthread_mutex_t g_coord_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_parked_cond = PTHREAD_COND_INITIALIZER;
static pthread_cond_t g_resume_cond = PTHREAD_COND_INITIALIZER;
static int g_stop_requested = 0;
static int g_active_mutators = 0;      /* attached and not GC-excluded */
static int g_blocked_mutators = 0;     /* published, parked for every epoch */
static int g_parked_for_epoch = 0;     /* safepoint-parked for g_epoch */
static uint64_t g_epoch = 0;
/* Generated polls only call npl_safepoint_poll when the exported stop flag
 * is set. A cooperative cancellation must also raise that flag so the target
 * reaches a poll; a counter keeps it set while cancellations are pending. */
static int g_cancel_pending = 0;

static void refresh_stop_flag(void)
{
    npl_gc_stop_requested = (g_stop_requested || g_cancel_pending > 0) ? 1 : 0;
}

static pthread_mutex_t g_heap_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_collect_lock = PTHREAD_MUTEX_INITIALIZER;

void npl_heap_lock(void) { pthread_mutex_lock(&g_heap_lock); }
void npl_heap_unlock(void) { pthread_mutex_unlock(&g_heap_lock); }

/* A second collector must not block on the collect mutex while the first is
 * waiting for mutators to park: a blocked (unparked) collector would deadlock
 * the world stop.  The caller retries at a safepoint instead, so it parks
 * when the first collector raises the stop flag. */
int npl_try_collect_lock(void)
{
    return pthread_mutex_trylock(&g_collect_lock) == 0 ? 1 : 0;
}
void npl_collect_unlock(void) { pthread_mutex_unlock(&g_collect_lock); }

/* Called by thread.c on attach/detach (registry spinlock is not held). */
void npl_coordinator_attach_internal(NplThreadContext *context)
{
    (void)context;
    pthread_mutex_lock(&g_coord_lock);
    /* A world stop may already be active: admitting an unparked mutator now
     * would let it enter managed execution during the collection (the
     * collector does not recheck the count). Wait for the stop to clear. */
    while (g_stop_requested)
        pthread_cond_wait(&g_resume_cond, &g_coord_lock);
    g_active_mutators++;
    pthread_mutex_unlock(&g_coord_lock);
}

void npl_coordinator_detach_internal(NplThreadContext *context)
{
    pthread_mutex_lock(&g_coord_lock);
    /* A thread detaching with an unconsumed cancellation must release its
     * contribution, or npl_gc_stop_requested stays set for the whole
     * process and every poll takes the slow path forever. */
    if (context->cancel_owed) {
        context->cancel_owed = 0;
        if (g_cancel_pending > 0) g_cancel_pending--;
        refresh_stop_flag();
    }
    if (!context->gc_excluded)
        g_active_mutators--;
    if (context->blocked_counted)
        g_blocked_mutators--;
    context->parked = 0;
    context->snapshot_valid = 0;
    context->blocked = 0;
    context->blocked_counted = 0;
    pthread_cond_broadcast(&g_parked_cond);
    pthread_mutex_unlock(&g_coord_lock);
}

/* setjmp materializes the caller's callee-saved registers into addressable
 * memory.  The jmp_buf lives in the context, so the collector can scan it
 * after this helper returns. */
static void capture_registers(NplThreadContext *context)
{
    (void)setjmp(context->registers);
}

/* Publish a snapshot whose C frame stays live while parked. */
static void publish_snapshot(NplThreadContext *context, uintptr_t marker)
{
    context->saved_stack_pointer = marker;
    capture_registers(context);
    __sync_synchronize();
    context->snapshot_valid = 1;
}

static void mark_parked(NplThreadContext *context)
{
    if (context->parked_epoch == g_epoch) return;
    context->parked_epoch = g_epoch;
    g_parked_for_epoch++;
    pthread_cond_broadcast(&g_parked_cond);
}

void npl_safepoint_poll(void)
{
    NplThreadContext *context = npl_ctx();
    /* Cancellation is observed at the same polls as a world stop.  T2 wires
     * the raise; here the observation is latched for the scheduler. The
     * request is exchanged atomically with npl_thread_request_cancel's
     * store, which happens under g_coord_lock. */
    if (__atomic_exchange_n(&context->cancellation_requested, 0,
                            __ATOMIC_ACQ_REL)) {
        context->cancel_observed = 1;
        /* Consume the poll sweep before terminating; only the target thread
         * has this flag, so the counter returns to zero exactly once. */
        npl_cancel_consumed();
        /* T2 wires the cooperative termination here; it may longjmp out of
         * the thread (and excludes this thread from the mutator count first).
         * No coordinator lock is held here. */
        npl_safepoint_cancelled();
    }
    if (!npl_gc_stop_requested)
        return;

    /* marker lives in this frame, which stays live across the condvar wait,
     * so the published stack range includes every older NPL C frame. */
    uintptr_t marker = (uintptr_t)(void *)&context;
    pthread_mutex_lock(&g_coord_lock);
    /* A wrapped blocking call already published a snapshot and is counted in
     * g_blocked_mutators for every epoch. It must not also park here: that
     * would satisfy the stop quorum with a thread the collector already
     * excluded, and the poll's epilogue would clear the published snapshot. */
    if (!g_stop_requested || context->blocked) {
        pthread_mutex_unlock(&g_coord_lock);
        return;
    }
    publish_snapshot(context, marker);
    context->parked = 1;
    mark_parked(context);
    while (g_stop_requested && context->parked_epoch == g_epoch)
        pthread_cond_wait(&g_resume_cond, &g_coord_lock);
    context->parked = 0;
    context->snapshot_valid = 0;
    context->parked_epoch = 0;
    pthread_mutex_unlock(&g_coord_lock);
}

void npl_gc_stop_world(void)
{
    pthread_mutex_lock(&g_coord_lock);
    g_epoch++;
    g_parked_for_epoch = 0;
    g_stop_requested = 1;
    refresh_stop_flag();
    __sync_synchronize();
    /* The collector is active but does not park.  Blocked mutators are
     * already published and count as parked for every epoch.  Recompute the
     * expectation on every wake: a mutator may detach (or become blocked)
     * while we wait, and npl_coordinator_detach_internal broadcasts this
     * condvar so the collector observes the smaller set. */
    for (;;) {
        int expected = g_active_mutators - 1 - g_blocked_mutators;
        if (expected < 0) expected = 0;
        if (g_parked_for_epoch >= expected) break;
        pthread_cond_wait(&g_parked_cond, &g_coord_lock);
    }
    pthread_mutex_unlock(&g_coord_lock);
}

void npl_gc_start_world(void)
{
    pthread_mutex_lock(&g_coord_lock);
    g_stop_requested = 0;
    refresh_stop_flag();
    __sync_synchronize();
    /* No wait: a long blocking wait must not delay the resume.  The next
     * stop uses a fresh epoch, so a mutator that has not yet acknowledged
     * this resume cannot be mistaken for parked in the new epoch. */
    pthread_cond_broadcast(&g_resume_cond);
    pthread_mutex_unlock(&g_coord_lock);
}

static void mark_one_stack(NplThreadContext *context, void *data)
{
    (void)data;
    if (!context->attached || !context->parked || !context->snapshot_valid)
        return;
    npl_gc_mark_words_internal(context->saved_stack_pointer,
                               context->stack_bottom);
    uintptr_t begin = (uintptr_t)(void *)&context->registers;
    npl_gc_mark_words_internal(begin, begin + sizeof(context->registers));
}

void npl_gc_mark_parked_stacks(void)
{
    npl_thread_foreach(mark_one_stack, NULL);
}

/* ---- Exclusion and safe blocked-state transitions ---- */

/* Invariant: g_blocked_mutators counts only threads still included in
 * g_active_mutators. A GC-excluded mutator must not reduce the stop-world
 * expectation (active - 1 - blocked) twice. */
void npl_gc_exclude(void)
{
    NplThreadContext *context = npl_ctx();
    pthread_mutex_lock(&g_coord_lock);
    if (!context->gc_excluded) {
        context->gc_excluded = 1;
        g_active_mutators--;
        if (context->blocked_counted) {
            context->blocked_counted = 0;
            g_blocked_mutators--;
        }
    }
    pthread_cond_broadcast(&g_parked_cond);
    pthread_mutex_unlock(&g_coord_lock);
}

void npl_gc_include(void)
{
    NplThreadContext *context = npl_ctx();
    pthread_mutex_lock(&g_coord_lock);
    /* A returning thread waits for an active collection before it touches
     * the heap or invalidates its snapshot. */
    while (g_stop_requested)
        pthread_cond_wait(&g_resume_cond, &g_coord_lock);
    if (context->gc_excluded) {
        context->gc_excluded = 0;
        g_active_mutators++;
        if (context->blocked) {
            context->blocked_counted = 1;
            g_blocked_mutators++;
        }
    }
    pthread_mutex_unlock(&g_coord_lock);
}

void npl_gc_block_begin(void)
{
    NplThreadContext *context = npl_ctx();
    uintptr_t marker = (uintptr_t)(void *)&context;
    pthread_mutex_lock(&g_coord_lock);
    publish_snapshot(context, marker);
    context->blocked = 1;
    context->parked = 1;
    context->parked_epoch = 0;
    if (!context->gc_excluded) {
        context->blocked_counted = 1;
        g_blocked_mutators++;
    }
    pthread_cond_broadcast(&g_parked_cond);
    pthread_mutex_unlock(&g_coord_lock);
}

void npl_gc_block_end(void)
{
    NplThreadContext *context = npl_ctx();
    pthread_mutex_lock(&g_coord_lock);
    /* A returning thread waits for an active collection before touching
     * managed state.  Its snapshot stays valid until then. */
    while (g_stop_requested)
        pthread_cond_wait(&g_resume_cond, &g_coord_lock);
    if (context->blocked) {
        context->blocked = 0;
        context->parked = 0;
        context->snapshot_valid = 0;
        if (context->blocked_counted) {
            context->blocked_counted = 0;
            g_blocked_mutators--;
        }
    }
    pthread_mutex_unlock(&g_coord_lock);
}

/* ---- Cooperative cancellation ---- */

void npl_thread_request_cancel(void *target)
{
    NplThreadContext *context = (NplThreadContext *)target;
    if (!context) return;
    pthread_mutex_lock(&g_coord_lock);
    __atomic_store_n(&context->cancellation_requested, 1, __ATOMIC_RELEASE);
    /* Only one contribution per context: duplicate termination requests must
     * not leak g_cancel_pending increments. A fresh context owes nothing, so
     * an unrelated uncaught raise cannot consume another thread's sweep. */
    if (!context->cancel_owed) {
        context->cancel_owed = 1;
        g_cancel_pending++;
    }
    refresh_stop_flag();
    /* Wake runtime-managed blocking waits so they can observe the request. */
    pthread_cond_broadcast(&g_resume_cond);
    pthread_mutex_unlock(&g_coord_lock);
}

/* Idempotent: a cancelled thread may reach this from a generated poll or
 * from a blocking wait that woke up. */
void npl_cancel_consumed(void)
{
    NplThreadContext *context = npl_ctx();
    pthread_mutex_lock(&g_coord_lock);
    if (context->cancel_owed) {
        context->cancel_owed = 0;
        if (g_cancel_pending > 0) g_cancel_pending--;
        refresh_stop_flag();
    }
    pthread_mutex_unlock(&g_coord_lock);
}

int npl_thread_cancel_pending(void)
{
    return npl_ctx()->cancel_observed;
}

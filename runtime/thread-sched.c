/*
 * thread-sched.c — T2 (SRFI 18) object layer and scheduler state.
 *
 * Heap objects (NplThread / NplMutex / NplCondvar / NplTime /
 * NplThreadException) carry NplVal fields that the GC visits; the native
 * pthread state beside them is never scanned. One scheduler lock and
 * condvar serialize all state transitions and waits; a thread blocked on
 * either is parked for the GC via npl_gc_block_begin_at/end.
 */
#define _POSIX_C_SOURCE 200809L

#include "npl.h"
#include "thread.h"
#include "thread-sched.h"
#include <math.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

pthread_mutex_t npl_sched_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t npl_sched_cond = PTHREAD_COND_INITIALIZER;

/* A plain pthread_mutex_lock waiter cannot park for a world stop, so the
 * collector would wait forever for a thread blocked behind a safepoint poll
 * inside the lock holder. Trylock + poll keeps every waiter at a poll site
 * (the same pattern as runtime/shared-lock.c). */
void npl_sched_lock_enter(void)
{
    while (pthread_mutex_trylock(&npl_sched_lock) != 0) {
        npl_safepoint_poll();
        sched_yield();
    }
}

void npl_sched_lock_leave(void)
{
    pthread_mutex_unlock(&npl_sched_lock);
}

/* ---- tagging ---- */

static NplVal boxed(void *header)
{
    return (NplVal)(uintptr_t)header | TAG_PTR;
}

NplVal npl_boxed_thread(void *header) { return boxed(header); }

/* ---- monotonic time ---- */

static double monotonic_seconds(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        npl_primitive_error("current-time", "clock unavailable", npl_nil());
    return (double)now.tv_sec + (double)now.tv_nsec / 1e9;
}

static void deadline_from(double seconds, struct timespec *deadline)
{
    /* Clamp, but do not turn a representable far-future absolute time into
     * an already-expired deadline (seconds > 1e9 used to become 0). */
    if (!(seconds >= 0)) seconds = 0;
    if (seconds > 1e9) seconds = 1e9;
    deadline->tv_sec = (time_t)seconds;
    deadline->tv_nsec = (long)((seconds - (double)deadline->tv_sec) * 1e9);
    if (deadline->tv_nsec < 0) deadline->tv_nsec = 0;
    if (deadline->tv_nsec >= 1000000000L) {
        deadline->tv_sec++;
        deadline->tv_nsec -= 1000000000L;
    }
}

/* Relative deadlines must be based on the same clock as the elapsed-time
 * check in wait_loop (CLOCK_MONOTONIC). Normalize negative/NaN values to
 * "now" instead of producing a timespec with a negative tv_nsec. */
static void relative_deadline(double seconds, struct timespec *deadline)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    if (!(seconds > 0)) {
        *deadline = now;
        return;
    }
    if (seconds > 1e9) seconds = 1e9;
    time_t whole = (time_t)seconds;
    long frac = (long)((seconds - (double)whole) * 1e9);
    deadline->tv_sec = now.tv_sec + whole;
    deadline->tv_nsec = now.tv_nsec + frac;
    if (deadline->tv_nsec >= 1000000000L) {
        deadline->tv_sec++;
        deadline->tv_nsec -= 1000000000L;
    }
}

/* Decode an SRFI 18 timeout: #f = no deadline, BOX_TIME = absolute, a real
 * number = relative seconds. Returns 1 on success, 0 on a bad type. */
int npl_thread_timeout(NplVal timeout, struct timespec *deadline,
                       int *has_deadline, const char *who)
{
    if (npl_is_false(timeout)) {
        *has_deadline = 0;
        return 1;
    }
    *has_deadline = 1;
    /* Absolute time objects are not real numbers; check them first. */
    if (npl_is_ptr(timeout) && npl_header(timeout)->tag == BOX_TIME) {
        deadline_from(((NplTime *)npl_header(timeout))->seconds, deadline);
        return 1;
    }
    /* SRFI 18 accepts any real number: fixnum, flonum, exact rational or
     * bignum. npl_real_double converts all of them; complex numbers are not
     * valid timeouts. */
    if (npl_number_p(timeout) == npl_true() && !npl_is_complex(timeout)) {
        relative_deadline(npl_real_double(timeout), deadline);
        return 1;
    }
    npl_primitive_error(who, "expected a timeout (time, real, or #f)",
                        timeout);
    return 0;
}

double npl_thread_real(NplVal value, const char *who)
{
    if (npl_number_p(value) == npl_true() && !npl_is_complex(value))
        return npl_real_double(value);
    npl_primitive_error(who, "expected a real number", value);
    return 0;
}

/* ---- object constructors ---- */

static void init_header(NplHeader *header, uint32_t tag)
{
    header->tag = tag;
    header->flags = 0;
}

NplVal npl_thread_make(NplVal thunk, NplVal name, NplVal parameters)
{
    NplThread *thread = (NplThread *)npl_alloc_object(sizeof(NplThread));
    init_header(&thread->header, BOX_THREAD);
    thread->name = name;
    thread->specific = npl_unspecified();
    thread->end_result = npl_unspecified();
    thread->end_exception = npl_false();
    thread->owned_mutexes = npl_nil();
    thread->thunk = thunk;
    thread->parameters = parameters;
    memset(&thread->handle, 0, sizeof(thread->handle));
    memset(&thread->terminate_jump, 0, sizeof(thread->terminate_jump));
    thread->state = NPL_THREAD_NEW;
    thread->started = 0;
    thread->cancel_requested = 0;
    thread->wake = 0;
    thread->wait_kind = NPL_WAIT_NONE;
    thread->wait_target = NULL;
    thread->has_native = 0;
    thread->registered = 0;
    thread->armed = 0;
    thread->context = NULL;
    return boxed(thread);
}

NplVal npl_mutex_make(NplVal name)
{
    NplMutex *mutex = (NplMutex *)npl_alloc_object(sizeof(NplMutex));
    init_header(&mutex->header, BOX_MUTEX);
    mutex->name = name;
    mutex->specific = npl_unspecified();
    mutex->owner = npl_false();
    mutex->waiters = npl_nil();
    mutex->state = NPL_MUTEX_UNLOCKED_NOT_ABANDONED;
    return boxed(mutex);
}

NplVal npl_condvar_make(NplVal name)
{
    NplCondvar *cv = (NplCondvar *)npl_alloc_object(sizeof(NplCondvar));
    init_header(&cv->header, BOX_CONDVAR);
    cv->name = name;
    cv->specific = npl_unspecified();
    cv->waiters = npl_nil();
    return boxed(cv);
}

NplVal npl_thread_time(double seconds)
{
    NplTime *time = (NplTime *)npl_alloc_object(sizeof(NplTime));
    init_header(&time->header, BOX_TIME);
    time->seconds = seconds;
    return boxed(time);
}

NplVal npl_thread_exception(int kind, NplVal reason)
{
    NplThreadException *exc = (NplThreadException *)
        npl_alloc_object(sizeof(NplThreadException));
    init_header(&exc->header, BOX_THREAD_EXCEPTION);
    exc->kind = kind;
    exc->reason = reason;
    return boxed(exc);
}

/* ---- predicates / accessors ---- */

static int tagged(NplVal value, uint32_t tag)
{
    return npl_is_ptr(value) && npl_header(value)->tag == tag;
}

static void require(NplVal value, uint32_t tag, const char *who)
{
    if (!tagged(value, tag))
        npl_primitive_error(who, "wrong type of argument", value);
}

NplVal npl_thread_p(NplVal value)
{
    return tagged(value, BOX_THREAD) ? npl_true() : npl_false();
}
NplVal npl_mutex_p(NplVal value)
{
    return tagged(value, BOX_MUTEX) ? npl_true() : npl_false();
}
NplVal npl_condvar_p(NplVal value)
{
    return tagged(value, BOX_CONDVAR) ? npl_true() : npl_false();
}
NplVal npl_time_p(NplVal value)
{
    return tagged(value, BOX_TIME) ? npl_true() : npl_false();
}

NplVal npl_thread_name(NplVal value)
{
    require(value, BOX_THREAD, "thread-name");
    return ((NplThread *)npl_header(value))->name;
}
NplVal npl_thread_specific(NplVal value)
{
    require(value, BOX_THREAD, "thread-specific");
    return ((NplThread *)npl_header(value))->specific;
}
NplVal npl_thread_specific_set(NplVal value, NplVal object)
{
    require(value, BOX_THREAD, "thread-specific-set!");
    NplThread *thread = (NplThread *)npl_header(value);
    thread->specific = object;
    npl_gc_write_barrier(thread, object);
    return npl_unspecified();
}
NplVal npl_mutex_name(NplVal value)
{
    require(value, BOX_MUTEX, "mutex-name");
    return ((NplMutex *)npl_header(value))->name;
}
NplVal npl_mutex_specific(NplVal value)
{
    require(value, BOX_MUTEX, "mutex-specific");
    return ((NplMutex *)npl_header(value))->specific;
}
NplVal npl_mutex_specific_set(NplVal value, NplVal object)
{
    require(value, BOX_MUTEX, "mutex-specific-set!");
    NplMutex *mutex = (NplMutex *)npl_header(value);
    mutex->specific = object;
    npl_gc_write_barrier(mutex, object);
    return npl_unspecified();
}
NplVal npl_condvar_name(NplVal value)
{
    require(value, BOX_CONDVAR, "condition-variable-name");
    return ((NplCondvar *)npl_header(value))->name;
}
NplVal npl_condvar_specific(NplVal value)
{
    require(value, BOX_CONDVAR, "condition-variable-specific");
    return ((NplCondvar *)npl_header(value))->specific;
}
NplVal npl_condvar_specific_set(NplVal value, NplVal object)
{
    require(value, BOX_CONDVAR, "condition-variable-specific-set!");
    NplCondvar *cv = (NplCondvar *)npl_header(value);
    cv->specific = object;
    npl_gc_write_barrier(cv, object);
    return npl_unspecified();
}

NplVal npl_mutex_state(NplVal value)
{
    require(value, BOX_MUTEX, "mutex-state");
    NplMutex *mutex = (NplMutex *)npl_header(value);
    /* Snapshot state and owner under the scheduler lock; all writers hold it.
     * The symbol allocations happen after the lock is released. */
    npl_sched_lock_enter();
    int state = mutex->state;
    NplVal owner = mutex->owner;
    npl_sched_lock_leave();
    switch (state) {
    case NPL_MUTEX_LOCKED_OWNED: return owner;
    case NPL_MUTEX_LOCKED_NOT_OWNED: return npl_symbol("not-owned", 9);
    case NPL_MUTEX_UNLOCKED_ABANDONED: return npl_symbol("abandoned", 9);
    default: return npl_symbol("not-abandoned", 13);
    }
}

/* ---- time ---- */

NplVal npl_current_time(void) { return npl_thread_time(monotonic_seconds()); }

/* SRFI 18 current-exception-handler. The runtime stores handler frames, so
 * return the procedure of the innermost one; with no frame the primordial
 * thread's handler is unspecified. */
NplVal npl_current_exception_handler(void)
{
    NplVal chain = npl_current_handler();
    if (!npl_is_ptr(chain) || npl_header(chain)->tag != BOX_HANDLER_FRAME)
        return npl_unspecified();
    return ((NplHandlerFrame *)npl_header(chain))->procedure;
}

NplVal npl_time_seconds(NplVal value)
{
    require(value, BOX_TIME, "time->seconds");
    return npl_float(((NplTime *)npl_header(value))->seconds);
}

NplVal npl_seconds_time(NplVal value)
{
    return npl_thread_time(npl_thread_real(value, "seconds->time"));
}

/* ---- exception objects ---- */

static NplVal exception_p(NplVal value, int kind)
{
    if (!tagged(value, BOX_THREAD_EXCEPTION)) return npl_false();
    return ((NplThreadException *)npl_header(value))->kind == kind
               ? npl_true() : npl_false();
}

NplVal npl_join_timeout_exception_p(NplVal v)
{
    return exception_p(v, NPL_EXC_JOIN_TIMEOUT);
}
NplVal npl_abandoned_mutex_exception_p(NplVal v)
{
    return exception_p(v, NPL_EXC_ABANDONED_MUTEX);
}
NplVal npl_terminated_thread_exception_p(NplVal v)
{
    return exception_p(v, NPL_EXC_TERMINATED_THREAD);
}
NplVal npl_uncaught_exception_p(NplVal v)
{
    return exception_p(v, NPL_EXC_UNCAUGHT);
}
NplVal npl_uncaught_exception_reason(NplVal v)
{
    require(v, BOX_THREAD_EXCEPTION, "uncaught-exception-reason");
    NplThreadException *exc = (NplThreadException *)npl_header(v);
    if (exc->kind != NPL_EXC_UNCAUGHT)
        npl_primitive_error("uncaught-exception-reason",
                            "not an uncaught exception", v);
    return exc->reason;
}

/* ---- current thread ---- */

/* The primordial thread gets a thread object on first current-thread. Each
 * created thread publishes its own object in its context. Both are rooted
 * through the per-thread context, so a live thread object is always a root. */
NplVal npl_thread_current_object(void)
{
    NplThreadContext *context = npl_ctx();
    if (tagged(context->current_thread_object, BOX_THREAD))
        return context->current_thread_object;
    NplVal object = npl_thread_make(npl_nil(), npl_unspecified(), npl_nil());
    NplThread *thread = (NplThread *)npl_header(object);
    thread->state = NPL_THREAD_RUNNABLE;
    thread->started = 1;
    context->current_thread_object = object;
    return object;
}

/* ---- scheduler helpers ---- */

void npl_sched_wake_all(void)
{
    pthread_cond_broadcast(&npl_sched_cond);
}

/* On any termination the thread abandons the mutexes it owns: they become
 * unlocked/abandoned, and threads waiting to lock them are woken. Verify the
 * CURRENT owner first: a foreign unlock may have left a stale entry in this
 * thread's list while another thread now owns the mutex. */
void npl_sched_abandon_mutexes(NplThread *thread)
{
    NplVal self = boxed(thread);
    for (NplVal rest = thread->owned_mutexes;
         npl_is_ptr(rest) && npl_header(rest)->tag == BOX_PAIR;
         rest = npl_pair_ptr(rest)->cdr) {
        NplVal mutex_value = npl_pair_ptr(rest)->car;
        if (!tagged(mutex_value, BOX_MUTEX)) continue;
        NplMutex *mutex = (NplMutex *)npl_header(mutex_value);
        if (mutex->state == NPL_MUTEX_LOCKED_OWNED &&
            mutex->owner == self) {
            mutex->state = NPL_MUTEX_UNLOCKED_ABANDONED;
            mutex->owner = npl_false();
        }
    }
    thread->owned_mutexes = npl_nil();
}

/* Publish the terminated state and wake everyone waiting on this thread. */
void npl_sched_finish(NplThread *thread, int terminated)
{
    (void)terminated;
    thread->state = NPL_THREAD_TERMINATED;
    thread->wait_kind = NPL_WAIT_NONE;
    thread->wait_target = NULL;
    npl_sched_wake_all();
}

/* ---- cancellation observed at a safepoint ---- */

void npl_thread_finish_current(NplVal end_exception)
{
    NplThreadContext *context = npl_ctx();
    NplVal object = context->current_thread_object;
    if (!tagged(object, BOX_THREAD)) return;   /* primordial: latch only */
    NplThread *thread = (NplThread *)npl_header(object);
    if (!thread->has_native || thread->state == NPL_THREAD_TERMINATED) return;
    /* A blocking wait can reach here without a generated poll; consume the
     * pending sweep too (idempotent). */
    npl_cancel_consumed();
    /* Publish the exception BEFORE excluding this thread from the mutator
     * count: npl_gc_exclude() stops the collector from scanning this stack,
     * so a young exception object must already be reachable from the heap. */
    if (!npl_is_false(end_exception)) {
        thread->end_exception = end_exception;
        npl_gc_write_barrier(thread, end_exception);
    }
    npl_gc_exclude();
    npl_sched_lock_enter();
    /* An uncaught raise or cancellation can happen while this thread is
     * waiting on a mutex / condition variable; retire its waiter entry before
     * the state is published as terminated. */
    npl_sched_remove_waiter(thread, thread->wait_kind,
                            thread->wait_target);
    npl_sched_abandon_mutexes(thread);
    npl_sched_finish(thread, 1);
    npl_sched_lock_leave();
    /* Only the thread whose thread_entry called setjmp owns this jmp_buf;
     * longjmp unwinds to its own unified termination path. */
    longjmp(thread->terminate_jump, 1);
}

void npl_safepoint_cancelled(void)
{
    npl_thread_finish_current(
        npl_thread_exception(NPL_EXC_TERMINATED_THREAD, npl_unspecified()));
}

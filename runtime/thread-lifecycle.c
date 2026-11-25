/*
 * thread-lifecycle.c — T2 (SRFI 18) 1:1 pthread substrate.
 *
 * Thread creation, start, join, yield, sleep and cooperative termination;
 * mutexes, condition variables, abandoned mutexes and timeout handling.
 * One scheduler lock and condvar serialize every state transition; waits
 * park through the T1.3 safe blocked-state transition so the collector can
 * stop the world without waiting on a blocked mutator.
 */
#define _POSIX_C_SOURCE 200809L

#include "npl.h"
#include "thread.h"
#include "thread-sched.h"
#include <errno.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static NplVal boxed(void *header)
{
    return (NplVal)(uintptr_t)header | TAG_PTR;
}

static int tagged(NplVal value, uint32_t tag)
{
    return npl_is_ptr(value) && npl_header(value)->tag == tag;
}

static NplThread *require_thread(NplVal value, const char *who)
{
    if (!tagged(value, BOX_THREAD))
        npl_primitive_error(who, "expected a thread", value);
    return (NplThread *)npl_header(value);
}

static NplMutex *require_mutex(NplVal value, const char *who)
{
    if (!tagged(value, BOX_MUTEX))
        npl_primitive_error(who, "expected a mutex", value);
    return (NplMutex *)npl_header(value);
}

static NplCondvar *require_condvar(NplVal value, const char *who)
{
    if (!tagged(value, BOX_CONDVAR))
        npl_primitive_error(who, "expected a condition variable", value);
    return (NplCondvar *)npl_header(value);
}

static void raise_object(NplVal object)
{
    if (!npl_raise_from_primitive(object)) {
        npl_report_error_object(object);
        exit(1);
    }
    exit(1);
}

/* Waiter/owned lists are mutated under npl_sched_lock. npl_cons() polls for
 * a safepoint and may allocate, which is unsafe inside the scheduler lock:
 * a poll that parks there strands threads blocked on the lock (they cannot
 * park themselves) and deadlocks a world stop. Cons cells are therefore
 * allocated OUTSIDE the lock and linked in place, and removal never allocates. */
static NplVal list_cell(NplVal head)
{
    return npl_cons(head, npl_nil());
}

static void list_link(NplVal *head, void *owner, NplVal cell)
{
    npl_pair_ptr(cell)->cdr = *head;
    npl_gc_write_barrier(npl_pair_ptr(cell), *head);
    *head = cell;
    npl_gc_write_barrier(owner, cell);
}

static void list_unlink(NplVal *head, void *owner, NplVal item)
{
    NplVal previous = npl_nil();
    for (NplVal rest = *head;
         npl_is_ptr(rest) && npl_header(rest)->tag == BOX_PAIR;) {
        NplVal cell = rest;
        NplVal next = npl_pair_ptr(cell)->cdr;
        if (npl_pair_ptr(cell)->car == item) {
            if (npl_is_nil(previous)) {
                *head = next;
                npl_gc_write_barrier(owner, next);
            } else {
                npl_pair_ptr(previous)->cdr = next;
                npl_gc_write_barrier(npl_pair_ptr(previous), next);
            }
            return;
        }
        previous = cell;
        rest = next;
    }
}

static int list_contains(NplVal list, NplVal item)
{
    for (NplVal rest = list;
         npl_is_ptr(rest) && npl_header(rest)->tag == BOX_PAIR;
         rest = npl_pair_ptr(rest)->cdr)
        if (npl_pair_ptr(rest)->car == item) return 1;
    return 0;
}

/* Called under npl_sched_lock when a thread terminates while waiting. */
void npl_sched_remove_waiter(NplThread *self, int wait_kind,
                             void *wait_target)
{
    if (!wait_target) return;
    NplVal value = npl_boxed_thread(self);
    if (wait_kind == NPL_WAIT_MUTEX) {
        NplMutex *mutex = (NplMutex *)wait_target;
        list_unlink(&mutex->waiters, mutex, value);
    } else if (wait_kind == NPL_WAIT_CONDVAR) {
        NplCondvar *cv = (NplCondvar *)wait_target;
        list_unlink(&cv->waiters, cv, value);
    }
}

/* ---- dynamic-environment inheritance ---- */

/* A new thread inherits the creator's bindings under its own ownership.
 * npl_clone_parameter_bindings (dynamic-context.c) preserves chain direction
 * (head = innermost binding), so nested parameterize keeps the inner value;
 * the old head-to-tail/prepend loop reversed the chain and made an outer
 * shadowed binding win. Parameter objects stay shared (T1.3 COW). */

/* ---- initial continuation and exception handler ---- */

/* SRFI 18: the initial continuation stores its result in the end-result
 * field of the (then) current thread and terminates it. Resolve the thread
 * at call time: this closure sits at the bottom of the CPS chain, so a
 * continuation captured on one thread and invoked on another reaches it on
 * the invoking thread. */
static NplVal thread_done(NplVal environment, int64_t argc, NplVal *argv)
{
    (void)environment;
    (void)argc;
    NplVal self = npl_thread_current_object();
    NplThread *thread = (NplThread *)npl_header(self);
    thread->end_result = argv[0];
    npl_gc_write_barrier(thread, argv[0]);
    /* Returning with no pending tail request ends the *current* thread's
     * outermost trampoline; thread_entry's unified termination path then
     * abandons its mutexes, marks it terminated and wakes joiners. */
    return npl_nil();
}

/* SRFI 18: the initial exception handler records an uncaught-exception
 * object in the (then) current thread and terminates it. It never longjmps
 * to a jmp_buf captured from another (possibly dead) thread. */
static NplVal thread_uncaught(NplVal environment, int64_t argc, NplVal *argv)
{
    (void)environment;
    (void)argc;
    NplVal exc = npl_thread_exception(NPL_EXC_UNCAUGHT, argv[0]);
    npl_thread_finish_current(exc);
    /* The primordial thread has no setjmp target: fall through so raise
     * reports the error object and exits, as it did before. */
    return npl_nil();
}

/* ---- startup rooting handshake (runtime/thread-start-root.c) ----
 * The NplThread object is rooted independently of the creator's root stack
 * between pthread_create and the worker publishing its own context root. */

/* ---- pthread entry ---- */

/* Boot the thread's thunk through a real trampoline so non-closure procedures
 * (parameter objects) can dispatch through npl_tail_request_closure. */
static NplVal thread_boot(NplVal environment, int64_t argc, NplVal *argv)
{
    npl_tail_request_closure(environment, argc, argv);
    return npl_nil();
}

static void *thread_entry(void *argument)
{
    NplThread *thread = (NplThread *)argument;
    NplVal self = boxed(thread);
    /* pthread_create makes a joinable pthread; nobody joins a worker, so
     * detach it to release its native resources on exit. */
    (void)pthread_detach(pthread_self());
    uintptr_t marker = 0;
    /* The top of an NPL thread's C stack is this frame (pthread's start
     * frames above hold no NplVal), so this is the exact conservative bound. */
    NplThreadContext *context = npl_thread_attach_exact(
        (uintptr_t)(void *)&marker + sizeof(marker));
    context->current_thread_object = self;
    thread->context = context;
    /* The object is now rooted by this thread's own context; drop the
     * independent startup root so a cancelled creator cannot strand it. */
    npl_pending_thread_remove(self);
    /* Release the creator's startup handshake: from now on this object is
     * rooted by this thread's own context. */
    __sync_synchronize();
    __sync_fetch_and_add(&thread->registered, 1);

    /* SRFI 18: inherit the dynamic environment, but with a fresh initial
     * exception handler and an empty dynamic-wind stack. The initial
     * continuation and initial handler act on the (then) current thread, so
     * their closures are thread-agnostic (environment = nil) and resolve
     * npl_ctx() when invoked; the initial handler frame is marked so a
     * captured chain can replace it with the invoking thread's own. */
    context->current_parameters =
        npl_clone_parameter_bindings(thread->parameters);
    context->current_wind = npl_nil();
    context->current_handler = npl_nil();
    NplVal handler = npl_make_closure(thread_uncaught, npl_nil(), 2);
    context->initial_handler = npl_handler_push_initial(handler);

    if (setjmp(thread->terminate_jump) == 0) {
        /* The jump target is live now. Publish `armed` under the scheduler
         * lock so npl_thread_terminate can start forwarding cancellations to
         * the coordinator flag; before this point a forwarded request could
         * longjmp on a still-zeroed jmp_buf. Then replay any request that
         * arrived during the startup window (before context was published). */
        npl_sched_lock_enter();
        thread->armed = 1;
        int cancelled = thread->cancel_requested;
        npl_sched_lock_leave();
        if (cancelled) npl_safepoint_cancelled();
        NplVal continuation = npl_make_closure(thread_done, npl_nil(), 1);
        NplVal arguments[1] = { continuation };
        (void)npl_trampoline(thread_boot, thread->thunk, 1, arguments);
    }

    /* Unified termination: normal return, uncaught raise (longjmp), or
     * cooperative cancellation (longjmp). A longjmp may have skipped frames
     * holding the shared-object lock. */
    npl_shared_release_all();
    /* A termination request can arrive after this thread's last poll. The
     * terminator sets cancel_requested under npl_sched_lock and waits for
     * TERMINATED, so resolve the pending request under that same lock BEFORE
     * publishing termination; otherwise thread-join! returns the normal
     * result instead of raising a terminated-thread exception. Allocate the
     * exception outside the lock. */
    NplVal terminated_exception =
        npl_thread_exception(NPL_EXC_TERMINATED_THREAD, npl_unspecified());
    npl_gc_root(&terminated_exception);
    npl_sched_lock_enter();
    if (thread->cancel_requested && npl_is_false(thread->end_exception)) {
        thread->end_exception = terminated_exception;
        npl_gc_write_barrier(thread, terminated_exception);
    }
    thread->armed = 0;
    npl_sched_abandon_mutexes(thread);
    npl_sched_finish(thread, 1);
    npl_sched_lock_leave();
    npl_gc_unroot(&terminated_exception);

    /* A longjmp may have skipped trampoline epilogues; free their argument
     * buffers, then drop every pointer to those dead C frames. */
    npl_trampoline_release_abandoned();
    context->sync_frame = NULL;
    context->escape_depth = 0;
    context->current_handler = npl_nil();
    context->current_wind = npl_nil();
    context->current_parameters = npl_nil();
    npl_thread_detach();
    return NULL;
}

/* ---- wait helper ---- */

struct wait_predicate {
    int (*ready)(NplThread *self, void *data);
    void *data;
};

/* npl_sched_cond is a default PTHREAD_COND_INITIALIZER, so timed waits use
 * CLOCK_REALTIME, while every NPL deadline (npl_thread_timeout) is computed
 * with CLOCK_MONOTONIC. Convert the remaining monotonic delay into an
 * absolute realtime deadline at wait time. This is portable to Darwin, which
 * has no pthread_condattr_setclock, and avoids treating a monotonic
 * seconds-since-boot value as an already-expired Unix-epoch deadline. */
static void realtime_deadline(const struct timespec *deadline,
                              struct timespec *out)
{
    struct timespec mono, real;
    clock_gettime(CLOCK_MONOTONIC, &mono);
    clock_gettime(CLOCK_REALTIME, &real);
    time_t sec = deadline->tv_sec - mono.tv_sec;
    long nsec = deadline->tv_nsec - mono.tv_nsec;
    if (nsec < 0) { sec--; nsec += 1000000000L; }
    if (sec < 0) { sec = 0; nsec = 0; }   /* deadline already passed */
    out->tv_sec = real.tv_sec + sec;
    out->tv_nsec = real.tv_nsec + nsec;
    if (out->tv_nsec >= 1000000000L) {
        out->tv_sec++;
        out->tv_nsec -= 1000000000L;
    }
}

static int wait_loop(NplThread *self, int (*ready)(NplThread *, void *),
                     void *data, struct timespec *deadline, int has_deadline)
{
    /* Publish the safe blocked snapshot before releasing exclusion. The
     * thread stays parked for GC for the whole wait. */
    npl_gc_block_begin();
    npl_sched_lock_enter();
    int timed_out = 0;
    int done = ready(self, data);
    while (!done && !self->cancel_requested) {
        if (has_deadline) {
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            if (now.tv_sec > deadline->tv_sec ||
                (now.tv_sec == deadline->tv_sec &&
                 now.tv_nsec >= deadline->tv_nsec)) {
                timed_out = 1;
                break;
            }
            struct timespec wait_deadline;
            realtime_deadline(deadline, &wait_deadline);
            int rc = pthread_cond_timedwait(&npl_sched_cond, &npl_sched_lock,
                                            &wait_deadline);
            if (rc == ETIMEDOUT) {
                /* A forward CLOCK_REALTIME jump can make the converted
                 * deadline expire early; recheck the monotonic deadline. */
                struct timespec check;
                clock_gettime(CLOCK_MONOTONIC, &check);
                if (check.tv_sec > deadline->tv_sec ||
                    (check.tv_sec == deadline->tv_sec &&
                     check.tv_nsec >= deadline->tv_nsec)) {
                    timed_out = 1;
                    break;
                }
                continue;
            }
        } else {
            pthread_cond_wait(&npl_sched_cond, &npl_sched_lock);
        }
        done = ready(self, data);
    }
    /* Capture the wait fields before clearing them: a cancellation longjmps
     * out of this function, so the waiter entry must be retired first. */
    int cancelled = self->cancel_requested;
    int saved_wait_kind = self->wait_kind;
    void *saved_wait_target = self->wait_target;
    self->wake = 0;
    self->wait_kind = NPL_WAIT_NONE;
    self->wait_target = NULL;
    npl_sched_lock_leave();
    npl_gc_block_end();
    if (cancelled) {
        npl_sched_lock_enter();
        npl_sched_remove_waiter(self, saved_wait_kind, saved_wait_target);
        npl_sched_lock_leave();
        npl_safepoint_cancelled();
    }
    if (timed_out && !done) return 0;   /* 0 = timeout */
    return done ? 1 : 0;
}

/* ---- thread primitives ---- */

NplVal npl_thread_make_prim(NplVal thunk, NplVal name)
{
    /* The library-level make-thread must reject a non-procedure thunk here:
     * the error is catchable and happens before the thread object exists.
     * npl_thread_make itself is also used for the primordial thread object,
     * whose thunk is nil, so validation belongs in this primitive. Parameter
     * objects are procedures too (npl_procedure_p / npl_apply accept them). */
    if (npl_is_false(npl_procedure_p(thunk)))
        npl_primitive_error("make-thread", "expected a procedure", thunk);
    /* SRFI 18: the child inherits the dynamic environment from the creating
     * thread AT make-thread time. Snapshot it now so later parent mutations
     * between make-thread and thread-start! cannot leak into the child. */
    NplVal snapshot = npl_clone_parameter_bindings(
        npl_ctx()->current_parameters);
    npl_gc_root(&snapshot);
    NplVal result = npl_thread_make(thunk, name, snapshot);
    npl_gc_unroot(&snapshot);
    return result;
}

NplVal npl_thread_current(void) { return npl_thread_current_object(); }

NplVal npl_thread_start(NplVal value)
{
    NplThread *thread = require_thread(value, "thread-start!");
    /* Hold a root across the startup window: between pthread_create and the
     * new thread publishing its own context root, no other root is
     * guaranteed. npl_safepoint_poll in the handshake keeps this thread
     * parkable while it waits. */
    npl_gc_root(&value);
    /* Independent of the creator's root stack: if the creator is cancelled
     * while waiting for `registered`, this process-wide root survives. */
    npl_pending_thread_add(value);
    /* Allocate the possible termination exception outside the scheduler lock:
     * npl_thread_exception() polls at allocation entry. */
    NplVal terminated_exception =
        npl_thread_exception(NPL_EXC_TERMINATED_THREAD, npl_unspecified());
    npl_gc_root(&terminated_exception);
    npl_sched_lock_enter();
    if (thread->state != NPL_THREAD_NEW) {
        npl_sched_lock_leave();
        npl_pending_thread_remove(value);
        npl_gc_unroot(&terminated_exception);
        npl_gc_unroot(&value);
        npl_primitive_error("thread-start!", "thread is not new", value);
    }
    if (thread->cancel_requested) {
        /* not-started -> terminated: no poll site exists, complete here. */
        thread->end_exception = terminated_exception;
        npl_gc_write_barrier(thread, terminated_exception);
        npl_sched_finish(thread, 1);
        npl_sched_lock_leave();
        npl_pending_thread_remove(value);
        npl_gc_unroot(&terminated_exception);
        npl_gc_unroot(&value);
        return value;
    }
    thread->state = NPL_THREAD_RUNNABLE;
    thread->started = 1;
    /* Publish has_native before pthread_create: the worker may run user code
     * (and observe a cancellation) before the creator returns from create. */
    thread->has_native = 1;
    int rc = pthread_create(&thread->handle, NULL, thread_entry,
                            (void *)npl_header(value));
    if (rc != 0) {
        thread->state = NPL_THREAD_NEW;
        thread->started = 0;
        thread->has_native = 0;
        npl_sched_lock_leave();
        npl_pending_thread_remove(value);
        npl_gc_unroot(&terminated_exception);
        npl_gc_unroot(&value);
        npl_primitive_error("thread-start!", "pthread_create failed", value);
    }
    npl_sched_lock_leave();
    npl_gc_unroot(&terminated_exception);
    while (!__sync_fetch_and_add(&thread->registered, 0)) {
        npl_safepoint_poll();
        sched_yield();
    }
    npl_gc_unroot(&value);
    return value;
}

static int join_ready(NplThread *self, void *data)
{
    (void)self;
    NplThread *target = (NplThread *)data;
    return target->state == NPL_THREAD_TERMINATED;
}

NplVal npl_thread_join(NplVal value, NplVal timeout, NplVal timeout_val,
                       NplVal supplied)
{
    NplThread *target = require_thread(value, "thread-join!");
    struct timespec deadline;
    int has_deadline = 0;
    int count = (int)npl_fixnum_value(supplied);
    if (count >= 1)
        (void)npl_thread_timeout(timeout, &deadline, &has_deadline,
                                 "thread-join!");
    NplThread *self = (NplThread *)npl_header(npl_thread_current_object());
    self->wait_kind = NPL_WAIT_JOIN;
    self->wait_target = target;
    int result = wait_loop(self, join_ready, target, &deadline, has_deadline);
    if (result == 0) {
        if (count >= 2) return timeout_val;
        raise_object(npl_thread_exception(NPL_EXC_JOIN_TIMEOUT,
                                          npl_unspecified()));
    }
    if (!npl_is_false(target->end_exception))
        raise_object(target->end_exception);
    return target->end_result;
}

NplVal npl_thread_yield(void)
{
    npl_safepoint_poll();
    sched_yield();
    return npl_unspecified();
}

static int sleep_ready(NplThread *self, void *data)
{
    (void)self;
    (void)data;
    return 0;   /* a sleep only ends at its deadline */
}

NplVal npl_thread_sleep(NplVal timeout)
{
    struct timespec deadline;
    int has_deadline = 0;
    if (!npl_thread_timeout(timeout, &deadline, &has_deadline,
                            "thread-sleep!") || !has_deadline)
        npl_primitive_error("thread-sleep!", "timeout must not be #f", timeout);
    NplThread *self = (NplThread *)npl_header(npl_thread_current_object());
    self->wait_kind = NPL_WAIT_SLEEP;
    (void)wait_loop(self, sleep_ready, NULL, &deadline, 1);
    return npl_unspecified();
}

static int terminated_ready(NplThread *self, void *data)
{
    (void)self;
    NplThread *target = (NplThread *)data;
    return target->state == NPL_THREAD_TERMINATED;
}

NplVal npl_thread_terminate(NplVal value)
{
    NplThread *target = require_thread(value, "thread-terminate!");
    NplVal current = npl_thread_current_object();
    NplThread *self = (NplThread *)npl_header(current);
    if (target == self) {
        /* Self-termination does not return. The primordial thread has no
         * thread_entry setjmp target, so end the process: SRFI 18 terminates
         * all threads when the primordial thread terminates. */
        self->end_exception =
            npl_thread_exception(NPL_EXC_TERMINATED_THREAD, npl_unspecified());
        npl_gc_write_barrier(self, self->end_exception);
        npl_gc_exclude();
        npl_sched_lock_enter();
        npl_sched_abandon_mutexes(self);
        npl_sched_finish(self, 1);
        npl_sched_lock_leave();
        if (!self->has_native) exit(0);
        longjmp(self->terminate_jump, 1);
    }
    /* Allocate the possible termination exception outside the scheduler lock. */
    NplVal terminated_exception =
        npl_thread_exception(NPL_EXC_TERMINATED_THREAD, npl_unspecified());
    npl_gc_root(&terminated_exception);
    npl_sched_lock_enter();
    if (target->state == NPL_THREAD_TERMINATED) {
        npl_sched_lock_leave();
        npl_gc_unroot(&terminated_exception);
        return npl_unspecified();
    }
    if (target->state == NPL_THREAD_NEW) {
        /* No poll site on a never-started thread: complete here. */
        target->end_exception = terminated_exception;
        npl_gc_write_barrier(target, terminated_exception);
        npl_sched_finish(target, 1);
        npl_sched_lock_leave();
        npl_gc_unroot(&terminated_exception);
        return npl_unspecified();
    }
    if (!target->has_native) {
        /* The primordial thread has no generated poll sites, no context for
         * npl_thread_request_cancel and no jump target, so a remote
         * thread-terminate! would wait forever. SRFI 18: terminating the
         * primordial thread terminates all threads. */
        target->end_exception = terminated_exception;
        npl_gc_write_barrier(target, terminated_exception);
        npl_sched_finish(target, 1);
        npl_sched_wake_all();
        npl_sched_lock_leave();
        npl_gc_unroot(&terminated_exception);
        exit(0);
    }
    target->cancel_requested = 1;
    target->wake = 1;
    /* Wake the coordinator's cancellation flag too, but ONLY once the worker
     * has armed its terminate_jump. A request forwarded earlier would make a
     * startup-time poll longjmp through a zeroed jmp_buf (UB). Before arming,
     * thread_entry's post-setjmp replay delivers the request. */
    if (target->armed && target->context)
        npl_thread_request_cancel((void *)target->context);
    npl_sched_wake_all();
    npl_sched_lock_leave();
    npl_gc_unroot(&terminated_exception);
    /* SRFI 18: termination must be acknowledged before returning. */
    self->wait_kind = NPL_WAIT_JOIN;
    self->wait_target = target;
    struct timespec unused;
    (void)wait_loop(self, terminated_ready, target, &unused, 0);
    return npl_unspecified();
}

/* ---- mutexes and condition variables ---- */

NplVal npl_mutex_make_prim(NplVal name) { return npl_mutex_make(name); }
NplVal npl_condvar_make_prim(NplVal name) { return npl_condvar_make(name); }

static int mutex_free(NplThread *self, void *data)
{
    (void)self;
    NplMutex *mutex = (NplMutex *)data;
    return mutex->state == NPL_MUTEX_UNLOCKED_NOT_ABANDONED ||
           mutex->state == NPL_MUTEX_UNLOCKED_ABANDONED;
}

static void wake_one_mutex_waiter(NplMutex *mutex)
{
    NplVal rest = mutex->waiters;
    if (npl_is_ptr(rest) && npl_header(rest)->tag == BOX_PAIR) {
        NplVal waiter = npl_pair_ptr(rest)->car;
        if (tagged(waiter, BOX_THREAD))
            ((NplThread *)npl_header(waiter))->wake = 1;
    }
    npl_sched_wake_all();
}

NplVal npl_mutex_lock(NplVal value, NplVal timeout, NplVal thread_value,
                      NplVal supplied)
{
    NplMutex *mutex = require_mutex(value, "mutex-lock!");
    struct timespec deadline;
    int has_deadline = 0;
    int count = (int)npl_fixnum_value(supplied);
    if (count >= 1)
        (void)npl_thread_timeout(timeout, &deadline, &has_deadline,
                                 "mutex-lock!");
    NplVal self_value = npl_thread_current_object();
    NplVal owner_thread = self_value;
    int not_owned = 0;
    NplThread *requested_owner = NULL;
    if (count >= 2) {
        if (npl_is_false(thread_value)) {
            not_owned = 1;
        } else {
            requested_owner = require_thread(thread_value, "mutex-lock!");
            owner_thread = thread_value;
        }
    }
    NplThread *self = (NplThread *)npl_header(self_value);

    int abandoned = 0;
    for (;;) {
        /* Allocate cons cells outside the scheduler lock (see list_link). */
        NplVal waiter_cell = list_cell(self_value);
        NplVal owned_cell = list_cell(value);
        npl_sched_lock_enter();
        int free = mutex_free(self, mutex);
        if (!free) {
            /* Non-recursive: even the owner waits. */
            if (!list_contains(mutex->waiters, self_value))
                list_link(&mutex->waiters, mutex, waiter_cell);
            npl_sched_lock_leave();
            /* Publish the descriptor only while a wait is active. wait_loop
             * clears it on return, and an uncontended acquisition never sets
             * it, so no stale wait_target (an untraced void*) can survive to
             * a later cancellation's npl_sched_remove_waiter. */
            self->wait_kind = NPL_WAIT_MUTEX;
            self->wait_target = mutex;
            int result = wait_loop(self, mutex_free, mutex, &deadline,
                                   has_deadline);
            if (result == 0) {
                /* Timed out: retire this thread's waiter entry under the lock
                 * instead of leaving it on the mutex forever. */
                npl_sched_lock_enter();
                list_unlink(&mutex->waiters, mutex, self_value);
                npl_sched_lock_leave();
                return npl_false();
            }
            continue;
        }
        abandoned = mutex->state == NPL_MUTEX_UNLOCKED_ABANDONED;
        /* A termination race on an explicitly requested owner is resolved
         * INSIDE the acquisition transaction: never clear a busy mutex's
         * ownership just because the requested owner has terminated. */
        if (requested_owner &&
            requested_owner->state == NPL_THREAD_TERMINATED) {
            /* SRFI 18: an abandoned-mutex exception is raised only if the
             * mutex was unlocked/abandoned BEFORE this state change. A
             * terminated requested owner merely leaves it unlocked/abandoned
             * and mutex-lock! returns #t. */
            mutex->state = NPL_MUTEX_UNLOCKED_ABANDONED;
            mutex->owner = npl_false();
            npl_sched_lock_leave();
            if (abandoned)
                raise_object(npl_thread_exception(NPL_EXC_ABANDONED_MUTEX,
                                                  npl_unspecified()));
            return npl_true();
        }
        list_unlink(&mutex->waiters, mutex, self_value);
        if (not_owned) {
            mutex->state = NPL_MUTEX_LOCKED_NOT_OWNED;
            mutex->owner = npl_false();
        } else {
            mutex->state = NPL_MUTEX_LOCKED_OWNED;
            mutex->owner = owner_thread;
            npl_gc_write_barrier(mutex, owner_thread);
            NplThread *owner = (NplThread *)npl_header(owner_thread);
            if (!list_contains(owner->owned_mutexes, value))
                list_link(&owner->owned_mutexes, owner, owned_cell);
        }
        npl_sched_lock_leave();
        break;
    }
    if (abandoned)
        raise_object(npl_thread_exception(NPL_EXC_ABANDONED_MUTEX,
                                          npl_unspecified()));
    return npl_true();
}

static int cv_ready(NplThread *self, void *data)
{
    (void)data;
    return self->wake;
}

NplVal npl_mutex_unlock(NplVal value, NplVal condvar_value, NplVal timeout,
                        NplVal supplied)
{
    NplMutex *mutex = require_mutex(value, "mutex-unlock!");
    struct timespec deadline;
    int has_deadline = 0;
    int count = (int)npl_fixnum_value(supplied);
    NplCondvar *cv = NULL;
    if (count >= 1 && !npl_is_false(condvar_value))
        cv = require_condvar(condvar_value, "mutex-unlock!");
    if (count >= 2)
        (void)npl_thread_timeout(timeout, &deadline, &has_deadline,
                                 "mutex-unlock!");

    NplVal self_value = npl_thread_current_object();
    NplThread *self = (NplThread *)npl_header(self_value);
    /* A condvar waiter cell is allocated outside the scheduler lock. */
    NplVal waiter_cell = cv ? list_cell(self_value) : npl_nil();
    npl_sched_lock_enter();
    self->wake = 0;
    /* A plain unlock is not a wait; clear any stale descriptor so a later
     * cancellation cannot dereference an untraced, collected wait_target. */
    if (!cv) {
        self->wait_kind = NPL_WAIT_NONE;
        self->wait_target = NULL;
    }
    /* Unlocking an unlocked mutex or one owned by any thread is not an error.
     * Remove the mutex from its ACTUAL owner's list, not the caller's: a
     * foreign unlock must not leave a stale entry in the previous owner's
     * owned_mutexes (it would later abandon a mutex owned by someone else). */
    NplVal previous_owner = mutex->owner;
    if (tagged(previous_owner, BOX_THREAD)) {
        NplThread *owner = (NplThread *)npl_header(previous_owner);
        list_unlink(&owner->owned_mutexes, owner, value);
    } else {
        list_unlink(&self->owned_mutexes, self, value);
    }
    mutex->state = NPL_MUTEX_UNLOCKED_NOT_ABANDONED;
    mutex->owner = npl_false();
    if (cv) {
        if (!list_contains(cv->waiters, self_value))
            list_link(&cv->waiters, cv, waiter_cell);
        self->wait_kind = NPL_WAIT_CONDVAR;
        self->wait_target = cv;
    }
    wake_one_mutex_waiter(mutex);
    npl_sched_lock_leave();

    if (cv) {
        int result = wait_loop(self, cv_ready, cv, &deadline, has_deadline);
        npl_sched_lock_enter();
        list_unlink(&cv->waiters, cv, self_value);
        npl_sched_lock_leave();
        if (result == 0) return npl_false();
    }
    return npl_true();
}

NplVal npl_condvar_signal(NplVal value)
{
    NplCondvar *cv = require_condvar(value, "condition-variable-signal!");
    npl_sched_lock_enter();
    NplVal rest = cv->waiters;
    if (npl_is_ptr(rest) && npl_header(rest)->tag == BOX_PAIR) {
        NplVal waiter = npl_pair_ptr(rest)->car;
        if (tagged(waiter, BOX_THREAD)) {
            ((NplThread *)npl_header(waiter))->wake = 1;
            /* Consume this waiter so a second signal wakes the NEXT one
             * rather than setting wake=1 on an already-woken thread. */
            list_unlink(&cv->waiters, cv, waiter);
        }
    }
    npl_sched_wake_all();
    npl_sched_lock_leave();
    return npl_unspecified();
}

NplVal npl_condvar_broadcast(NplVal value)
{
    NplCondvar *cv = require_condvar(value, "condition-variable-broadcast!");
    npl_sched_lock_enter();
    for (NplVal rest = cv->waiters;
         npl_is_ptr(rest) && npl_header(rest)->tag == BOX_PAIR;
         rest = npl_pair_ptr(rest)->cdr) {
        NplVal waiter = npl_pair_ptr(rest)->car;
        if (tagged(waiter, BOX_THREAD))
            ((NplThread *)npl_header(waiter))->wake = 1;
    }
    npl_sched_wake_all();
    npl_sched_lock_leave();
    return npl_unspecified();
}

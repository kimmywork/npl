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

static NplVal list_cons(NplVal head, NplVal tail)
{
    return npl_cons(head, tail);
}

static NplVal list_remove(NplVal list, NplVal item)
{
    NplVal result = npl_nil();
    NplVal tail = result;
    for (NplVal rest = list;
         npl_is_ptr(rest) && npl_header(rest)->tag == BOX_PAIR;
         rest = npl_pair_ptr(rest)->cdr) {
        if (npl_pair_ptr(rest)->car == item) continue;
        NplVal cell = npl_cons(npl_pair_ptr(rest)->car, npl_nil());
        if (npl_is_nil(result)) result = cell;
        else {
            npl_pair_ptr(tail)->cdr = cell;
            npl_gc_write_barrier(npl_pair_ptr(tail), cell);
        }
        tail = cell;
    }
    return result;
}

static int list_contains(NplVal list, NplVal item)
{
    for (NplVal rest = list;
         npl_is_ptr(rest) && npl_header(rest)->tag == BOX_PAIR;
         rest = npl_pair_ptr(rest)->cdr)
        if (npl_pair_ptr(rest)->car == item) return 1;
    return 0;
}

/* ---- dynamic-environment inheritance ---- */

/* Copy the creator's parameter-binding chain into fresh thread-owned nodes.
 * Parameter objects stay shared; only bindings are per-thread (T1.3 COW). */
static NplVal clone_parameter_chain(NplVal chain, NplThreadContext *context)
{
    NplVal result = npl_nil();
    for (NplVal rest = chain;
         npl_is_ptr(rest) && npl_header(rest)->tag == BOX_PARAMETER_BINDING;) {
        NplParameterBinding *source =
            (NplParameterBinding *)npl_header(rest);
        NplParameterBinding *copy = (NplParameterBinding *)
            npl_alloc_object(sizeof(NplParameterBinding));
        copy->header.tag = BOX_PARAMETER_BINDING;
        copy->header.flags = 0;
        copy->parent = result;
        copy->parameter = source->parameter;
        copy->value = source->value;
        copy->owner = (uintptr_t)context;
        result = boxed(copy);
        rest = source->parent;
    }
    return result;
}

/* ---- initial continuation and exception handler ---- */

static NplVal thread_done(NplVal environment, int64_t argc, NplVal *argv)
{
    (void)argc;
    NplThread *thread = (NplThread *)npl_header(environment);
    thread->end_result = argv[0];
    npl_gc_write_barrier(thread, argv[0]);
    return npl_nil();
}

static NplVal thread_uncaught(NplVal environment, int64_t argc, NplVal *argv)
{
    (void)argc;
    NplThread *thread = (NplThread *)npl_header(environment);
    NplVal exc = npl_thread_exception(NPL_EXC_UNCAUGHT, argv[0]);
    thread->end_exception = exc;
    npl_gc_write_barrier(thread, exc);
    longjmp(thread->terminate_jump, 1);
}

/* ---- pthread entry ---- */

static void *thread_entry(void *argument)
{
    NplThread *thread = (NplThread *)argument;
    NplVal self = boxed(thread);
    uintptr_t marker = 0;
    /* The top of an NPL thread's C stack is this frame (pthread's start
     * frames above hold no NplVal), so this is the exact conservative bound. */
    NplThreadContext *context = npl_thread_attach_exact(
        (uintptr_t)(void *)&marker + sizeof(marker));
    context->current_thread_object = self;
    thread->context = context;
    /* Release the creator's startup handshake: from now on this object is
     * rooted by this thread's own context. */
    __sync_synchronize();
    __sync_fetch_and_add(&thread->registered, 1);

    /* SRFI 18: inherit the dynamic environment, but with a fresh initial
     * exception handler and an empty dynamic-wind stack. */
    context->current_parameters = clone_parameter_chain(thread->parameters,
                                                        context);
    context->current_wind = npl_nil();
    context->current_handler = npl_nil();
    NplVal handler = npl_make_closure(thread_uncaught, self, 2);
    (void)npl_handler_push(handler);

    if (setjmp(thread->terminate_jump) == 0) {
        NplVal continuation = npl_make_closure(thread_done, self, 1);
        NplVal arguments[1] = { continuation };
        (void)npl_apply(thread->thunk, 1, arguments);
    }

    /* Unified termination: normal return, uncaught raise (longjmp), or
     * cooperative cancellation (longjmp). Abandon owned mutexes and publish
     * the terminated state before join/terminate! can observe it. */
    pthread_mutex_lock(&npl_sched_lock);
    npl_sched_abandon_mutexes(thread);
    npl_sched_finish(thread, 1);
    pthread_mutex_unlock(&npl_sched_lock);

    /* A longjmp may have skipped trampoline epilogues; those frames are on
     * the dead part of this C stack, so drop every pointer to them. */
    context->tail_context = NULL;
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

static int wait_loop(NplThread *self, int (*ready)(NplThread *, void *),
                     void *data, struct timespec *deadline, int has_deadline)
{
    /* Publish the safe blocked snapshot before releasing exclusion. The
     * thread stays parked for GC for the whole wait. */
    npl_gc_block_begin();
    pthread_mutex_lock(&npl_sched_lock);
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
            int rc = pthread_cond_timedwait(&npl_sched_cond, &npl_sched_lock,
                                            deadline);
            if (rc == ETIMEDOUT) { timed_out = 1; break; }
        } else {
            pthread_cond_wait(&npl_sched_cond, &npl_sched_lock);
        }
        done = ready(self, data);
    }
    self->wake = 0;
    self->wait_kind = NPL_WAIT_NONE;
    self->wait_target = NULL;
    pthread_mutex_unlock(&npl_sched_lock);
    npl_gc_block_end();
    if (self->cancel_requested) npl_safepoint_cancelled();
    if (timed_out && !done) return 0;   /* 0 = timeout */
    return done ? 1 : 0;
}

/* ---- thread primitives ---- */

NplVal npl_thread_make_prim(NplVal thunk, NplVal name)
{
    return npl_thread_make(thunk, name, npl_ctx()->current_parameters);
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
    pthread_mutex_lock(&npl_sched_lock);
    if (thread->state != NPL_THREAD_NEW) {
        pthread_mutex_unlock(&npl_sched_lock);
        npl_gc_unroot(&value);
        npl_primitive_error("thread-start!", "thread is not new", value);
    }
    if (thread->cancel_requested) {
        /* not-started -> terminated: no poll site exists, complete here. */
        thread->end_exception =
            npl_thread_exception(NPL_EXC_TERMINATED_THREAD, npl_unspecified());
        npl_sched_finish(thread, 1);
        pthread_mutex_unlock(&npl_sched_lock);
        npl_gc_unroot(&value);
        return value;
    }
    thread->state = NPL_THREAD_RUNNABLE;
    thread->started = 1;
    int rc = pthread_create(&thread->handle, NULL, thread_entry,
                            (void *)npl_header(value));
    if (rc != 0) {
        thread->state = NPL_THREAD_NEW;
        thread->started = 0;
        pthread_mutex_unlock(&npl_sched_lock);
        npl_gc_unroot(&value);
        npl_primitive_error("thread-start!", "pthread_create failed", value);
    }
    thread->has_native = 1;
    pthread_mutex_unlock(&npl_sched_lock);
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
        /* Self-termination does not return. */
        self->end_exception =
            npl_thread_exception(NPL_EXC_TERMINATED_THREAD, npl_unspecified());
        npl_gc_exclude();
        pthread_mutex_lock(&npl_sched_lock);
        npl_sched_abandon_mutexes(self);
        npl_sched_finish(self, 1);
        pthread_mutex_unlock(&npl_sched_lock);
        longjmp(self->terminate_jump, 1);
    }
    pthread_mutex_lock(&npl_sched_lock);
    if (target->state == NPL_THREAD_TERMINATED) {
        pthread_mutex_unlock(&npl_sched_lock);
        return npl_unspecified();
    }
    if (target->state == NPL_THREAD_NEW) {
        /* No poll site on a never-started thread: complete here. */
        target->end_exception =
            npl_thread_exception(NPL_EXC_TERMINATED_THREAD, npl_unspecified());
        npl_sched_finish(target, 1);
        pthread_mutex_unlock(&npl_sched_lock);
        return npl_unspecified();
    }
    target->cancel_requested = 1;
    target->wake = 1;
    /* Wake the coordinator's cancellation flag too: generated safepoint
     * polls observe NplThreadContext.cancellation_requested, not the thread
     * object's field. A runtime-managed blocking wait is woken as well. */
    if (target->context)
        npl_thread_request_cancel((void *)target->context);
    npl_sched_wake_all();
    pthread_mutex_unlock(&npl_sched_lock);
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
    NplVal owner_thread = npl_thread_current_object();
    int not_owned = 0;
    if (count >= 2) {
        if (npl_is_false(thread_value)) {
            not_owned = 1;
        } else {
            NplThread *given = require_thread(thread_value, "mutex-lock!");
            owner_thread = thread_value;
            if (given->state == NPL_THREAD_TERMINATED) {
                pthread_mutex_lock(&npl_sched_lock);
                mutex->state = NPL_MUTEX_UNLOCKED_ABANDONED;
                mutex->owner = npl_false();
                pthread_mutex_unlock(&npl_sched_lock);
                raise_object(npl_thread_exception(NPL_EXC_ABANDONED_MUTEX,
                                                  npl_unspecified()));
            }
        }
    }
    NplThread *self = (NplThread *)npl_header(npl_thread_current_object());
    self->wait_kind = NPL_WAIT_MUTEX;
    self->wait_target = mutex;

    int abandoned = 0;
    for (;;) {
        pthread_mutex_lock(&npl_sched_lock);
        int free = mutex_free(self, mutex);
        if (!free) {
            /* Non-recursive: even the owner waits. */
            if (!list_contains(mutex->waiters, npl_thread_current_object()))
                mutex->waiters = list_cons(npl_thread_current_object(),
                                           mutex->waiters);
            pthread_mutex_unlock(&npl_sched_lock);
            int result = wait_loop(self, mutex_free, mutex, &deadline,
                                   has_deadline);
            if (result == 0) return npl_false();
            continue;
        }
        abandoned = mutex->state == NPL_MUTEX_UNLOCKED_ABANDONED;
        if (list_contains(mutex->waiters, npl_thread_current_object()))
            mutex->waiters = list_remove(mutex->waiters,
                                         npl_thread_current_object());
        if (not_owned) {
            mutex->state = NPL_MUTEX_LOCKED_NOT_OWNED;
            mutex->owner = npl_false();
        } else {
            mutex->state = NPL_MUTEX_LOCKED_OWNED;
            mutex->owner = owner_thread;
            NplThread *owner = (NplThread *)npl_header(owner_thread);
            if (!list_contains(owner->owned_mutexes, value))
                owner->owned_mutexes = list_cons(value, owner->owned_mutexes);
        }
        pthread_mutex_unlock(&npl_sched_lock);
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

    NplThread *self = (NplThread *)npl_header(npl_thread_current_object());
    self->wake = 0;
    pthread_mutex_lock(&npl_sched_lock);
    /* Unlocking an unlocked mutex or one owned by any thread is not an error. */
    mutex->state = NPL_MUTEX_UNLOCKED_NOT_ABANDONED;
    mutex->owner = npl_false();
    if (list_contains(self->owned_mutexes, value))
        self->owned_mutexes = list_remove(self->owned_mutexes, value);
    if (cv) {
        if (!list_contains(cv->waiters, npl_thread_current_object()))
            cv->waiters = list_cons(npl_thread_current_object(), cv->waiters);
        self->wait_kind = NPL_WAIT_CONDVAR;
        self->wait_target = cv;
    }
    wake_one_mutex_waiter(mutex);
    pthread_mutex_unlock(&npl_sched_lock);

    if (cv) {
        int result = wait_loop(self, cv_ready, cv, &deadline, has_deadline);
        pthread_mutex_lock(&npl_sched_lock);
        cv->waiters = list_remove(cv->waiters, npl_thread_current_object());
        pthread_mutex_unlock(&npl_sched_lock);
        if (result == 0) return npl_false();
    }
    return npl_true();
}

NplVal npl_condvar_signal(NplVal value)
{
    NplCondvar *cv = require_condvar(value, "condition-variable-signal!");
    pthread_mutex_lock(&npl_sched_lock);
    NplVal rest = cv->waiters;
    if (npl_is_ptr(rest) && npl_header(rest)->tag == BOX_PAIR) {
        NplVal waiter = npl_pair_ptr(rest)->car;
        if (tagged(waiter, BOX_THREAD))
            ((NplThread *)npl_header(waiter))->wake = 1;
    }
    npl_sched_wake_all();
    pthread_mutex_unlock(&npl_sched_lock);
    return npl_unspecified();
}

NplVal npl_condvar_broadcast(NplVal value)
{
    NplCondvar *cv = require_condvar(value, "condition-variable-broadcast!");
    pthread_mutex_lock(&npl_sched_lock);
    for (NplVal rest = cv->waiters;
         npl_is_ptr(rest) && npl_header(rest)->tag == BOX_PAIR;
         rest = npl_pair_ptr(rest)->cdr) {
        NplVal waiter = npl_pair_ptr(rest)->car;
        if (tagged(waiter, BOX_THREAD))
            ((NplThread *)npl_header(waiter))->wake = 1;
    }
    npl_sched_wake_all();
    pthread_mutex_unlock(&npl_sched_lock);
    return npl_unspecified();
}

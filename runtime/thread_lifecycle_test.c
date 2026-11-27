/*
 * thread_lifecycle_test.c — T2 substrate acceptance: 1:1 pthread start/join,
 * timeout with timeout-val, cooperative termination, mutex lock/unlock and
 * condition-variable signal/broadcast. C closures stand in for NPL thunks so
 * this harness needs no compiler or evaluator.
 */
#define _POSIX_C_SOURCE 200809L

#include "npl.h"
#include "thread-sched.h"
#include <stdio.h>
#include <unistd.h>

static int failures = 0;

#define CHECK(condition, message)                                            \
    do {                                                                      \
        if (!(condition)) {                                                   \
            fprintf(stderr, "FAIL: %s\n", (message));                         \
            failures++;                                                       \
        }                                                                     \
    } while (0)

/* A CPS thunk: argv[0] is the continuation; tail-call it with the result. */
static NplVal return_123(NplVal environment, int64_t argc, NplVal *argv)
{
    (void)environment;
    (void)argc;
    NplVal result = npl_fixnum(123);
    npl_tail_request_closure(argv[0], 1, &result);
    return npl_nil();
}

/* A long-running NPL computation: polls at safepoints so a cancellation
 * request can be observed without pthread_cancel. The loop is unbounded;
 * termination longjmps out of it. alarm() below guards the gate. */
static NplVal spin(NplVal environment, int64_t argc, NplVal *argv)
{
    (void)environment;
    (void)argc;
    for (;;) npl_safepoint_poll();
    return npl_nil();
}

static NplVal make_started(NplGenericFunction function, NplVal *thread_out)
{
    NplVal thunk = npl_make_closure(function, npl_nil(), 1);
    npl_gc_root(&thunk);
    NplVal thread = npl_thread_make_prim(thunk, npl_false());
    *thread_out = thread;
    npl_gc_unroot(&thunk);
    (void)npl_thread_start(thread);
    return thread;
}

int main(void)
{
    /* Watchdog: if a wait never completes, do not hang the acceptance gate. */
#ifndef _WIN32
    alarm(30);
#endif
    npl_runtime_init();

    /* Normal join result. */
    NplVal t1;
    make_started(return_123, &t1);
    npl_gc_root(&t1);
    NplVal result = npl_thread_join(t1, npl_false(), npl_false(),
                                    npl_fixnum(0));
    CHECK(result == npl_fixnum(123), "join returns the thunk result");
    NplVal current = npl_thread_current();
    CHECK(npl_thread_p(current) == npl_true(), "current-thread is a thread");
    CHECK(npl_thread_current() == current, "current-thread is eq? to itself");
    npl_gc_unroot(&t1);

    /* Join timeout with timeout-val returns the supplied value. */
    NplVal t2;
    make_started(spin, &t2);
    npl_gc_root(&t2);
    NplVal timed = npl_thread_join(t2, npl_fixnum(0), npl_fixnum(456),
                                   npl_fixnum(2));
    CHECK(timed == npl_fixnum(456), "join timeout returns timeout-val");
    /* Cooperative termination is acknowledged before it returns. */
    (void)npl_thread_terminate(t2);
    CHECK(((NplThread *)npl_header(t2))->state == NPL_THREAD_TERMINATED,
          "thread-terminate! completes before returning");
    CHECK(npl_terminated_thread_exception_p(
              ((NplThread *)npl_header(t2))->end_exception) == npl_true(),
          "terminated thread has a terminated-thread-exception");
    npl_gc_unroot(&t2);

    /* Mutex lock / unlock and state. */
    NplVal mutex = npl_mutex_make_prim(npl_false());
    npl_gc_root(&mutex);
    CHECK(npl_mutex_lock(mutex, npl_false(), npl_false(),
                         npl_fixnum(0)) == npl_true(), "mutex-lock! succeeds");
    CHECK(npl_mutex_state(mutex) == npl_thread_current(),
          "mutex-state reports the owner thread");
    (void)npl_mutex_unlock(mutex, npl_false(), npl_false(), npl_fixnum(0));
    CHECK(npl_mutex_state(mutex) == npl_symbol("not-abandoned", 13),
          "mutex-unlock! makes the mutex unlocked/not-abandoned");
    /* A mutex lock with an already-reached timeout still succeeds when free. */
    CHECK(npl_mutex_lock(mutex, npl_fixnum(0), npl_false(),
                         npl_fixnum(1)) == npl_true(),
          "already-reached timeout checks sync conditions first");
    (void)npl_mutex_unlock(mutex, npl_false(), npl_false(), npl_fixnum(0));
    npl_gc_unroot(&mutex);

    /* Condition-variable signal/broadcast on an empty CV. */
    NplVal cv = npl_condvar_make_prim(npl_false());
    npl_gc_root(&cv);
    (void)npl_condvar_signal(cv);
    (void)npl_condvar_broadcast(cv);
    CHECK(npl_condvar_p(cv) == npl_true(), "condition-variable?");
    npl_gc_unroot(&cv);

    /* sleep with an already-reached timeout does not block; yield returns. */
    (void)npl_thread_sleep(npl_fixnum(0));
    (void)npl_thread_yield();

    /* A collection after all threads terminated must keep objects alive. */
    npl_gc_collect();

    if (failures) {
        fprintf(stderr, "thread lifecycle: %d failure(s)\n", failures);
        return 1;
    }
    puts("PASS: SRFI 18 lifecycle, join timeout, termination, mutex, CV");
    return 0;
}

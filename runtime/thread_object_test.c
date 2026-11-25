/*
 * thread_object_test.c — T2 object-layer acceptance: SRFI 18 heap objects,
 * predicates/accessors, time conversion, exception objects, current-thread
 * identity, and GC visibility of the rooted thread object.
 */
#define _POSIX_C_SOURCE 200809L

#include "npl.h"
#include "thread-sched.h"
#include <stdio.h>

static int failures = 0;

#define CHECK(condition, message)                                            \
    do {                                                                      \
        if (!(condition)) {                                                   \
            fprintf(stderr, "FAIL: %s\n", (message));                         \
            failures++;                                                       \
        }                                                                     \
    } while (0)

int main(void)
{
    npl_runtime_init();

    NplVal thread = npl_thread_current_object();
    npl_gc_root(&thread);
    CHECK(npl_thread_p(thread) == npl_true(), "current-thread is a thread");
    CHECK(npl_thread_current_object() == thread,
          "current-thread is eq? to itself");
    CHECK(npl_thread_p(npl_fixnum(1)) == npl_false(),
          "thread? rejects a fixnum");
    /* Use a heap object (not a fixnum) so a dropped GC visit of the field is
     * observable: an immediate cannot prove the collector traced it. */
    NplVal specific = npl_cons(npl_fixnum(77), npl_fixnum(78));
    (void)npl_thread_specific_set(thread, specific);
    CHECK(npl_thread_specific(thread) == specific,
          "thread-specific-set!/thread-specific");
    specific = npl_nil();   /* drop the C-stack reference before collecting */

    NplVal mutex = npl_mutex_make(npl_false());
    npl_gc_root(&mutex);
    CHECK(npl_mutex_p(mutex) == npl_true(), "mutex? accepts a mutex");
    CHECK(npl_mutex_p(thread) == npl_false(), "mutex? rejects a thread");
    CHECK(npl_mutex_state(mutex) == npl_symbol("not-abandoned", 13),
          "a new mutex is unlocked/not-abandoned");
    (void)npl_mutex_specific_set(mutex, npl_fixnum(5));
    CHECK(npl_mutex_specific(mutex) == npl_fixnum(5),
          "mutex-specific-set!/mutex-specific");

    NplVal cv = npl_condvar_make(npl_false());
    npl_gc_root(&cv);
    CHECK(npl_condvar_p(cv) == npl_true(), "condition-variable? accepts a CV");
    (void)npl_condvar_specific_set(cv, npl_fixnum(6));
    CHECK(npl_condvar_specific(cv) == npl_fixnum(6),
          "condition-variable-specific-set!/specific");

    NplVal now = npl_current_time();
    npl_gc_root(&now);
    CHECK(npl_time_p(now) == npl_true(), "current-time returns a time object");
    CHECK(npl_time_p(npl_fixnum(1)) == npl_false(),
          "time? rejects a fixnum");
    double seconds = npl_guard_float(npl_time_seconds(now));
    NplVal rebuilt = npl_seconds_time(npl_float(seconds));
    npl_gc_root(&rebuilt);
    CHECK(npl_time_p(rebuilt) == npl_true(), "seconds->time returns a time");
    CHECK(npl_guard_float(npl_time_seconds(rebuilt)) == seconds,
          "time->seconds / seconds->time round trip");

    NplVal e1 = npl_thread_exception(NPL_EXC_JOIN_TIMEOUT, npl_unspecified());
    NplVal reason = npl_cons(npl_fixnum(42), npl_fixnum(43));
    NplVal e2 = npl_thread_exception(NPL_EXC_UNCAUGHT, reason);
    reason = npl_nil();
    npl_gc_root(&e1);
    npl_gc_root(&e2);
    CHECK(npl_join_timeout_exception_p(e1) == npl_true(),
          "join-timeout-exception?");
    CHECK(npl_join_timeout_exception_p(e2) == npl_false(),
          "join-timeout-exception? rejects another kind");
    CHECK(npl_uncaught_exception_p(e2) == npl_true(),
          "uncaught-exception?");
    CHECK(npl_is_ptr(npl_uncaught_exception_reason(e2)) &&
          npl_car(npl_uncaught_exception_reason(e2)) == npl_fixnum(42),
          "uncaught-exception-reason");
    CHECK(npl_terminated_thread_exception_p(e2) == npl_false(),
          "terminated-thread-exception? rejects another kind");

    /* A collection must keep the rooted thread object and all NplVal fields
     * of the new box types alive. */
    npl_gc_collect();
    CHECK(npl_thread_p(thread) == npl_true(), "thread survives collection");
    CHECK(npl_is_ptr(npl_thread_specific(thread)) &&
          npl_car(npl_thread_specific(thread)) == npl_fixnum(77),
          "thread-specific survives collection");
    CHECK(npl_mutex_state(mutex) == npl_symbol("not-abandoned", 13),
          "mutex survives collection");
    CHECK(npl_time_p(now) == npl_true(), "time survives collection");
    CHECK(npl_is_ptr(npl_uncaught_exception_reason(e2)) &&
          npl_car(npl_uncaught_exception_reason(e2)) == npl_fixnum(42),
          "exception reason survives collection");

    npl_gc_unroot(&e2);
    npl_gc_unroot(&e1);
    npl_gc_unroot(&rebuilt);
    npl_gc_unroot(&now);
    npl_gc_unroot(&cv);
    npl_gc_unroot(&mutex);
    npl_gc_unroot(&thread);

    if (failures) {
        fprintf(stderr, "thread object: %d failure(s)\n", failures);
        return 1;
    }
    puts("PASS: SRFI 18 object layer, predicates, time, exceptions, GC roots");
    return 0;
}

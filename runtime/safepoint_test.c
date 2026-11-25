/*
 * safepoint_test.c — T1.3 acceptance: concurrent mutation and stop-the-world
 * collection.  Several pthreads allocate NPL pairs while the main thread
 * repeatedly requests full collections.  One value is a registered root;
 * another is reachable ONLY from a parked thread's C stack, so it survives
 * only if the collector scans every parked context's published stack range.
 */

#define _POSIX_C_SOURCE 200809L

#include "npl.h"
#include "thread.h"
#include <pthread.h>
#include <stdio.h>

#define WORKERS 3
#define ITERS 20000
#define COLLECTS 200

static volatile int g_failures = 0;
static volatile int g_stack_failures = 0;
static volatile int g_root_failures = 0;
static volatile int g_ready = 0;
/* A parameter shared by every worker; each thread's parameter-set! must not
 * change the shared default seen by the primordial thread (T1.3 COW). */
static NplVal g_shared_parameter;

typedef struct { int id; } Worker;

static void *worker(void *argument)
{
    Worker *worker = (Worker *)argument;
    (void)npl_thread_attach();

    /* Publish the scan bound BEFORE the first allocation that can trigger a
     * collection: attach_context's bound is deeper (inside npl_thread_attach)
     * and would omit this frame's stack_only value. The slack covers the
     * start routine's frame above us. */
    volatile NplVal stack_only;
    (void)npl_gc_swap_stack_bottom(
        (uintptr_t)(void *)&stack_only + 4096);
    /* reachable only from this C stack (not registered as a root) */
    stack_only = npl_cons(npl_fixnum(1000 + worker->id), npl_fixnum(7));
    NplVal rooted = npl_cons(npl_fixnum(2000 + worker->id), npl_fixnum(9));
    npl_gc_root(&rooted);
    /* COW: this thread's parameter-set! shadows the shared default instead
     * of mutating it, so the other threads still see 7. */
    (void)npl_parameter_set(g_shared_parameter,
                            npl_fixnum(3000 + worker->id));
    if (npl_parameter_ref(g_shared_parameter) !=
        npl_fixnum(3000 + worker->id))
        g_failures++;
    /* Signal readiness only after the published bound covers stack_only;
     * npl_thread_count() becomes visible earlier than this. */
    __sync_fetch_and_add(&g_ready, 1);

    for (int i = 0; i < ITERS; i++) {
        /* Explicit poll plus the poll inside every npl_alloc_object. */
        npl_safepoint_poll();
        NplVal garbage = npl_cons(npl_fixnum(i), stack_only);
        if (npl_car(stack_only) != npl_fixnum(1000 + worker->id)) {
            g_stack_failures++;
            g_failures++;
        }
        if (npl_car(rooted) != npl_fixnum(2000 + worker->id)) {
            g_root_failures++;
            g_failures++;
        }
        (void)garbage;
    }

    npl_gc_unroot(&rooted);
    npl_thread_detach();
    return NULL;
}

int main(void)
{
    npl_runtime_init();
    g_shared_parameter = npl_make_parameter(npl_nil(), npl_fixnum(7));
    npl_gc_root(&g_shared_parameter);
    pthread_t threads[WORKERS];
    Worker workers[WORKERS];
    for (int i = 0; i < WORKERS; i++) {
        workers[i].id = i;
        if (pthread_create(&threads[i], NULL, worker, &workers[i]) != 0) {
            fprintf(stderr, "FAIL: pthread_create\n");
            return 1;
        }
    }
    /* Start collecting only when every mutator has registered AND published
     * its stack bound, so the first collection already scans their stacks. */
    while (g_ready < WORKERS) {
        /* A worker may need a collection to publish its bound; stay at a
         * poll site so a world stop cannot wait forever on this spin. */
        npl_safepoint_poll();
    }
    for (int i = 0; i < COLLECTS; i++)
        npl_gc_collect();
    /* pthread_join is an unwrapped C wait: keep GC excluded while blocked,
     * otherwise a worker-triggered collection would wait forever for this
     * thread (which has no poll site) to park. */
    npl_gc_exclude();
    for (int i = 0; i < WORKERS; i++)
        pthread_join(threads[i], NULL);
    npl_gc_include();
    if (npl_thread_count() != 1)
        g_failures++;
    /* The primordial thread never called parameter-set!, so it must still
     * see the shared default: no worker's update leaked across threads. */
    if (npl_parameter_ref(g_shared_parameter) != npl_fixnum(7))
        g_failures++;
    npl_gc_unroot(&g_shared_parameter);

    /* Cooperative cancellation is observed at a poll. */
    npl_thread_request_cancel((void *)npl_current_context());
    npl_safepoint_poll();
    if (!npl_thread_cancel_pending())
        g_failures++;

    /* Exclusion and safe blocked-state transitions roundtrip, then collect
     * once more so the coordinator's counts are exercised after them. */
    npl_gc_exclude();
    npl_gc_include();
    npl_gc_block_begin();
    npl_gc_block_end();
    npl_gc_collect();

    if (g_failures) {
        fprintf(stderr, "safepoint stress: %d failure(s) "
                "(stack=%d root=%d)\n",
                g_failures, g_stack_failures, g_root_failures);
        return 1;
    }
    puts("PASS: concurrent allocation, world-stop park/resume, parked stack "
         "scan, cancellation, blocking/exclusion");
    return 0;
}

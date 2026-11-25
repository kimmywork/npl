/*
 * virtual_thread_test.c — T2b substrate acceptance.
 *
 * Three virtual threads run on ONE carrier. Each suspends twice through the
 * one-shot scheduler continuation, so the carrier multiplexes them; each
 * keeps its own dynamic parameter binding across suspension; full collections
 * are forced while the other virtual threads are suspended, proving that the
 * heap tokens and their dynamic state are GC roots. Reaching the private CPS
 * root records the end-result without terminating the carrier.
 */
#define _POSIX_C_SOURCE 200809L

#include "npl.h"
#include "thread.h"
#include "thread-sched.h"
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define NTHREADS 3
#define NSTEPS 4

static volatile int failures;
static NplVal g_vts[NTHREADS];
static NplVal g_param;
static int g_progress[NTHREADS];
static int g_order[NTHREADS * NSTEPS];
static int g_order_count;
static pthread_mutex_t g_order_lock = PTHREAD_MUTEX_INITIALIZER;
static volatile int g_gc_done;
static volatile int g_gc_collections;

/* A second mutator forces full collections continuously while the carrier
 * multiplexes virtual threads. Suspended tokens then have no live C frame
 * on any stack and must survive purely through the virtual-thread records. */
static void *gc_worker(void *argument)
{
    (void)argument;
    uintptr_t marker = 0;
    (void)npl_thread_attach_exact(
        (uintptr_t)(void *)&marker + sizeof(marker) + 4096);
    while (!g_gc_done && g_gc_collections < 1000) {
        npl_gc_collect();
        __sync_fetch_and_add(&g_gc_collections, 1);
        /* Leave a window for an idle carrier to clear its blocked state;
         * a tight collect loop would starve npl_gc_block_end. */
        usleep(1000);
    }
    npl_thread_detach();
    return NULL;
}

static void fail(const char *what)
{
    fprintf(stderr, "FAIL: %s\n", what);
    __sync_fetch_and_add(&failures, 1);
}

static void record(int index)
{
    pthread_mutex_lock(&g_order_lock);
    if (g_order_count < NTHREADS * NSTEPS)
        g_order[g_order_count] = index;
    g_order_count++;
    pthread_mutex_unlock(&g_order_lock);
}

/* A binding chain stays length 1 when its owner is re-stamped on migration;
 * without re-localization every post-migration parameter-set! COW-clones a
 * node and the chain grows. */
static int parameter_chain_length(void)
{
    int length = 0;
    for (NplVal rest = npl_ctx()->current_parameters;
         npl_is_ptr(rest) &&
         npl_header(rest)->tag == BOX_PARAMETER_BINDING;
         rest = ((NplParameterBinding *)npl_header(rest))->parent)
        length++;
    return length;
}

/* One step of a virtual thread. The trailing argument is the resume value
 * (unspecified); the environment is the virtual thread's index. */
static NplVal step(NplVal environment, int64_t argc, NplVal *argv)
{
    (void)argc;
    (void)argv;
    int index = (int)npl_fixnum_value(environment);
    int step_index = __sync_fetch_and_add(&g_progress[index], 1);
    record(index);

    /* The virtual thread's own parameter binding must survive suspension and
     * must not be visible to any other virtual thread. */
    NplVal observed = npl_parameter_ref(g_param);
    if (!npl_is_fixnum(observed) ||
        npl_fixnum_value(observed) != 100 + index)
        fail("dynamic parameter isolation across virtual threads");
    if (npl_is_false(npl_vthread_current()))
        fail("current virtual thread not mounted");
    (void)npl_parameter_set(g_param, npl_fixnum(100 + index));
    if (parameter_chain_length() != 1)
        fail("parameter binding chain grew across carrier migration");

    /* Churn the heap so a missing GC edge is not masked by a still-clean
     * allocation region, then force a collection while the other virtual
     * threads' tokens are suspended. */
    for (int i = 0; i < 32; i++)
        (void)npl_cons(npl_fixnum(i), environment);
    npl_gc_collect();

    if (step_index + 1 < NSTEPS) {
        NplVal token = npl_make_closure(step, environment, 1);
        npl_vthread_park(token);
        return npl_nil();
    }

    NplVal result = npl_fixnum(index);
    NplVal root = npl_vthread_root();
    npl_tail_request_closure(root, 1, &result);
    return npl_nil();
}

static NplVal thunk(NplVal environment, int64_t argc, NplVal *argv)
{
    (void)argc;
    (void)argv;
    int index = (int)npl_fixnum_value(environment);
    (void)npl_parameter_set(g_param, npl_fixnum(100 + index));
    return step(environment, 0, NULL);
}

int main(int argc, char **argv)
{
    int carriers = (argc > 1) ? atoi(argv[1]) : 1;
    if (carriers < 1) carriers = 1;
    alarm(120);
    npl_runtime_init();

    npl_gc_root(&g_param);
    g_param = npl_make_parameter(npl_nil(), npl_fixnum(-1));

    for (int i = 0; i < NTHREADS; i++) {
        npl_gc_root(&g_vts[i]);
        NplVal environment = npl_fixnum(i);
        NplVal procedure = npl_make_closure(thunk, environment, 1);
        g_vts[i] = npl_vthread_make(procedure, npl_false(), npl_nil());
        if (npl_is_false(npl_vthread_p(g_vts[i])))
            fail("virtual-thread? predicate");
    }

    npl_virtual_thread_set_carriers(carriers);
    /* Enqueue the whole batch before any carrier starts: with one carrier
     * this gives deterministic round-robin; with M carriers it spreads the
     * initial batch across the per-carrier queues. */
    for (int i = 0; i < NTHREADS; i++) npl_vthread_start(g_vts[i]);

    pthread_t collector;
    if (pthread_create(&collector, NULL, gc_worker, NULL) != 0)
        fail("cannot start collector thread");
    npl_virtual_thread_run();

    for (int i = 0; i < NTHREADS; i++) {
        NplVal result = npl_vthread_join(g_vts[i]);
        if (!npl_is_fixnum(result) || npl_fixnum_value(result) != i)
            fail("end-result of virtual thread");
        if (!npl_is_false(npl_vthread_end_exception(g_vts[i])))
            fail("unexpected end-exception");
        if (npl_vthread_park_count(g_vts[i]) != NSTEPS - 1)
            fail("park count");
        if (npl_vthread_resume_count(g_vts[i]) != NSTEPS - 1)
            fail("one-shot resume count");
        /* With M > 1 every park hands the virtual thread to the next
         * carrier, so every resume is a migration. With one carrier there
         * is no migration at all. Both are deterministic. */
        int expected_migrations = carriers > 1 ? NSTEPS - 1 : 0;
        if (npl_vthread_migration_count(g_vts[i]) != expected_migrations)
            fail("cross-carrier migration count");
    }

    if (g_order_count != NTHREADS * NSTEPS)
        fail("total step count");
    int seen[NTHREADS] = { 0, 0, 0 };
    for (int i = 0; i < g_order_count && i < NTHREADS * NSTEPS; i++) {
        int index = g_order[i];
        if (index < 0 || index >= NTHREADS)
            fail("invalid virtual thread index in step log");
        else
            seen[index] = 1;
    }
    for (int i = 0; i < NTHREADS; i++)
        if (!seen[i]) fail("a virtual thread never ran");
    /* Ordering is only deterministic with a single carrier: with M carriers
     * each queue progresses independently. */
    if (carriers == 1) {
        for (int i = 0; i < NTHREADS; i++)
            if (g_order[i] != i)
                fail("virtual threads did not round-robin on one carrier");
    }

    g_gc_done = 1;
    /* pthread_join is an unwrapped C wait: keep GC excluded while blocked,
     * otherwise the collector's world stop waits for this thread forever. */
    npl_gc_exclude();
    (void)pthread_join(collector, NULL);
    npl_gc_include();
    if (g_gc_collections == 0) fail("no concurrent collections ran");

    npl_virtual_thread_shutdown();
    if (failures) {
        fprintf(stderr, "virtual threads: %d failure(s)\n", failures);
        return 1;
    }
    printf("PASS: %d carrier(s), one-shot tokens, dynamic-state isolation "
           "across migration, GC-surviving suspended tokens\n", carriers);
    return 0;
}

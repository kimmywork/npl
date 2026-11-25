/*
 * thread_test.c — T1 acceptance for the native TLS current context.
 *
 * Checks that the generated executable's TLS slot, the per-thread dynamic
 * chains, and the process-wide registry behave as the thread design requires,
 * and that a collection enumerates every registered thread's roots.  It links
 * libnpl_runtime.a directly, so standalone-slots.c supplies the two data
 * slots that a generated program would otherwise define.
 */

#define _POSIX_C_SOURCE 200809L

#include "npl.h"
#include "thread.h"
#include <pthread.h>
#include <stdio.h>

static int failures = 0;
static volatile int ready = 0;

#define CHECK(condition, message)                                             \
    do {                                                                      \
        if (!(condition)) {                                                   \
            fprintf(stderr, "FAIL: %s\n", (message));                         \
            failures++;                                                       \
        }                                                                     \
    } while (0)

typedef struct {
    uintptr_t stack_value;
    NplVal dynamic_value;      /* a fixnum, so it is not a heap pointer */
    volatile int *release;
    int ok;
} Worker;

static void *worker(void *argument)
{
    Worker *worker = (Worker *)argument;
    NplThreadContext *context = npl_thread_attach();
    if (!context) {
        worker->ok = 0;
        return NULL;
    }
    npl_stack_limit = worker->stack_value;
    context->current_parameters = worker->dynamic_value;
    context->current_wind = worker->dynamic_value;

    /* Stay registered while the main thread collects.  A runtime-managed
     * blocking wait publishes its roots and machine context first and stays
     * parked, so a world stop never waits on this spin (T1.3 safe blocked
     * transition). */
    npl_gc_block_begin();
    /* Only after the blocked-state publication is complete may the main
     * thread collect; otherwise the world stop would wait for a thread that
     * spins without a poll site. */
    __sync_fetch_and_add(&ready, 1);
    while (!*worker->release) {
        /* spin: the snapshot published above stays valid while parked */
    }
    npl_gc_block_end();

    if (npl_stack_limit != worker->stack_value ||
        npl_current_context() != context ||
        context->current_parameters != worker->dynamic_value ||
        context->current_wind != worker->dynamic_value ||
        !context->attached)
        worker->ok = 0;
    else
        worker->ok = 1;

    npl_thread_detach();
    return NULL;
}

int main(void)
{
    npl_runtime_init();
    NplThreadContext *main_context = npl_current_context();
    uintptr_t main_limit = npl_stack_limit;

    CHECK(main_context != NULL, "primordial thread registered");
    CHECK(main_context->attached == 1, "primordial context attached");
    CHECK(npl_thread_count() == 1, "one registered thread at init");
    CHECK(main_limit != 0, "stack limit initialized");

    /* A rooted heap object must survive a collection that walks every
     * registered context's root stack. */
    NplVal pair = npl_cons(npl_fixnum(11), npl_fixnum(22));
    npl_gc_root(&pair);

    volatile int release = 0;
    Worker a = { (uintptr_t)0x1111000, npl_fixnum(101), &release, 0 };
    Worker b = { (uintptr_t)0x2222000, npl_fixnum(202), &release, 0 };
    pthread_t ta, tb;
    if (pthread_create(&ta, NULL, worker, &a) != 0 ||
        pthread_create(&tb, NULL, worker, &b) != 0) {
        fprintf(stderr, "FAIL: pthread_create\n");
        return 1;
    }

    /* Wait until both workers are registered AND have published their
     * blocked snapshot, then collect on the main thread while they are live. */
    while (ready < 2) {
        /* spin */
    }
    npl_gc_collect();
    CHECK(npl_car(pair) == npl_fixnum(11), "root survives collection");
    CHECK(npl_stack_limit == main_limit, "main TLS slot unchanged by workers");
    CHECK(main_context->current_parameters == npl_nil(),
          "main dynamic state unchanged by workers");
    release = 1;

    pthread_join(ta, NULL);
    pthread_join(tb, NULL);
    CHECK(a.ok && b.ok, "workers keep independent TLS and dynamic state");
    CHECK(npl_thread_count() == 1, "detached workers unregistered");
    CHECK(npl_current_context() == main_context, "main context unchanged");
    npl_gc_unroot(&pair);

    if (failures) {
        fprintf(stderr, "thread context: %d failure(s)\n", failures);
        return 1;
    }
    puts("PASS: TLS current context, per-thread stack limit, attach/detach, "
         "GC root enumeration");
    return 0;
}

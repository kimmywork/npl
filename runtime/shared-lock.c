/*
 * shared-lock.c — one recursive lock for process-wide mutable native objects
 * (ports, FFI blocks, directory handles, the symbol intern table).
 *
 * A plain pthread_mutex_lock here would deadlock a world stop: a mutator
 * blocked on this lock is not at a poll site, so the collector would wait for
 * it forever. Callers instead loop on pthread_mutex_trylock with a safepoint
 * poll, so every waiter parks when a collection is requested.
 *
 * npl_primitive_error can longjmp out of a locked region; every longjmp
 * landing calls npl_shared_release_all, which drops the calling thread's
 * whole lock depth before the handler runs.
 */
#define _POSIX_C_SOURCE 200809L

#include "npl.h"
#include "thread.h"
#include <pthread.h>
#include <sched.h>

static pthread_mutex_t g_shared_lock;
static pthread_once_t g_shared_once = PTHREAD_ONCE_INIT;

static void init_shared_lock(void)
{
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&g_shared_lock, &attr);
    pthread_mutexattr_destroy(&attr);
}

void npl_shared_lock(void)
{
    pthread_once(&g_shared_once, init_shared_lock);
    NplThreadContext *context = npl_ctx();
    for (;;) {
        if (pthread_mutex_trylock(&g_shared_lock) == 0) {
            context->shared_depth++;
            return;
        }
        npl_safepoint_poll();
        sched_yield();
    }
}

void npl_shared_unlock(void)
{
    NplThreadContext *context = npl_ctx();
    if (context->shared_depth > 0) {
        context->shared_depth--;
        pthread_mutex_unlock(&g_shared_lock);
    }
}

void npl_shared_release_all(void)
{
    NplThreadContext *context = npl_ctx();
    /* Every longjmp landing calls this; release C scratch buffers whose
     * owning frames were skipped by the unwind. */
    npl_cleanup_release_all();
    while (context->shared_depth > 0) {
        context->shared_depth--;
        pthread_mutex_unlock(&g_shared_lock);
    }
}

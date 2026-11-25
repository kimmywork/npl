/*
 * thread-start-root.c — process-wide root for the thread-start handshake.
 *
 * Between pthread_create and the worker publishing its own context root, the
 * NplThread object is otherwise reachable only from the creator's `value`
 * local. If the creator is cancelled while waiting for `registered`, its
 * root stack is unwound; this independent registry keeps the object rooted
 * until the worker attaches (thread_entry calls npl_pending_thread_remove).
 *
 * Extracted from thread-lifecycle.c to keep that file under the project's
 * 800-line soft limit.
 */
#define _POSIX_C_SOURCE 200809L

#include "npl.h"
#include <pthread.h>
#include <stdlib.h>

static NplVal *g_pending;
static int g_pending_count;
static int g_pending_capacity;
static pthread_mutex_t g_pending_lock = PTHREAD_MUTEX_INITIALIZER;

void npl_pending_thread_mark_roots(void)
{
    pthread_mutex_lock(&g_pending_lock);
    for (int i = 0; i < g_pending_count; i++)
        npl_gc_mark_candidate_internal(g_pending[i]);
    pthread_mutex_unlock(&g_pending_lock);
}

void npl_pending_thread_add(NplVal value)
{
    pthread_mutex_lock(&g_pending_lock);
    if (g_pending_count == g_pending_capacity) {
        int capacity = g_pending_capacity ? g_pending_capacity * 2 : 8;
        NplVal *grown = (NplVal *)realloc(
            g_pending, (size_t)capacity * sizeof(NplVal));
        if (!grown) npl_runtime_oom_internal();
        g_pending = grown;
        g_pending_capacity = capacity;
    }
    g_pending[g_pending_count++] = value;
    pthread_mutex_unlock(&g_pending_lock);
}

void npl_pending_thread_remove(NplVal value)
{
    pthread_mutex_lock(&g_pending_lock);
    for (int i = 0; i < g_pending_count; i++) {
        if (g_pending[i] == value) {
            g_pending[i] = g_pending[--g_pending_count];
            break;
        }
    }
    pthread_mutex_unlock(&g_pending_lock);
}

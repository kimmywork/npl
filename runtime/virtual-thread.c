/*
 * virtual-thread.c — T2b (JVM-style) virtual-thread substrate.
 *
 * M carrier pthreads each own a run queue; a virtual thread is handed to the
 * next carrier in round-robin order on every park, so cross-carrier migration
 * is deterministic. A virtual thread suspends only at a CPS boundary:
 * npl_vthread_park stores the raw CPS continuation (argv[argc-1]) in the
 * virtual-thread record and returns WITHOUT a tail request, so the trampoline
 * breaks and the carrier loop regains control. No C stack frame is retained
 * for a suspended thread; the continuation and its dynamic state are heap
 * objects reached from the virtual-thread record (marked by
 * npl_virtual_thread_mark_roots).
 *
 * The private CPS root (vthread_done) is a C closure bound to the record, so
 * a user call/cc continuation ends at the virtual thread's own boundary
 * instead of escaping into the carrier loop. The scheduler token is one-shot
 * and is cleared as soon as a carrier mounts it. On a cross-carrier mount the
 * saved parameter bindings are re-stamped for the new carrier's ownership.
 *
 * Work stealing (moving work between the per-carrier queues) and a
 * CPU-derived carrier count are later tuning; set_carriers() selects M.
 */
#define _POSIX_C_SOURCE 200809L

#include "npl.h"
#include "thread.h"
#include "thread-sched.h"
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define CTX npl_ctx()

static NplVal boxed(void *header)
{
    return (NplVal)(uintptr_t)header | TAG_PTR;
}

static int tagged(NplVal value, uint32_t tag)
{
    return npl_is_ptr(value) && npl_header(value)->tag == tag;
}

/* ---- process-wide registry and per-carrier run queues ----
 *
 * Every virtual-thread object is registered in a malloc'd C array and marked
 * explicitly by npl_virtual_thread_mark_roots (called from the collector's
 * root pass, with all mutators parked). The run queues are C-side linked
 * lists into that same set, so they need no separate GC root.
 *
 * All critical sections are short and perform no NPL allocation or safepoint
 * polls, so a plain mutex cannot hold up a world stop. */
typedef struct {
    NplVirtualThread *head;
    NplVirtualThread *tail;
} VThreadQueue;

static NplVal *g_all;
static int g_all_count;
static int g_all_capacity;
static VThreadQueue *g_queues;
static int g_queues_count;      /* fixed once the first item is enqueued */
static int g_next_carrier;      /* round-robin for newly started threads */
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_cond = PTHREAD_COND_INITIALIZER;
static int g_shutdown;
static int g_carrier_count = 2;
static pthread_t *g_carrier_handles;
static pthread_once_t g_carrier_once = PTHREAD_ONCE_INIT;

void npl_virtual_thread_mark_roots(void)
{
    for (int i = 0; i < g_all_count; i++)
        npl_gc_mark_candidate_internal(g_all[i]);
}

static void registry_add(NplVal value)
{
    if (g_all_count == g_all_capacity) {
        int capacity = g_all_capacity ? g_all_capacity * 2 : 16;
        NplVal *grown = (NplVal *)realloc(
            g_all, (size_t)capacity * sizeof(NplVal));
        if (!grown) npl_runtime_oom_internal();
        g_all = grown;
        g_all_capacity = capacity;
    }
    g_all[g_all_count++] = value;
}

/* Called under g_lock. The queue count is frozen at the first enqueue so a
 * late set_carriers() cannot invalidate indices already handed to carriers. */
static void ensure_queues(void)
{
    if (g_queues) return;
    g_queues_count = g_carrier_count;
    g_queues = (VThreadQueue *)calloc(
        (size_t)g_queues_count, sizeof(VThreadQueue));
    if (!g_queues) npl_runtime_oom_internal();
}

static void enqueue_to(NplVirtualThread *vt, int index)
{
    VThreadQueue *queue = &g_queues[index];
    vt->queue_next = NULL;
    vt->carrier_index = index;
    if (queue->tail) queue->tail->queue_next = vt;
    else queue->head = vt;
    queue->tail = vt;
}

static NplVirtualThread *dequeue_from(int index)
{
    VThreadQueue *queue = &g_queues[index];
    NplVirtualThread *vt = queue->head;
    if (!vt) return NULL;
    queue->head = vt->queue_next;
    if (!queue->head) queue->tail = NULL;
    vt->queue_next = NULL;
    return vt;
}

static NplVirtualThread *require_vthread(NplVal value, const char *who)
{
    if (!tagged(value, BOX_VIRTUAL_THREAD))
        npl_primitive_error(who, "not a virtual thread", value);
    return (NplVirtualThread *)npl_header(value);
}

/* Stores into an existing (possibly old) heap object need a barrier. */
static void set_field(NplVirtualThread *vt, NplVal *field, NplVal value)
{
    *field = value;
    npl_gc_write_barrier(vt, value);
}

/* ---- dynamic state: save the virtual thread, restore it on mount ---- */

static void save_state(NplVirtualThread *vt)
{
    set_field(vt, &vt->wind, CTX->current_wind);
    set_field(vt, &vt->handler, CTX->current_handler);
    set_field(vt, &vt->parameters, CTX->current_parameters);
    set_field(vt, &vt->parameter_extents, CTX->parameter_extents);
    set_field(vt, &vt->pending_extent_key, CTX->pending_extent_key);
    set_field(vt, &vt->initial_handler, CTX->initial_handler);
    vt->next_wind_id = CTX->next_wind_id;
    vt->carrier_context = (void *)npl_ctx();
}

static void restore_state(NplVirtualThread *vt)
{
    /* A different carrier now owns the saved bindings; re-stamp ownership so
     * in-place parameter-set! and continuation re-entry keep working. */
    if (vt->carrier_context != (void *)npl_ctx())
        npl_relocalize_parameter_bindings(vt->parameters);
    CTX->current_wind = vt->wind;
    CTX->current_handler = vt->handler;
    CTX->current_parameters = vt->parameters;
    CTX->parameter_extents = vt->parameter_extents;
    CTX->pending_extent_key = vt->pending_extent_key;
    CTX->initial_handler = vt->initial_handler;
    CTX->next_wind_id = vt->next_wind_id;
    CTX->logical_thread = vt;
}

static void clear_carrier_state(void)
{
    CTX->current_wind = npl_nil();
    CTX->current_handler = npl_nil();
    CTX->current_parameters = npl_nil();
    CTX->parameter_extents = npl_nil();
    CTX->pending_extent_key = npl_nil();
    CTX->initial_handler = npl_nil();
    CTX->next_wind_id = 1;
    CTX->logical_thread = NULL;
}

/* ---- private CPS root and virtual-thread uncaught handler ----
 *
 * Both return npl_nil() without setting pending, so npl_trampoline breaks
 * and npl_apply returns to the carrier loop. Neither longjmps, so no C frame
 * is skipped and the carrier pthread stays alive. */

static void finish_vt(NplVirtualThread *vt, NplVal result, NplVal exception)
{
    pthread_mutex_lock(&g_lock);
    set_field(vt, &vt->end_result, result);
    set_field(vt, &vt->end_exception, exception);
    vt->state = NPL_VTHREAD_TERMINATED;
    vt->continuation = npl_nil();
    pthread_cond_broadcast(&g_cond);
    pthread_mutex_unlock(&g_lock);
}

static NplVal vthread_done(NplVal environment, int64_t argc, NplVal *argv)
{
    (void)argc;
    finish_vt((NplVirtualThread *)npl_header(environment),
              argv[0], npl_false());
    return npl_nil();
}

static NplVal vthread_uncaught(NplVal environment, int64_t argc, NplVal *argv)
{
    (void)argc;
    NplVirtualThread *vt = (NplVirtualThread *)npl_header(environment);
    NplVal exc = npl_thread_exception(NPL_EXC_UNCAUGHT, argv[0]);
    finish_vt(vt, npl_unspecified(), exc);
    return npl_nil();
}

/* ---- public C API ---- */

NplVal npl_vthread_make(NplVal thunk, NplVal name, NplVal parameters)
{
    if (!tagged(thunk, BOX_CLOSURE))
        npl_primitive_error("make-virtual-thread", "expected a procedure",
                            thunk);
    NplVirtualThread *vt = (NplVirtualThread *)
        npl_alloc_object(sizeof(NplVirtualThread));
    vt->header.tag = BOX_VIRTUAL_THREAD;
    vt->header.flags = 0;
    vt->name = npl_is_false(name) ? npl_unspecified() : name;
    vt->end_result = npl_unspecified();
    vt->end_exception = npl_false();
    vt->thunk = thunk;
    vt->root = npl_nil();
    vt->continuation = npl_nil();
    vt->wind = npl_nil();
    vt->handler = npl_nil();
    vt->parameters = parameters;
    vt->parameter_extents = npl_nil();
    vt->pending_extent_key = npl_nil();
    vt->initial_handler = npl_nil();
    vt->next_wind_id = 1;
    vt->state = NPL_VTHREAD_NEW;
    vt->park_count = 0;
    vt->resume_count = 0;
    vt->migration_count = 0;
    vt->carrier_index = 0;
    vt->carrier_context = NULL;
    vt->queue_next = NULL;

    NplVal value = boxed(vt);
    NplVal root = npl_make_closure(vthread_done, value, 1);
    set_field(vt, &vt->root, root);
    npl_gc_write_barrier(vt, parameters);
    npl_gc_write_barrier(vt, vt->name);
    npl_gc_write_barrier(vt, thunk);

    pthread_mutex_lock(&g_lock);
    registry_add(value);
    pthread_mutex_unlock(&g_lock);
    return value;
}

NplVal npl_vthread_p(NplVal value)
{
    return tagged(value, BOX_VIRTUAL_THREAD) ? npl_true() : npl_false();
}

NplVal npl_vthread_name(NplVal value)
{
    return require_vthread(value, "virtual-thread-name")->name;
}

NplVal npl_vthread_current(void)
{
    NplVirtualThread *vt = (NplVirtualThread *)CTX->logical_thread;
    return vt ? boxed(vt) : npl_false();
}

/* The creator's dynamic environment, inherited by a new virtual thread.
 * mount_new clones it under the carrier's ownership. */
NplVal npl_vthread_current_parameters(void)
{
    return CTX->current_parameters;
}

NplVal npl_vthread_root(void)
{
    NplVirtualThread *vt = (NplVirtualThread *)CTX->logical_thread;
    return vt ? vt->root : npl_false();
}

NplVal npl_vthread_end_exception(NplVal value)
{
    return require_vthread(value, "virtual-thread-join!")->end_exception;
}

int npl_vthread_park_count(NplVal value)
{
    return require_vthread(value, "virtual-thread-park-count")->park_count;
}

int npl_vthread_resume_count(NplVal value)
{
    return require_vthread(value, "virtual-thread-resume-count")->resume_count;
}

int npl_vthread_migration_count(NplVal value)
{
    return require_vthread(value,
                           "virtual-thread-migration-count")->migration_count;
}

/* Capture the raw CPS continuation. The emitted control-transfer form does
 * `call $npl_vthread_park(l K)` followed by `ret 2` and no tail request, so
 * returning here ends the current trampoline. */
void npl_vthread_park(NplVal continuation)
{
    NplVirtualThread *vt = (NplVirtualThread *)CTX->logical_thread;
    if (!vt || vt->state != NPL_VTHREAD_RUNNING)
        npl_primitive_error("virtual-thread-yield!",
                            "no active virtual thread", npl_nil());
    if (!npl_is_nil(vt->continuation))
        npl_primitive_error("virtual-thread-yield!",
                            "scheduler continuation already suspended",
                            npl_nil());
    if (!tagged(continuation, BOX_CLOSURE))
        npl_primitive_error("virtual-thread-yield!",
                            "invalid scheduler continuation", continuation);

    set_field(vt, &vt->continuation, continuation);
    save_state(vt);
    pthread_mutex_lock(&g_lock);
    vt->state = NPL_VTHREAD_SUSPENDED;
    vt->park_count++;
    /* Hand off to the next carrier: with M > 1 every yield migrates, so
     * cross-carrier dynamic-state restoration is exercised on each resume. */
    enqueue_to(vt, (vt->carrier_index + 1) % g_queues_count);
    pthread_cond_broadcast(&g_cond);
    pthread_mutex_unlock(&g_lock);
    CTX->logical_thread = NULL;
    /* Deliberately no npl_tail_request: the trampoline breaks. */
}

static void *carrier_entry(void *argument);

static void start_carriers(void)
{
    pthread_mutex_lock(&g_lock);
    ensure_queues();
    int count = g_queues_count;
    pthread_mutex_unlock(&g_lock);
    g_carrier_handles = (pthread_t *)calloc(
        (size_t)count, sizeof(pthread_t));
    if (!g_carrier_handles) npl_runtime_oom_internal();
    for (int i = 0; i < count; i++) {
        if (pthread_create(&g_carrier_handles[i], NULL,
                           carrier_entry, (void *)(intptr_t)i) != 0) {
            fprintf(stderr, "virtual thread: cannot start carrier\n");
            exit(1);
        }
    }
}

NplVal npl_vthread_start(NplVal value)
{
    NplVirtualThread *vt = require_vthread(value, "virtual-thread-start!");
    if (vt->state != NPL_VTHREAD_NEW)
        npl_primitive_error("virtual-thread-start!",
                            "virtual thread already started", value);
    pthread_mutex_lock(&g_lock);
    ensure_queues();
    vt->state = NPL_VTHREAD_RUNNABLE;
    enqueue_to(vt, g_next_carrier % g_queues_count);
    g_next_carrier++;
    pthread_cond_broadcast(&g_cond);
    pthread_mutex_unlock(&g_lock);
    return value;
}

/* Carriers start lazily, after the initial batch is enqueued, so the first
 * validation observes deterministic round-robin multiplexing. */
void npl_virtual_thread_run(void)
{
    pthread_once(&g_carrier_once, start_carriers);
}

NplVal npl_vthread_join(NplVal value)
{
    NplVirtualThread *vt = require_vthread(value, "virtual-thread-join!");
    npl_virtual_thread_run();
    /* The caller is a native thread (or a carrier parked by block_begin);
     * publish a safe snapshot so a collection never waits on this wait. */
    npl_gc_block_begin();
    pthread_mutex_lock(&g_lock);
    while (vt->state != NPL_VTHREAD_TERMINATED)
        pthread_cond_wait(&g_cond, &g_lock);
    NplVal result = vt->end_result;
    pthread_mutex_unlock(&g_lock);
    npl_gc_block_end();
    return result;
}

void npl_virtual_thread_set_carriers(int count)
{
    /* Only effective before any thread has been enqueued and before carriers
     * start; otherwise queue indices already in flight would be invalid. */
    if (count > 0 && !g_queues && !g_carrier_handles)
        g_carrier_count = count;
}

void npl_virtual_thread_shutdown(void)
{
    pthread_mutex_lock(&g_lock);
    g_shutdown = 1;
    pthread_cond_broadcast(&g_cond);
    pthread_mutex_unlock(&g_lock);
    if (g_carrier_handles) {
        /* pthread_join is an unwrapped C wait with no poll site. */
        npl_gc_exclude();
        for (int i = 0; i < g_queues_count; i++)
            (void)pthread_join(g_carrier_handles[i], NULL);
        npl_gc_include();
        free(g_carrier_handles);
        g_carrier_handles = NULL;
    }
    free(g_queues);
    g_queues = NULL;
    g_queues_count = 0;
}

/* ---- the carrier loop ---- */

static void mount_new(NplVirtualThread *vt)
{
    clear_carrier_state();
    /* Inherit the creator's parameter chain under this carrier's ownership. */
    CTX->current_parameters = npl_clone_parameter_bindings(vt->parameters);
    NplVal handler = npl_make_closure(vthread_uncaught, boxed(vt), 2);
    CTX->initial_handler = npl_handler_push_initial(handler);
    save_state(vt);
    CTX->logical_thread = vt;
}

static void *carrier_entry(void *argument)
{
    int index = (int)(intptr_t)argument;
    uintptr_t marker = 0;
    (void)npl_thread_attach_exact(
        (uintptr_t)(void *)&marker + sizeof(marker));
    clear_carrier_state();

    for (;;) {
        /* An idle carrier has no virtual thread mounted, so parking it for
         * GC is safe: its stack holds no suspended computation. */
        npl_gc_block_begin();
        pthread_mutex_lock(&g_lock);
        while (!g_queues[index].head && !g_shutdown)
            pthread_cond_wait(&g_cond, &g_lock);
        NplVirtualThread *vt = g_shutdown ? NULL : dequeue_from(index);
        pthread_mutex_unlock(&g_lock);
        npl_gc_block_end();
        if (!vt) break;

        int first = vt->state == NPL_VTHREAD_RUNNABLE &&
                    vt->resume_count == 0 && vt->park_count == 0;
        vt->state = NPL_VTHREAD_RUNNING;
        if (!first && npl_is_nil(vt->continuation)) {
            fprintf(stderr,
                    "virtual thread: one-shot scheduler continuation "
                    "resumed twice\n");
            exit(1);
        }
        if (!first && vt->carrier_context != (void *)npl_ctx())
            vt->migration_count++;

        NplVal argv[1];
        if (first) {
            mount_new(vt);
            argv[0] = vt->root;
            (void)npl_apply(vt->thunk, 1, argv);
        } else {
            NplVal token = vt->continuation;
            vt->continuation = npl_nil();
            vt->resume_count++;
            restore_state(vt);
            argv[0] = npl_unspecified();
            (void)npl_apply(token, 1, argv);
        }
        /* npl_apply returned: either a yield (park saved the state) or the
         * private root/uncaught handler finished the thread. Clear the
         * carrier so the next virtual thread starts from its own state. */
        clear_carrier_state();
    }

    npl_thread_detach();
    return NULL;
}

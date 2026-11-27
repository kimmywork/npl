/*
 * thread.c — native TLS current context and the process-wide registry.
 *
 * T1 foundation: one NplThreadContext per native thread, one native TLS slot
 * pointing at it, and an explicit registry that the conservative collector can
 * enumerate.  TLS itself is not a GC root; the NplVal fields of a context are
 * registered as roots on the thread that owns them.
 *
 * Runtime C stays C99.  The one-time init and the registry use GCC/Clang
 * __sync builtins, so no libpthread link flag is required for this increment;
 * pthread_self() is in libc on Darwin and on glibc >= 2.34 (the CI hosts).
 * T1.3 replaces the spinlock with the stop-the-world coordinator's mutex.
 */

#define _POSIX_C_SOURCE 200809L

#include "thread.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#define CTX npl_ctx()
/* Compiled programs register one root per global slot; start with room. */
#define NPL_ROOTS_INITIAL 256

/* One current context per OS thread. */
NPL_TLS NplThreadContext *npl_tls_current_context = NULL;

/* Process-wide registry, guarded by a spinlock (T1.3: coordinator lock). */
static NplThreadContext *g_registry = NULL;
static volatile int g_registry_lock = 0;
static volatile int g_thread_count = 0;
static volatile uint64_t g_next_thread_id = 1;

static void lock_registry(void)
{
    while (__sync_lock_test_and_set(&g_registry_lock, 1)) {
        /* spin: registry operations are short and never allocate */
    }
}

static void unlock_registry(void)
{
    __sync_lock_release(&g_registry_lock);
}

static void init_context(NplThreadContext *context, uintptr_t stack_bottom)
{
    memset(context, 0, sizeof(*context));
    context->native_handle = (uintptr_t)pthread_self();
    context->id = __sync_fetch_and_add(&g_next_thread_id, 1);
    context->attached = 1;
    context->stack_bottom = stack_bottom;
    /* Dynamic chains start empty; each thread owns its own bindings. */
    context->current_wind = npl_nil();
    context->current_handler = npl_nil();
    context->current_parameters = npl_nil();
    context->parameter_extents = npl_nil();
    context->pending_extent_key = npl_nil();
    context->initial_handler = npl_nil();
    context->current_prompts = npl_nil();
    context->prompt_overrides = npl_nil();
    context->current_segment = npl_nil();
    context->current_thread_object = npl_nil();
    context->cleanup = NULL;
    context->cleanup_depth = 0;
    context->next_segment_id = 1;
    context->next_wind_id = 1;
    context->next_escape_id = 1;
    context->next_sync_id = 1;
    context->read_recovery = NULL;
}

static NplThreadContext *attach_context(uintptr_t stack_bottom)
{
    NplThreadContext *context =
        (NplThreadContext *)calloc(1, sizeof(*context));
    if (!context) npl_runtime_oom_internal();
    init_context(context, stack_bottom);

    /* Publish the TLS pointer before registering roots: npl_gc_root resolves
     * the current context through it. */
    npl_tls_current_context = context;
    npl_gc_root(&context->current_wind);
    npl_gc_root(&context->current_handler);
    npl_gc_root(&context->current_parameters);
    npl_gc_root(&context->parameter_extents);
    npl_gc_root(&context->pending_extent_key);
    npl_gc_root(&context->initial_handler);
    npl_gc_root(&context->current_prompts);
    npl_gc_root(&context->prompt_overrides);
    npl_gc_root(&context->current_segment);
    npl_gc_root(&context->current_thread_object);

    lock_registry();
    context->next = g_registry;
    g_registry = context;
    __sync_fetch_and_add(&g_thread_count, 1);
    unlock_registry();
    /* Coordinator lock is separate from the registry spinlock; take it
     * outside the registry walk to avoid a lock-order inversion. */
    npl_coordinator_attach_internal(context);
    return context;
}

NplThreadContext *npl_current_context(void)
{
    NplThreadContext *context = npl_tls_current_context;
    if (context) return context;
    /* Implicit attach for C paths that reach the runtime before an explicit
     * npl_thread_attach.  The bound is an approximation of this thread's
     * oldest active frame; T1.3 records exact bounds at safepoints. */
    uintptr_t marker = 0;
    return attach_context((uintptr_t)(void *)&marker + sizeof(marker));
}

NplThreadContext *npl_thread_attach(void)
{
    /* An implicit attach (npl_current_context) may already have registered a
     * context for this thread. Reuse it instead of replacing the TLS pointer
     * and inserting a second registry entry that detach would leave behind. */
    if (npl_tls_current_context) return npl_tls_current_context;
    uintptr_t marker = 0;
    return attach_context((uintptr_t)(void *)&marker + sizeof(marker));
}

NplThreadContext *npl_thread_init_internal(uintptr_t stack_bottom)
{
    /* A lazy attach may already have created the primordial thread's context;
     * reuse it, but publish the authoritative stack bound from main. */
    if (npl_tls_current_context) {
        npl_tls_current_context->stack_bottom = stack_bottom;
        return npl_tls_current_context;
    }
    return attach_context(stack_bottom);
}

/* T2: a created thread knows the top of its own C stack (thread_entry's
 * frame), which is the correct conservative-scan bound for that pthread. */
NplThreadContext *npl_thread_attach_exact(uintptr_t stack_bottom)
{
    if (npl_tls_current_context) {
        npl_tls_current_context->stack_bottom = stack_bottom;
        return npl_tls_current_context;
    }
    return attach_context(stack_bottom);
}

void npl_thread_detach(void)
{
    NplThreadContext *context = npl_tls_current_context;
    if (!context) return;

    lock_registry();
    NplThreadContext **link = &g_registry;
    while (*link && *link != context) link = &(*link)->next;
    if (*link) {
        *link = context->next;
        __sync_fetch_and_sub(&g_thread_count, 1);
    }
    unlock_registry();
    npl_coordinator_detach_internal(context);

    /* A detached foreign thread must not retain NPL values.  Clear the
     * dynamic chains and release the per-thread root array. */
    context->attached = 0;
    context->current_wind = npl_nil();
    context->current_handler = npl_nil();
    context->current_parameters = npl_nil();
    context->parameter_extents = npl_nil();
    context->pending_extent_key = npl_nil();
    context->initial_handler = npl_nil();
    context->current_prompts = npl_nil();
    context->prompt_overrides = npl_nil();
    context->current_segment = npl_nil();
    /* A detached foreign thread must not leave scratch buffers behind. */
    npl_cleanup_release_all();
    context->tail_context = NULL;
    context->sync_frame = NULL;
    context->read_recovery = NULL;
    free(context->roots);
    context->roots = NULL;
    context->root_count = 0;
    context->root_capacity = 0;
    npl_tls_current_context = NULL;
    free(context);
}

void npl_thread_foreach(NplThreadVisitor visit, void *data)
{
    if (!visit) return;
    /* The visitor marks GC candidates; it must not attach or detach threads.
     * Concurrent attach/detach is serialized by the same lock, so the list is
     * stable for the duration of the walk. */
    lock_registry();
    for (NplThreadContext *context = g_registry; context;
         context = context->next)
        visit(context, data);
    unlock_registry();
}

int npl_thread_count(void)
{
    return g_thread_count;
}

/* ---- Per-thread GC root stack ----
 * These used to be process-global in alloc.c.  The root stack is owned by the
 * calling thread's context, so register/unregister and the longjmp
 * save/restore protocol all act on that one stack. */

/* ---- longjmp-safe C scratch buffers ---- */

void npl_cleanup_push(void (*function)(void *), void *argument)
{
    NplCleanupFrame *frame = (NplCleanupFrame *)malloc(sizeof(*frame));
    if (!frame) npl_runtime_oom_internal();
    frame->function = function;
    frame->argument = argument;
    frame->previous = CTX->cleanup;
    CTX->cleanup = frame;
    CTX->cleanup_depth++;
}

void npl_cleanup_pop(void)
{
    NplCleanupFrame *frame = CTX->cleanup;
    if (!frame) return;
    CTX->cleanup = frame->previous;
    CTX->cleanup_depth--;
    free(frame);
}

int npl_cleanup_depth(void)
{
    return CTX->cleanup_depth;
}

/* Unwind only the frames registered since `depth` was captured. A caught
 * reader error must not free scratch buffers owned by surviving callers. */
void npl_cleanup_release_to(int depth)
{
    NplThreadContext *context = CTX;
    while (context->cleanup && context->cleanup_depth > depth) {
        NplCleanupFrame *frame = context->cleanup;
        context->cleanup = frame->previous;
        context->cleanup_depth--;
        frame->function(frame->argument);
        free(frame);
    }
}

void npl_cleanup_release_all(void)
{
    npl_cleanup_release_to(0);
}

void npl_gc_root(NplVal *root)
{
    NplThreadContext *context = CTX;
    if (context->root_count == context->root_capacity) {
        int capacity = context->root_capacity ? context->root_capacity * 2
                                              : NPL_ROOTS_INITIAL;
        NplVal **roots = (NplVal **)realloc(
            context->roots, (size_t)capacity * sizeof(NplVal *));
        if (!roots) npl_runtime_oom_internal();
        context->roots = roots;
        context->root_capacity = capacity;
    }
    context->roots[context->root_count++] = root;
}

/* Primitive-error unwinding (trampoline.c) drops roots registered by the C
 * frames it discards; roots are registered and released in LIFO order. */
int npl_gc_root_count_internal(void) { return CTX->root_count; }

void npl_gc_restore_root_count_internal(int count)
{
    NplThreadContext *context = CTX;
    if (count >= 0 && count <= context->root_count)
        context->root_count = count;
}

/* Search from the top (roots are normally released LIFO) and keep the
 * order of the rest, so a saved root count still marks the same prefix. */
void npl_gc_unroot(NplVal *root)
{
    NplThreadContext *context = CTX;
    for (int i = context->root_count - 1; i >= 0; i--) {
        if (context->roots[i] == root) {
            memmove(&context->roots[i], &context->roots[i + 1],
                    (size_t)(context->root_count - 1 - i) * sizeof(NplVal *));
            context->root_count--;
            return;
        }
    }
}

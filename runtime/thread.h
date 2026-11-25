/*
 * thread.h — per-thread runtime context (T1 foundation).
 *
 * Each native thread that enters NPL owns one NplThreadContext.  The current
 * context lives in native TLS (NPL_TLS, a checked C99 compiler extension);
 * an explicit process-wide registry lets the collector enumerate every
 * context.  TLS storage is not automatically a GC root: the NplVal fields
 * below are registered as roots on their owning thread at attach time.
 *
 * The QBE-generated executable owns the `npl_stack_limit` TLS slot
 * (program.npl emits `export thread data`), and the runtime reaches that one
 * slot through the extern declaration in npl.h.  Native packages and the
 * shared runtime never define an independent copy.
 *
 * Runtime C stays C99: NPL_TLS is __thread, not C11 _Thread_local.
 */
#ifndef NPL_THREAD_H
#define NPL_THREAD_H

#include "npl.h"
#include <setjmp.h>

#define NPL_MAX_ESCAPE_DEPTH 64
#define NPL_READ_ERROR_SIZE 128

/* A longjmp (cooperative cancellation or primitive error) skips C frames
 * holding malloc'd scratch buffers. Frames are heap allocated so they stay
 * valid across the unwind; npl_shared_release_all runs them at every landing. */
typedef struct NplCleanupFrame {
    struct NplCleanupFrame *previous;
    void (*function)(void *);
    void *argument;
} NplCleanupFrame;

/* ---- Trampoline / escape / synchronous-call frames ----
 * These used to be file-static in trampoline.c.  They are per-thread because
 * they point at C stack frames and describe one thread's control state. */

typedef struct NplTailContext {
    struct NplTailContext *previous;
    NplGenericFunction function;
    NplVal environment;
    int64_t argc;
    NplVal *arguments;
    size_t argument_capacity;
    int pending;
    /* Primitive-error unwind target (error.c): state restored on longjmp. */
    jmp_buf unwind;
    int root_count;
    uintptr_t stack_bottom;
    int escape_depth;
    /* npl_stack_limit (stack.c) when the trampoline started. */
    uintptr_t stack_limit;
} NplTailContext;

typedef struct NplEscapeFrame {
    jmp_buf jump;
    uint64_t id;
    NplVal value;
    NplTailContext *saved_tail_context;
    uintptr_t saved_stack_limit;
} NplEscapeFrame;

typedef struct NplSyncFrame {
    struct NplSyncFrame *previous;
    jmp_buf jump;
    uint64_t id;
    NplVal raised;
    NplTailContext *saved_tail_context;
    int saved_escape_depth;
    int saved_root_count;
    uintptr_t saved_stack_bottom;
} NplSyncFrame;

/* ---- Per-thread context ----
 * Classification of the remaining runtime statics is recorded in
 * .agents/design/threads.md ("Mutable-global audit").  This struct holds the
 * thread-local class; heap metadata, symbol interning, ports, command line,
 * and the EOF singleton stay process-wide. */

typedef struct NplThreadContext {
    struct NplThreadContext *next;   /* process-wide registry linkage */
    uintptr_t native_handle;         /* pthread_self() at registration */
    uint64_t id;                     /* stable runtime-assigned id */
    int attached;                    /* 1 while registered */

    /* Conservative stack bounds and direct-frame windows (carrier-owned). */
    uintptr_t stack_bottom;
    uintptr_t stack_low;
    uintptr_t stack_high;
    uintptr_t stack_floor;
    uintptr_t window_bytes;

    /* Per-thread GC root stack. */
    NplVal **roots;
    int root_count;
    int root_capacity;

    /* Dynamic state: one independent chain per thread. */
    NplVal current_wind;
    NplVal current_handler;
    NplVal current_parameters;
    /* SRFI 18 cross-thread continuation support: a per-thread mapping from a
     * parameterize extent (frames vector) to its thread-owned binding chain
     * plus the baseline to restore on exit; the frames vector published by
     * npl_parameters_enter for the following npl_wind_push; and this thread's
     * initial uncaught handler frame. All three are GC roots (thread.c). */
    NplVal parameter_extents;
    NplVal pending_extent_key;
    NplVal initial_handler;
    uint64_t next_wind_id;

    /* Trampoline / escape / synchronous-call context. */
    NplTailContext *tail_context;
    int escape_depth;
    NplEscapeFrame escape_frames[NPL_MAX_ESCAPE_DEPTH];
    uint64_t next_escape_id;
    NplSyncFrame *sync_frame;
    uint64_t next_sync_id;

    /* Reader recovery. */
    jmp_buf *read_recovery;
    char read_error_message[NPL_READ_ERROR_SIZE];

    /* Safepoint / stop-the-world state (T1.3).  At a poll a mutator publishes
     * a callee-saved register snapshot and the low end of its scanable C
     * stack, then parks.  The collector scans [saved_stack_pointer,
     * stack_bottom) and the jmp_buf of every parked context. */
    jmp_buf registers;
    uintptr_t saved_stack_pointer;
    int snapshot_valid;
    int parked;
    /* A thread in arbitrary C code is not safe to scan: an unwrapped call
     * keeps GC excluded (not counted as an active mutator).  A wrapped
     * blocking call publishes its snapshot first and stays parked. */
    int gc_excluded;
    int blocked;
    /* Only blocked contexts that are still counted in g_active_mutators
     * contribute to g_blocked_mutators; an excluded mutator must not reduce
     * the stop-world expectation twice. */
    int blocked_counted;
    int cancellation_requested;
    int cancel_observed;            /* latched at a safepoint; T2 raises */
    int cancel_owed;                /* this context owns one g_cancel_pending contribution */
    int shared_depth;               /* recursive shared-object lock depth */
    uint64_t parked_epoch;          /* world-stop generation this park is for */

    /* T2: the current thread object, registered as a GC root at attach. */
    NplVal current_thread_object;
    /* T2b: logical thread currently mounted on this carrier. */
    void *logical_thread;
    /* C-side scratch buffers that must be freed when a cancellation or
     * primitive-error longjmp skips their function. Frames are heap allocated
     * so they survive the unwind; npl_shared_release_all releases them. */
    struct NplCleanupFrame *cleanup;
    int cleanup_depth;
} NplThreadContext;

/* The current context lives in one native TLS slot.  npl_ctx() is the hot
 * path: one TLS read and a branch, with no cross-translation-unit call, so
 * the trampoline and control loops keep their old direct-access cost.  The
 * slow path attaches the calling thread on first use; foreign threads should
 * prefer the explicit npl_thread_attach/npl_thread_detach pair. */
extern NPL_TLS NplThreadContext *npl_tls_current_context;
NplThreadContext *npl_current_context(void);
static inline NplThreadContext *npl_ctx(void)
{
    NplThreadContext *context = npl_tls_current_context;
    return context ? context : npl_current_context();
}
/* Attach a foreign thread. The default stack bound covers this function's
 * own frame only: a caller holding an unrooted NplVal across a safepoint must
 * either root it (npl_gc_root) or publish its own bound with
 * npl_gc_swap_stack_bottom / npl_thread_attach_exact. A thread that already
 * has a TLS context (e.g. an implicit attach) reuses it. */
NplThreadContext *npl_thread_attach(void);
void npl_thread_detach(void);

/* Registry enumeration for the collector. */
typedef void (*NplThreadVisitor)(NplThreadContext *context, void *data);
void npl_thread_foreach(NplThreadVisitor visit, void *data);
int npl_thread_count(void);

/* Register the primordial thread during one-time runtime initialization.
 * Registers the dynamic-state roots and publishes the GC stack bound. */
NplThreadContext *npl_thread_init_internal(uintptr_t stack_bottom);

/* Attach a thread whose C stack top is known (T2 thread_entry). */
NplThreadContext *npl_thread_attach_exact(uintptr_t stack_bottom);

/* coordinator.c hooks, called by thread.c on attach/detach. */
void npl_coordinator_attach_internal(NplThreadContext *context);
void npl_coordinator_detach_internal(NplThreadContext *context);

#endif /* NPL_THREAD_H */

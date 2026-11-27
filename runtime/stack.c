/*
 * stack.c — the C stack limit of direct entries.
 *
 * A direct entry of a compiled direct-safe function (compiler
 * backend/program.npl emit-direct-function) compares the address of its
 * frame with npl_stack_limit. Below it, the call runs the function's CPS
 * entry through npl_direct_fallback, whose continuations live on the heap,
 * so deep non-tail recursion does not overflow the C stack.
 *
 * T1: npl_stack_limit is a native TLS slot owned by the generated executable
 * (QBE `export thread data`), declared extern here through npl.h.  The
 * window bounds moved into the current thread's NplThreadContext, so each OS
 * thread has its own direct-frame window.  The generated guard reads the
 * slot directly with `thread $npl_stack_limit` — one IL instruction, no C
 * accessor call per entry.
 */

#define _POSIX_C_SOURCE 200809L

#include "npl.h"
#include "thread.h"
#include <stdlib.h>
#ifdef _WIN32
#include "os-win32.h"
#else
#include <sys/resource.h>
#endif

#define CTX npl_ctx()

/* 0 before npl_runtime_init: no entry falls back.  The definition lives in
 * the generated executable (program.npl) or, for C-only harnesses, in
 * standalone-slots.c.  Every link mode and native package reaches the same
 * TLS slot through the extern declaration in npl.h. */

#define NPL_STACK_FALLBACK_BYTES ((uintptr_t)8 << 20)
#define NPL_STACK_MAX_BYTES ((uintptr_t)1 << 30)
/* At most this much C stack holds direct frames. Every collection scans
 * the stack conservatively, so a deep direct stack makes each minor
 * collection slow (a 6 MiB direct stack at a 64 KiB nursery: 10x); deeper
 * recursion continues in CPS on the heap. */
#ifndef NPL_DIRECT_STACK_BYTES
#define NPL_DIRECT_STACK_BYTES ((uintptr_t)256 << 10)
#endif

/* npl_stack_limit is the end of the current window: direct frames may use
 * the stack down to it. The first window ends NPL_DIRECT_STACK_BYTES below
 * the top; npl_direct_fallback opens the next one. No window ends below
 * ctx->stack_floor, which leaves at least a quarter of the stack free for C
 * code below the direct frames: primitives that recurse on the C stack
 * (write, equal?, bignum printing) and the fallbacks' nested trampolines. */

/* opaque zero (npl.h): never written, process-wide immutable. */

/* base is an address near the top (oldest end) of the stack. */
void npl_stack_init_internal(uintptr_t base)
{
#ifdef _WIN32
    uintptr_t low = 0, high = 0;
    npl_win32_stack_bounds(&low, &high);
    uintptr_t size = (high > low) ? (high - low) : NPL_STACK_FALLBACK_BYTES;
    if (high > base) base = high;
#else
    struct rlimit limit;
    uintptr_t size = NPL_STACK_FALLBACK_BYTES;
    if (getrlimit(RLIMIT_STACK, &limit) == 0 &&
        limit.rlim_cur != RLIM_INFINITY && limit.rlim_cur > 0)
        size = (uintptr_t)limit.rlim_cur;
#endif
    if (size > NPL_STACK_MAX_BYTES) size = NPL_STACK_MAX_BYTES;
    uintptr_t usable = size - size / 4;
    CTX->window_bytes = usable < NPL_DIRECT_STACK_BYTES ? usable
                                                       : NPL_DIRECT_STACK_BYTES;
    CTX->stack_floor = base > usable ? base - usable : 0;
    /* Approximate native stack bounds for the safepoint/GC record.  T1.3
     * replaces this with the platform's exact stack query at registration. */
    CTX->stack_high = base;
    CTX->stack_low = base > size ? base - size : 0;
    NPL_STACK_LIMIT_SET(base > CTX->window_bytes
                            ? base - CTX->window_bytes : 0);
}

/* A direct entry below the limit: run the CPS entry on a nested trampoline,
 * whose continuations are on the heap, in a new window below this frame.
 * The CPS entry of a direct-safe function makes no direct calls
 * (frontend/direct-calls.npl), so deep recursion continues on the heap;
 * direct calls made by other code on the way (a library procedure in a
 * numeric slow path) run directly again instead of falling back one by
 * one. A raise or an escape that leaves the call lands on a trampoline or
 * call/cc frame, which restores the limit it saved (trampoline.c). */
NplVal npl_direct_fallback(NplGenericFunction function, NplVal environment,
                           int64_t argc, NplVal *argv)
{
    uintptr_t marker = 0;
    uintptr_t here = (uintptr_t)(void *)&marker;
    uintptr_t saved = NPL_STACK_LIMIT_GET();
    uintptr_t window = here > CTX->window_bytes
                           ? here - CTX->window_bytes : 0;
    NPL_STACK_LIMIT_SET(window > CTX->stack_floor ? window : CTX->stack_floor);
    NplVal result = npl_call_sync(function, environment, argc, argv);
    NPL_STACK_LIMIT_SET(saved);
    return result;
}

static int is_pair(NplVal value)
{
    return npl_is_ptr(value) && npl_header(value)->tag == BOX_PAIR;
}

NplVal npl_direct_fallback_rest(NplGenericFunction function,
                                NplVal environment, int64_t fixed,
                                NplVal *argv)
{
    /* The rest list keeps its elements alive while they are copied into
     * the malloc'd buffer, which the collector does not scan; keep it on
     * the scanned stack. */
    volatile NplVal rest = argv[fixed];
    int64_t count = fixed;
    for (NplVal item = rest; is_pair(item); item = npl_pair_ptr(item)->cdr)
        count++;
    NplVal *arguments =
        (NplVal *)malloc((size_t)(count > 0 ? count : 1) * sizeof(NplVal));
    if (!arguments) npl_runtime_oom_internal();
    int64_t index = 0;
    for (; index < fixed; index++) arguments[index] = argv[index];
    for (NplVal item = rest; is_pair(item); item = npl_pair_ptr(item)->cdr)
        arguments[index++] = npl_pair_ptr(item)->car;
    NplVal result =
        npl_direct_fallback(function, environment, count, arguments);
    free(arguments);
    (void)rest;
    return result;
}

/*
 * stack.c — the C stack limit of direct entries.
 *
 * A direct entry of a compiled direct-safe function (compiler
 * backend/program.npl emit-direct-function) compares the address of its
 * frame with npl_stack_limit. Below it, the call runs the function's CPS
 * entry through npl_call_sync, whose continuations live on the heap, so deep
 * non-tail recursion does not overflow the C stack.
 */

#define _POSIX_C_SOURCE 200809L

#include "npl.h"
#include <sys/resource.h>

/* 0 before npl_runtime_init: no entry falls back. */
uintptr_t npl_stack_limit = 0;

#define NPL_STACK_FALLBACK_BYTES ((uintptr_t)8 << 20)
#define NPL_STACK_MAX_BYTES ((uintptr_t)1 << 30)
/* At most this much C stack holds direct frames. Every collection scans
 * the stack conservatively, so a deep direct stack makes each minor
 * collection slow (a 6 MiB direct stack at a 64 KiB nursery: 10x); deeper
 * recursion continues in CPS on the heap. */
#ifndef NPL_DIRECT_STACK_BYTES
#define NPL_DIRECT_STACK_BYTES ((uintptr_t)256 << 10)
#endif

/* base is an address near the top (oldest end) of the stack. At least a
 * quarter of the stack stays free for C code below the limit: primitives
 * that recurse on the C stack (write, equal?, bignum printing) and the
 * fallback's nested trampoline. */
void npl_stack_init_internal(uintptr_t base)
{
    struct rlimit limit;
    uintptr_t size = NPL_STACK_FALLBACK_BYTES;
    if (getrlimit(RLIMIT_STACK, &limit) == 0 &&
        limit.rlim_cur != RLIM_INFINITY && limit.rlim_cur > 0)
        size = (uintptr_t)limit.rlim_cur;
    if (size > NPL_STACK_MAX_BYTES) size = NPL_STACK_MAX_BYTES;
    uintptr_t usable = size - size / 4;
    if (usable > NPL_DIRECT_STACK_BYTES) usable = NPL_DIRECT_STACK_BYTES;
    npl_stack_limit = base > usable ? base - usable : 0;
}

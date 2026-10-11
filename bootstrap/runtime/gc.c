/*
 * gc.c — stop-the-world conservative mark-sweep collector.
 *
 * Allocator metadata and the mark worklist live in alloc.c. This file owns
 * root discovery: registered roots, saved registers, and the active stack.
 */

#include "npl.h"
#include <setjmp.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

void npl_gc_begin_internal(void);
void npl_gc_mark_candidate_internal(NplVal value);
void npl_gc_mark_roots_internal(void);
void npl_gc_drain_internal(void);
void npl_gc_sweep_internal(void);
uintptr_t npl_gc_stack_bottom_internal(void);

#if defined(__clang__)
__attribute__((no_sanitize("address")))
#endif
static void mark_words(uintptr_t begin, uintptr_t end)
{
    if (begin > end) {
        uintptr_t tmp = begin;
        begin = end;
        end = tmp;
    }
    begin = (begin + sizeof(uintptr_t) - 1) & ~(sizeof(uintptr_t) - 1);
    for (uintptr_t address = begin; address + sizeof(uintptr_t) <= end;
         address += sizeof(uintptr_t)) {
        uintptr_t candidate;
        memcpy(&candidate, (const void *)address, sizeof(candidate));
        npl_gc_mark_candidate_internal((NplVal)candidate);
    }
}

void npl_gc_collect(void)
{
    uintptr_t stack_marker = 0;
    uintptr_t stack_bottom = npl_gc_stack_bottom_internal();
    jmp_buf registers;

    npl_gc_begin_internal();
    npl_gc_mark_roots_internal();

    /* setjmp materializes callee-saved registers into addressable memory. */
    if (setjmp(registers) == 0) {
        uintptr_t reg_begin = (uintptr_t)(const void *)&registers;
        uintptr_t reg_end = reg_begin + sizeof(registers);
        mark_words(reg_begin, reg_end);
    }

    if (stack_bottom)
        mark_words((uintptr_t)(void *)&stack_marker, stack_bottom);
    npl_gc_drain_internal();
    npl_gc_sweep_internal();
}

void npl_gc_maybe_collect(void)
{
    if (npl_gc_allocated() >= npl_gc_threshold()) npl_gc_collect();
}

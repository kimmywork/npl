/*
 * gc.c — stop-the-world conservative mark-sweep collector.
 *
 * Allocator metadata and the mark worklist live in alloc.c. This file owns
 * root discovery (registered roots, saved registers, the active stack) and
 * the object graph: the NplVal fields of each heap object type.
 */

#include "npl.h"
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

void npl_gc_begin_internal(int minor);
void npl_gc_sweep_minor_internal(void);
void npl_gc_set_poison_internal(int enabled);
size_t npl_gc_verify_heap_internal(void);
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

/* Every NplVal field of an object, for marking and barrier verification. */
void npl_gc_visit_children_internal(NplHeader *header,
                                    NplChildVisitor visit)
{
        switch (header->tag) {
        case BOX_PAIR: {
            NplPair *pair = (NplPair *)header;
            visit(pair->car);
            visit(pair->cdr);
            break;
        }
        case BOX_BIGNUM:
            break;
        case BOX_STRING:
        case BOX_SYMBOL:
            visit(npl_string_owned_buffer_internal((const NplString *)header));
            break;
        case BOX_RATIONAL: {
            NplRational *rational = (NplRational *)header;
            visit(rational->numerator);
            visit(rational->denominator);
            break;
        }
        case BOX_COMPLEX: {
            NplComplex *complex = (NplComplex *)header;
            visit(complex->real);
            visit(complex->imag);
            break;
        }
        case BOX_RECORD:
        case BOX_VECTOR: {
            NplVector *vector = (NplVector *)header;
            for (uint32_t i = 0; i < vector->length; i++)
                visit(vector->data[i]);
            break;
        }
        case BOX_CLOSURE: {
            NplClosure *closure = (NplClosure *)header;
            visit(closure->environment);
            for (int64_t i = 0; i < closure->capture_count; i++)
                visit(closure->captures[i]);
            break;
        }
        case BOX_VALUES: {
            NplValues *values = (NplValues *)header;
            for (uint32_t i = 0; i < values->count; i++)
                visit(values->data[i]);
            break;
        }
        case BOX_WIND_FRAME: {
            NplWindFrame *frame = (NplWindFrame *)header;
            visit(frame->parent);
            visit(frame->before);
            visit(frame->after);
            break;
        }
        case BOX_WIND_TRANSITION: {
            NplWindTransition *transition = (NplWindTransition *)header;
            visit(transition->target);
            visit(transition->target_handler);
            visit(transition->continuation);
            visit(transition->value);
            visit(transition->entering);
            uint32_t count = transition->exit_count + transition->entry_count;
            for (uint32_t i = 0; i < count; i++)
                visit(transition->frames[i]);
            break;
        }
        case BOX_HANDLER_FRAME: {
            NplHandlerFrame *frame = (NplHandlerFrame *)header;
            visit(frame->parent);
            visit(frame->procedure);
            break;
        }
        case BOX_RAISE_CONTEXT: {
            NplRaiseContext *context = (NplRaiseContext *)header;
            visit(context->frame);
            visit(context->continuation);
            visit(context->value);
            break;
        }
        case BOX_PARAMETER: {
            NplParameter *parameter = (NplParameter *)header;
            visit(parameter->converter);
            visit(parameter->value);
            break;
        }
        case BOX_PARAMETER_BINDING: {
            NplParameterBinding *binding = (NplParameterBinding *)header;
            visit(binding->parent);
            visit(binding->parameter);
            visit(binding->value);
            break;
        }
        case BOX_PARAMETER_CONTEXT: {
            NplParameterContext *context = (NplParameterContext *)header;
            visit(context->parameter);
            visit(context->continuation);
            break;
        }
        case BOX_PROMISE: {
            NplPromise *promise = (NplPromise *)header;
            visit(promise->value);
            break;
        }
        case BOX_PROMISE_CONTEXT: {
            NplPromiseContext *context = (NplPromiseContext *)header;
            visit(context->root);
            visit(context->current);
            visit(context->continuation);
            break;
        }
        case BOX_PORT: {
            NplPort *port = (NplPort *)header;
            visit(port->buffer);
            break;
        }
        default:
            break;
        }
}

/* NPL_GC_VERIFY=N (N >= 1): at every Nth collection, check before it that
 * each old object holding a young pointer is in the remembered set (a miss
 * is a store that skipped npl_gc_write_barrier), and after it that no live
 * object holds a freed one. Freed young objects are poisoned at every
 * collection, so a dangling reference also fails between checks. Exit 70
 * on a failure. Each check scans the whole heap. */
static int verify_due(void)
{
    static long interval = -1;
    static long count = 0;
    if (interval < 0) {
        const char *text = getenv("NPL_GC_VERIFY");
        char *end = NULL;
        interval = text && *text ? strtol(text, &end, 10) : 0;
        if (interval < 0 || (end && *end != '\0')) interval = 0;
        npl_gc_set_poison_internal(interval > 0);
    }
    return interval > 0 && ++count % interval == 0;
}

static void verify_failed(size_t count, const char *what)
{
    fprintf(stderr, "npl gc verify: %zu %s\n", count, what);
    exit(70);
}

static void collect(int minor)
{
    uintptr_t stack_marker = 0;
    uintptr_t stack_bottom = npl_gc_stack_bottom_internal();
    jmp_buf registers;
    int verify = verify_due();

    if (verify) {
        size_t missed = npl_gc_verify_barriers(1);
        if (missed != 0) verify_failed(missed, "missed write barriers");
    }
    npl_gc_begin_internal(minor);
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
    if (minor) npl_gc_sweep_minor_internal();
    else npl_gc_sweep_internal();
    if (verify) {
        size_t dangling = npl_gc_verify_heap_internal();
        if (dangling != 0) verify_failed(dangling, "references to freed objects");
    }
}

void npl_gc_collect(void) { collect(0); }

/* The nursery is full: collect young objects only (alloc.c). */
void npl_gc_collect_minor_internal(void) { collect(1); }

void npl_gc_maybe_collect(void)
{
    if (npl_gc_allocated() >= npl_gc_threshold()) npl_gc_collect();
}

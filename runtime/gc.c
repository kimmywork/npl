/*
 * gc.c — stop-the-world conservative mark-sweep collector.
 *
 * Allocator metadata and the mark worklist live in alloc.c. This file owns
 * root discovery (registered roots, saved registers, the active stack) and
 * the object graph: the NplVal fields of each heap object type.
 */

#include "npl.h"
#include "thread-sched.h"
#include <setjmp.h>
#include <sched.h>
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

/* The T1.3 coordinator uses this to scan a parked mutator's C stack and
 * register snapshot with the collector's own conservative word scan. */
void npl_gc_mark_words_internal(uintptr_t begin, uintptr_t end)
{
    mark_words(begin, end);
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
            visit(frame->handler);
            visit(frame->parameters);
            visit(frame->extent_key);
            break;
        }
        case BOX_DYNAMIC_CONTEXT: {
            NplDynamicContext *context = (NplDynamicContext *)header;
            visit(context->wind);
            visit(context->handler);
            visit(context->parameters);
            visit(context->prompts);
            visit(context->prompt_overrides);
            visit(context->segment);
            visit(context->source_initial_handler);
            break;
        }
        case BOX_PROMPT_FRAME: {
            NplPromptFrame *frame = (NplPromptFrame *)header;
            visit(frame->tag);
            visit(frame->handler);
            visit(frame->escape);
            visit(frame->return_);
            visit(frame->parent);
            visit(frame->origin);
            break;
        }
        case BOX_CONTINUATION_SEGMENT: {
            NplContinuationSegment *segment =
                (NplContinuationSegment *)header;
            visit(segment->closure);
            visit(segment->parent);
            visit(segment->origin_prompt);
            break;
        }
        case BOX_CONTINUATION_GRAFT: {
            NplContinuationGraft *graft = (NplContinuationGraft *)header;
            visit(graft->procedure);
            visit(graft->arguments);
            visit(graft->prompt);
            visit(graft->target);
            break;
        }
        case BOX_CONTINUATION: {
            NplContinuation *continuation = (NplContinuation *)header;
            visit(continuation->procedure);
            visit(continuation->kind);
            visit(continuation->origin);
            visit(continuation->tag);
            visit(continuation->prompts);
            visit(continuation->graft);
            break;
        }
        case BOX_VIRTUAL_THREAD: {
            NplVirtualThread *vt = (NplVirtualThread *)header;
            visit(vt->name);
            visit(vt->end_result);
            visit(vt->end_exception);
            visit(vt->thunk);
            visit(vt->root);
            visit(vt->continuation);
            visit(vt->work);
            visit(vt->resume_value);
            visit(vt->wind);
            visit(vt->handler);
            visit(vt->parameters);
            visit(vt->parameter_extents);
            visit(vt->pending_extent_key);
            visit(vt->initial_handler);
            visit(vt->prompts);
            visit(vt->prompt_overrides);
            visit(vt->default_prompt);
            visit(vt->segment);
            break;
        }
        case BOX_WIND_TRANSITION: {
            NplWindTransition *transition = (NplWindTransition *)header;
            visit(transition->target);
            visit(transition->target_handler);
            visit(transition->continuation);
            visit(transition->value);
            visit(transition->entering);
            visit(transition->context);
            visit(transition->target_parameters);
            visit(transition->target_prompts);
            visit(transition->target_prompt_overrides);
            visit(transition->target_segment);
            visit(transition->cache);
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
        case BOX_PARAMETERIZATION: {
            NplParameterization *parameterization =
                (NplParameterization *)header;
            visit(parameterization->bindings);
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
        case BOX_HASHTABLE: {
            NplHashTable *table = (NplHashTable *)header;
            visit(table->entries);
            visit(table->equivalence);
            visit(table->hash);
            break;
        }
        case BOX_POINTER: {
            NplPointer *pointer = (NplPointer *)header;
            visit(pointer->block);
            break;
        }
        case BOX_PORT: {
            NplPort *port = (NplPort *)header;
            visit(port->buffer);
            break;
        }
        case BOX_THREAD: {
            NplThread *thread = (NplThread *)header;
            visit(thread->name);
            visit(thread->specific);
            visit(thread->end_result);
            visit(thread->end_exception);
            visit(thread->owned_mutexes);
            visit(thread->thunk);
            visit(thread->parameters);
            break;
        }
        case BOX_MUTEX: {
            NplMutex *mutex = (NplMutex *)header;
            visit(mutex->name);
            visit(mutex->specific);
            visit(mutex->owner);
            visit(mutex->waiters);
            break;
        }
        case BOX_CONDVAR: {
            NplCondvar *cv = (NplCondvar *)header;
            visit(cv->name);
            visit(cv->specific);
            visit(cv->waiters);
            break;
        }
        case BOX_THREAD_EXCEPTION: {
            NplThreadException *exc = (NplThreadException *)header;
            visit(exc->reason);
            break;
        }
        case BOX_TIME:
            break;
        case BOX_ATOMIC_FLAG:
        case BOX_ATOMIC_FXBOX:
            break;
        case BOX_ATOMIC_BOX: {
            NplAtomicBox *box = (NplAtomicBox *)header;
            visit(box->value);
            break;
        }
        case BOX_ATOMIC_PAIR: {
            NplAtomicPair *pair = (NplAtomicPair *)header;
            visit(pair->first);
            visit(pair->second);
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
    int verify;

    /* Only one collector at a time.  A losing collector retries at a
     * safepoint, so it parks instead of blocking unparked on the mutex while
     * the winner waits for all mutators to park.  Request the world stop
     * BEFORE the heap lock: a mutator polls before it ever needs the heap
     * lock, so that wait can never deadlock on the heap mutex. */
    while (!npl_try_collect_lock()) {
        npl_safepoint_poll();
        sched_yield();
    }
    npl_gc_stop_world();
    npl_heap_lock();

    verify = verify_due();
    if (verify) {
        size_t missed = npl_gc_verify_barriers(1);
        if (missed != 0) verify_failed(missed, "missed write barriers");
    }
    npl_gc_begin_internal(minor);
    npl_gc_mark_roots_internal();
    /* Every other mutator is parked with a published stack pointer and
     * callee-saved register snapshot; scan those before draining. */
    npl_gc_mark_parked_stacks();

    /* The collector's own callee-saved registers and C stack. */
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

    npl_heap_unlock();
    npl_gc_start_world();
    npl_collect_unlock();
}

void npl_gc_collect(void) { collect(0); }

/* The nursery is full: collect young objects only (alloc.c). */
void npl_gc_collect_minor_internal(void) { collect(1); }

void npl_gc_maybe_collect(void)
{
    if (npl_gc_allocated() >= npl_gc_threshold()) npl_gc_collect();
}

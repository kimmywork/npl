/*
 * dynamic-context.c — SRFI 18 captured dynamic environments and the wind
 * transfer. Split from control.c (which keeps the dynamic-state primitives)
 * so the per-extent environment machinery has one clear home.
 *
 * A call/cc continuation captures a heap NplDynamicContext (wind, handler,
 * parameters, and the source thread's initial handler frame). On invocation,
 * possibly on another thread, npl_wind_transfer reinstates that environment
 * on the *invoking* thread: every dynamic-wind before/after thunk runs with
 * the handler and parameter chain of its own extent, and the target context
 * is installed at completion. Parameter bindings are always thread-owned:
 * a per-thread extent mapping lets the same continuation be re-entered while
 * preserving in-place parameter mutations, and lets a cross-thread first
 * entry clone the source chain under the invoking thread's ownership.
 */
#define _POSIX_C_SOURCE 200809L

#include "npl.h"
#include "thread.h"
#include <stdio.h>
#include <stdlib.h>

#define CTX npl_ctx()

static NplVal boxed(void *header)
{
    return (NplVal)(uintptr_t)header | TAG_PTR;
}

static NplVector *require_parameter_vector(NplVal value)
{
    if (!npl_is_ptr(value) || npl_header(value)->tag != BOX_VECTOR) {
        fprintf(stderr, "parameterize: invalid binding vectors\n");
        exit(1);
    }
    return (NplVector *)npl_header(value);
}

/* ---- per-thread extent -> local binding mapping ----
 *
 * One entry is (key . (local . baseline)); the list head holds the most
 * recent entry, so a transfer that re-localizes an extent supersedes the
 * normal entry without mutating existing pairs. The frames vector itself is
 * never written: it is shared by the before/after closures of a captured
 * parameterize extent and may still be in use on the source thread. */

static NplVal extent_lookup(NplVal key)
{
    for (NplVal rest = CTX->parameter_extents; !npl_is_nil(rest);
         rest = npl_cdr(rest)) {
        NplVal entry = npl_car(rest);
        if (npl_car(entry) == key) return entry;
    }
    return npl_nil();
}

static NplVal extent_local(NplVal entry)
{
    return npl_car(npl_cdr(entry));
}

static NplVal extent_baseline(NplVal entry)
{
    return npl_cdr(npl_cdr(entry));
}

static void extent_register(NplVal key, NplVal local, NplVal baseline)
{
    NplVal entry = npl_cons(key, npl_cons(local, baseline));
    CTX->parameter_extents = npl_cons(entry, CTX->parameter_extents);
}

/* ---- parameter chain localization ---- */

/* Clone a source binding chain into fresh nodes owned by the invoking
 * thread, preserving chain direction (head = innermost binding). A owned
 * chain is reused as-is so in-place parameter-set! mutations remain visible
 * on continuation re-entry. baseline_out receives the chain with the
 * extent's leading binding_count nodes removed. */
/* Iterative, order-preserving clone: a deeply nested parameterize chain lives
 * on the heap, so a recursive C clone could overflow the native stack. */
static NplVal clone_parameter_bindings(NplVal source)
{
    uint32_t length = 0;
    for (NplVal rest = source;
         npl_is_ptr(rest) &&
         npl_header(rest)->tag == BOX_PARAMETER_BINDING;
         rest = ((NplParameterBinding *)npl_header(rest))->parent)
        length++;
    if (length == 0) return npl_nil();
    NplVal *nodes = (NplVal *)malloc((size_t)length * sizeof(NplVal));
    if (!nodes) npl_runtime_oom_internal();
    /* The loop below allocates (and polls); a cancellation longjmp must not
     * leak this C buffer. Register it with the cleanup stack. */
    npl_cleanup_push(free, nodes);
    uint32_t index = 0;
    for (NplVal rest = source;
         npl_is_ptr(rest) &&
         npl_header(rest)->tag == BOX_PARAMETER_BINDING;
         rest = ((NplParameterBinding *)npl_header(rest))->parent)
        nodes[index++] = rest;

    NplVal parent = npl_nil();
    npl_gc_root(&parent);
    for (uint32_t i = length; i > 0; i--) {
        NplParameterBinding *src =
            (NplParameterBinding *)npl_header(nodes[i - 1]);
        NplParameterBinding *copy = (NplParameterBinding *)
            npl_alloc_object(sizeof(NplParameterBinding));
        copy->header.tag = BOX_PARAMETER_BINDING;
        copy->header.flags = 0;
        copy->parent = parent;
        copy->parameter = src->parameter;
        copy->value = src->value;
        copy->owner = npl_binding_owner();
        parent = boxed(copy);
    }
    npl_gc_unroot(&parent);
    npl_cleanup_pop();
    free(nodes);
    return parent;
}

/* T2b: a new virtual thread inherits its creator's parameter chain under
 * its own (the carrier's) ownership. Unlike the SRFI 18 thread-start clone,
 * this preserves chain direction (head = innermost binding). */
NplVal npl_clone_parameter_bindings(NplVal chain)
{
    return clone_parameter_bindings(chain);
}

/* A virtual thread keeps the SAME owner identity across carrier migration,
 * so no re-stamping is needed for migration itself; this helper is kept for
 * re-homing a chain to a specific logical thread. */
uintptr_t npl_binding_owner(void)
{
    void *logical = CTX->logical_thread;
    return logical ? (uintptr_t)logical : (uintptr_t)npl_ctx()->id;
}

/* The chain is private to the virtual thread (cloned at its first mount),
 * and the per-thread parameter_extents mapping points into this same chain,
 * so a single walk covers both. */
void npl_relocalize_parameter_bindings(NplVal chain, uintptr_t owner)
{
    for (NplVal rest = chain;
         npl_is_ptr(rest) &&
         npl_header(rest)->tag == BOX_PARAMETER_BINDING;
         rest = ((NplParameterBinding *)npl_header(rest))->parent) {
        NplParameterBinding *binding =
            (NplParameterBinding *)npl_header(rest);
        binding->owner = owner;
    }
}

static uint32_t binding_chain_length(NplVal source)
{
    uint32_t length = 0;
    for (NplVal rest = source;
         npl_is_ptr(rest) &&
         npl_header(rest)->tag == BOX_PARAMETER_BINDING;
         rest = ((NplParameterBinding *)npl_header(rest))->parent)
        length++;
    return length;
}

static int binding_chain_owned(NplVal source)
{
    uintptr_t owner = npl_binding_owner();
    for (NplVal rest = source;
         npl_is_ptr(rest) &&
         npl_header(rest)->tag == BOX_PARAMETER_BINDING;
         rest = ((NplParameterBinding *)npl_header(rest))->parent) {
        if (((NplParameterBinding *)npl_header(rest))->owner != owner)
            return 0;
    }
    return 1;
}

/* One (src . copy) association. `owner` is the heap object holding *cache
 * when it needs a write barrier, or NULL for a C-stack local. */
static void cache_put(NplVal *cache, void *owner, NplVal src, NplVal copy)
{
    NplVal entry = npl_cons(src, copy);
    NplVal pair = npl_cons(entry, *cache);
    *cache = pair;
    if (owner) npl_gc_write_barrier(owner, pair);
}

static NplVal cache_find(NplVal cache, NplVal src)
{
    for (NplVal rest = cache;
         npl_is_ptr(rest) && npl_header(rest)->tag == BOX_PAIR;
         rest = npl_pair_ptr(rest)->cdr) {
        NplVal entry = npl_pair_ptr(rest)->car;
        if (npl_pair_ptr(entry)->car == src)
            return npl_pair_ptr(entry)->cdr;
    }
    return npl_nil();
}

/* A continuation may be re-entered on the same destination thread after its
 * localized parameter bindings have been mutated. Seed the next transfer's
 * cache from those existing extent mappings so source bindings resolve to
 * the same destination nodes. The wind frames retain the source extent key
 * and source chain needed to pair each binding with its localized copy. */
static void seed_extent_cache(NplVal target, NplVal *cache)
{
    for (NplVal frame_value = target; !npl_is_nil(frame_value);) {
        NplWindFrame *frame =
            require_wind_frame(frame_value, "continuation");
        NplVal key = frame->extent_key;
        if (!npl_is_nil(key)) {
            NplVal entry = extent_lookup(key);
            if (!npl_is_nil(entry)) {
                NplVal source = frame->parameters;
                NplVal local = extent_local(entry);
                uint32_t count = ((NplVector *)npl_header(key))->length;
                for (uint32_t i = 0; i < count &&
                     npl_is_ptr(source) &&
                     npl_header(source)->tag == BOX_PARAMETER_BINDING &&
                     npl_is_ptr(local) &&
                     npl_header(local)->tag == BOX_PARAMETER_BINDING; i++) {
                    if (npl_is_nil(cache_find(*cache, source)))
                        cache_put(cache, NULL, source, local);
                    source = ((NplParameterBinding *)
                        npl_header(source))->parent;
                    local = ((NplParameterBinding *)
                        npl_header(local))->parent;
                }
            }
        }
        frame_value = frame->parent;
    }
}

static NplVal make_local_binding(NplVal src, NplVal parent)
{
    NplParameterBinding *s = (NplParameterBinding *)npl_header(src);
    NplParameterBinding *copy = (NplParameterBinding *)
        npl_alloc_object(sizeof(NplParameterBinding));
    copy->header.tag = BOX_PARAMETER_BINDING;
    copy->header.flags = 0;
    copy->parent = parent;
    copy->parameter = s->parameter;
    copy->value = s->value;
    copy->owner = npl_binding_owner();
    return boxed(copy);
}

/* Localize a binding chain with a per-transfer (src . copy) cache so a
 * binding shared by the destination chain and a wind frame resolves to ONE
 * node. `seed` builds the cache for the destination chain; `owner` is the
 * transition object when the cache lives on the heap (write barrier).
 * Preserves chain direction (head = innermost). */
static NplVal localize_cached_chain(NplVal source, NplVal *cache,
                                    void *owner, int seed, int reuse_source,
                                    uint32_t binding_count,
                                    NplVal *baseline_out)
{
    uint32_t length = binding_chain_length(source);
    NplVal *nodes = (NplVal *)malloc(
        (size_t)(length ? length : 1) * sizeof(NplVal));
    if (!nodes) npl_runtime_oom_internal();
    npl_cleanup_push(free, nodes);
    uint32_t index = 0;
    for (NplVal rest = source;
         npl_is_ptr(rest) &&
         npl_header(rest)->tag == BOX_PARAMETER_BINDING;
         rest = ((NplParameterBinding *)npl_header(rest))->parent)
        nodes[index++] = rest;

    NplVal parent_copy = npl_nil();
    NplVal head_copy = npl_nil();
    for (uint32_t i = length; i > 0; i--) {
        NplVal src = nodes[i - 1];
        NplVal copy = seed ? npl_nil() : cache_find(*cache, src);
        if (npl_is_nil(copy)) {
            copy = reuse_source ? src : make_local_binding(src, parent_copy);
            cache_put(cache, owner, src, copy);
        }
        parent_copy = copy;
        head_copy = copy;
    }
    npl_cleanup_pop();
    free(nodes);

    NplVal baseline = head_copy;
    for (uint32_t i = 0; i < binding_count &&
         npl_is_ptr(baseline) &&
         npl_header(baseline)->tag == BOX_PARAMETER_BINDING; i++)
        baseline = ((NplParameterBinding *)npl_header(baseline))->parent;
    *baseline_out = baseline;
    return head_copy;
}

/* User handler frames are kept by identity: npl_handler_pop validates
 * current_handler == frame, so rebuilding frames would break re-entry. The
 * source thread's initial handler frame is resolved at raise time against
 * the invoking thread's own initial handler (see npl_raise in control.c). */
static NplVal localize_handler(NplVal source)
{
    return source;
}

static NplVal find_initial_handler(NplVal chain)
{
    for (NplVal rest = chain;
         npl_is_ptr(rest) &&
         npl_header(rest)->tag == BOX_HANDLER_FRAME;
         rest = ((NplHandlerFrame *)npl_header(rest))->parent) {
        if (npl_header(rest)->flags & NPL_FLAG_INITIAL_HANDLER)
            return rest;
    }
    return npl_nil();
}

NplVal npl_capture_dynamic_context(void)
{
    NplDynamicContext *context = (NplDynamicContext *)
        npl_alloc_object(sizeof(NplDynamicContext));
    context->header.tag = BOX_DYNAMIC_CONTEXT;
    context->header.flags = 0;
    context->wind = CTX->current_wind;
    context->handler = CTX->current_handler;
    context->parameters = CTX->current_parameters;
    context->source_initial_handler =
        find_initial_handler(CTX->current_handler);
    return boxed(context);
}

/* ---- parameterize primitives (moved from control.c) ---- */

NplVal npl_parameters_enter(NplVal parameters_value, NplVal values_value,
                            NplVal frames_value)
{
    NplVector *parameters = require_parameter_vector(parameters_value);
    NplVector *values = require_parameter_vector(values_value);
    NplVector *frames = require_parameter_vector(frames_value);
    if (parameters->length != values->length ||
        parameters->length != frames->length) {
        fprintf(stderr, "parameterize: inconsistent binding vectors\n");
        exit(1);
    }
    /* npl_wind_push runs in the before thunk's continuation (control.npl);
     * publish the extent key for it. A transfer never pushes a frame and
     * discards this at the start of its next step. */
    CTX->pending_extent_key = frames_value;

    NplVal entry = extent_lookup(frames_value);
    if (!npl_is_nil(entry)) {
        /* Re-entry, or a transfer that already localized this extent: reuse
         * the thread-owned chain so in-place mutations survive. */
        CTX->current_parameters = extent_local(entry);
        return npl_nil();
    }

    NplVal baseline = CTX->current_parameters;
    NplVal local = baseline;
    for (uint32_t i = 0; i < parameters->length; i++) {
        (void)require_parameter(parameters->data[i], "parameterize");
        NplParameterBinding *binding = (NplParameterBinding *)
            npl_alloc_object(sizeof(NplParameterBinding));
        binding->header.tag = BOX_PARAMETER_BINDING;
        binding->header.flags = 0;
        binding->parent = local;
        binding->parameter = parameters->data[i];
        binding->value = values->data[i];
        binding->owner = npl_binding_owner();
        local = boxed(binding);
    }
    CTX->current_parameters = local;
    extent_register(frames_value, local, baseline);
    return npl_nil();
}

NplVal npl_parameters_leave(NplVal frames_value)
{
    (void)require_parameter_vector(frames_value);
    NplVal entry = extent_lookup(frames_value);
    if (!npl_is_nil(entry))
        CTX->current_parameters = extent_baseline(entry);
    /* No mapping means the environment was installed by a wind transfer,
     * which already restored the enclosing environment for this step. */
    return npl_nil();
}

/* ---- wind transfer (moved from control.c, extended for SRFI 18) ---- */

static uint32_t wind_depth(NplVal value)
{
    uint32_t depth = 0;
    while (!npl_is_nil(value)) {
        value = require_wind_frame(value, "continuation")->parent;
        if (depth == UINT32_MAX) npl_runtime_oom_internal();
        depth++;
    }
    return depth;
}

static NplVal wind_parent(NplVal value)
{
    return require_wind_frame(value, "continuation")->parent;
}

static NplVal wind_common_ancestor(NplVal source, NplVal target)
{
    uint32_t source_depth = wind_depth(source);
    uint32_t target_depth = wind_depth(target);
    while (source_depth > target_depth) {
        source = wind_parent(source);
        source_depth--;
    }
    while (target_depth > source_depth) {
        target = wind_parent(target);
        target_depth--;
    }
    while (source != target) {
        source = wind_parent(source);
        target = wind_parent(target);
    }
    return source;
}

static NplVal wind_transition_resume(NplVal environment, int64_t argc,
                                     NplVal *argv);

/* Install the dynamic environment of one extent before running its thunk
 * (SRFI 18: each before/after thunk runs in the environment in effect when
 * the corresponding dynamic-wind was executed). Legacy transfers have no
 * captured context and keep the old behaviour. */
static void install_frame_environment(NplWindTransition *transition,
                                      NplWindFrame *frame)
{
    if (npl_is_nil(transition->context)) return;
    CTX->current_handler = frame->handler;
    NplVal key = frame->extent_key;
    uint32_t count = 0;
    if (npl_is_ptr(key) && npl_header(key)->tag == BOX_VECTOR)
        count = ((NplVector *)npl_header(key))->length;
    NplVal baseline;
    /* Reuse the transfer's (src . copy) cache: a binding shared with the
     * destination chain must stay the SAME node, so a parameter-set! in this
     * before/after thunk is visible to the continuation body. */
    NplVal local = localize_cached_chain(
        frame->parameters, &transition->cache, transition, 0, 0,
        count, &baseline);
    CTX->current_parameters = local;
    if (!npl_is_nil(key))
        extent_register(key, local, baseline);
}

static void schedule_wind_transition(NplVal transition_value)
{
    NplWindTransition *transition =
        (NplWindTransition *)npl_header(transition_value);
    /* A before thunk may publish a pending extent key; a transfer never
     * pushes a wind frame, so discard anything left from the last step. */
    CTX->pending_extent_key = npl_nil();

    if (!npl_is_nil(transition->entering)) {
        CTX->current_wind = transition->entering;
        transition->entering = npl_nil();
    }

    NplVal thunk = npl_nil();
    if (transition->exit_index < transition->exit_count) {
        NplVal frame_value = transition->frames[transition->exit_index++];
        NplWindFrame *frame = require_wind_frame(frame_value, "continuation");
        install_frame_environment(transition, frame);
        CTX->current_wind = frame->parent;
        thunk = frame->after;
    } else if (transition->entry_index < transition->entry_count) {
        CTX->current_handler = transition->target_handler;
        uint32_t reverse = transition->entry_count -
                           transition->entry_index - 1;
        NplVal frame_value =
            transition->frames[transition->exit_count + reverse];
        NplWindFrame *frame = require_wind_frame(frame_value, "continuation");
        transition->entry_index++;
        transition->entering = frame_value;
        npl_gc_write_barrier(transition, frame_value);
        install_frame_environment(transition, frame);
        thunk = frame->before;
    } else {
        CTX->current_wind = transition->target;
        CTX->current_handler = transition->target_handler;
        if (!npl_is_nil(transition->context))
            CTX->current_parameters = transition->target_parameters;
        NplVal argument = transition->value;
        npl_tail_request_closure(transition->continuation, 1, &argument);
        return;
    }

    NplVal resume = npl_make_closure(
        wind_transition_resume, transition_value, 1);
    npl_tail_request_closure(thunk, 1, &resume);
}

static NplVal wind_transition_resume(NplVal environment, int64_t argc,
                                     NplVal *argv)
{
    (void)argv;
    if (argc != 1 || !npl_is_ptr(environment) ||
        npl_header(environment)->tag != BOX_WIND_TRANSITION) {
        fprintf(stderr, "dynamic-wind: invalid transition callback\n");
        exit(1);
    }
    schedule_wind_transition(environment);
    return npl_nil();
}

static NplVal make_transition(NplVal target, NplVal target_handler,
                              NplVal continuation, NplVal value,
                              NplVal context, NplVal target_parameters,
                              NplVal cache)
{
    NplVal common = wind_common_ancestor(CTX->current_wind, target);
    uint32_t exit_count = 0;
    for (NplVal frame = CTX->current_wind; frame != common;
         frame = wind_parent(frame))
        exit_count++;
    uint32_t entry_count = 0;
    for (NplVal frame = target; frame != common;
         frame = wind_parent(frame))
        entry_count++;

    size_t total = (size_t)exit_count + entry_count;
    NplWindTransition *transition = (NplWindTransition *)npl_alloc_object(
        sizeof(NplWindTransition) + total * sizeof(NplVal));
    transition->header.tag = BOX_WIND_TRANSITION;
    transition->header.flags = 0;
    transition->exit_count = exit_count;
    transition->exit_index = 0;
    transition->entry_count = entry_count;
    transition->entry_index = 0;
    transition->target = target;
    transition->target_handler = target_handler;
    transition->continuation = continuation;
    transition->value = value;
    transition->entering = npl_nil();
    transition->context = context;
    transition->target_parameters = target_parameters;
    transition->cache = cache;

    size_t index = 0;
    for (NplVal frame = CTX->current_wind; frame != common;
         frame = wind_parent(frame))
        transition->frames[index++] = frame;
    for (NplVal frame = target; frame != common;
         frame = wind_parent(frame))
        transition->frames[index++] = frame;
    return boxed(transition);
}

static void transfer_legacy(NplVal target, NplVal target_handler,
                            NplVal continuation, NplVal value)
{
    if (!npl_is_nil(target))
        (void)require_wind_frame(target, "continuation");
    if (!npl_is_nil(target_handler) &&
        (!npl_is_ptr(target_handler) ||
         npl_header(target_handler)->tag != BOX_HANDLER_FRAME)) {
        fprintf(stderr, "continuation: invalid handler state\n");
        exit(1);
    }
    if (CTX->current_wind == target) {
        CTX->current_handler = target_handler;
        npl_tail_request_closure(continuation, 1, &value);
        return;
    }
    schedule_wind_transition(make_transition(
        target, target_handler, continuation, value,
        npl_nil(), npl_nil(), npl_nil()));
}

static void transfer_with_context(NplVal context_value,
                                  NplVal continuation, NplVal value)
{
    if (!npl_is_ptr(context_value) ||
        npl_header(context_value)->tag != BOX_DYNAMIC_CONTEXT) {
        fprintf(stderr, "continuation: invalid dynamic context\n");
        exit(1);
    }
    NplDynamicContext *context =
        (NplDynamicContext *)npl_header(context_value);
    NplVal target = context->wind;
    if (!npl_is_nil(target))
        (void)require_wind_frame(target, "continuation");
    /* Build ONE localized destination chain and record (src . copy) for
     * every binding. install_frame_environment later reuses these copies for
     * the wind frames, so shared bindings are not cloned twice. */
    NplVal cache = npl_nil();
    seed_extent_cache(target, &cache);
    NplVal ignored_baseline;
    int reuse_source = binding_chain_owned(context->parameters);
    NplVal target_parameters = localize_cached_chain(
        context->parameters, &cache, NULL, 0, reuse_source,
        0, &ignored_baseline);
    NplVal target_handler = localize_handler(context->handler);

    if (CTX->current_wind == target) {
        CTX->current_handler = target_handler;
        CTX->current_parameters = target_parameters;
        npl_tail_request_closure(continuation, 1, &value);
        return;
    }
    schedule_wind_transition(make_transition(
        target, target_handler, continuation, value,
        context_value, target_parameters, cache));
}

/* The first argument is a BOX_DYNAMIC_CONTEXT for a compiler emitted after
 * the relay, and the old target-wind value for a legacy seed. Keeping the
 * 4-argument shape lets a pre-relay compiler (and npli) link against this
 * runtime without an ABI break. */
void npl_wind_transfer(NplVal first, NplVal second,
                       NplVal continuation, NplVal value)
{
    if (npl_is_ptr(first) &&
        npl_header(first)->tag == BOX_DYNAMIC_CONTEXT) {
        transfer_with_context(first, continuation, value);
        return;
    }
    transfer_legacy(first, second, continuation, value);
}

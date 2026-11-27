/*
 * segment.c — T4-A compiler-emitted continuation segments.
 *
 * The CPS frontend wraps the local continuation closure at every non-direct
 * user procedure call boundary in a BOX_CONTINUATION_SEGMENT.  A segment is
 * an immutable semantic frame: it holds the raw CPS closure, the parent
 * segment (the continuation that receives this frame's result) and the
 * nearest enclosing prompt at creation.  Segments are callable exactly like
 * the closure they wrap, so compiled code and the evaluator's native control
 * operators use one representation.
 *
 * Creation is conditional on an active prompt: without a delimiter no
 * prompt operation can capture a prefix, so the ordinary call path keeps the
 * raw closure and pays no allocation.
 */
#include "npl.h"
#include "thread.h"

#define CTX npl_ctx()

static int segment_tagged(NplVal value)
{
    return npl_is_ptr(value) &&
           npl_header(value)->tag == BOX_CONTINUATION_SEGMENT;
}

NplVal npl_segment_make(NplVal closure)
{
    if (!npl_is_ptr(closure) || npl_header(closure)->tag != BOX_CLOSURE)
        return closure;
    if (npl_is_nil(CTX->current_prompts))
        return closure;
    NplContinuationSegment *segment = (NplContinuationSegment *)
        npl_alloc_object(sizeof(NplContinuationSegment));
    segment->header.tag = BOX_CONTINUATION_SEGMENT;
    segment->header.flags = 0;
    segment->closure = closure;
    segment->parent = CTX->current_segment;
    segment->origin_prompt = CTX->current_prompts;
    segment->id = CTX->next_segment_id++;
    return (NplVal)(uintptr_t)segment | TAG_PTR;
}

NplVal npl_segment_p(NplVal value)
{
    return segment_tagged(value) ? npl_true() : npl_false();
}

NplVal npl_segment_current(void)
{
    return CTX->current_segment;
}

static NplContinuationSegment *require_segment(NplVal value, const char *who)
{
    if (!segment_tagged(value))
        npl_primitive_error(who, "not a continuation segment", value);
    return (NplContinuationSegment *)npl_header(value);
}

NplVal npl_segment_parent(NplVal value)
{
    return require_segment(value, "segment-parent")->parent;
}

NplVal npl_segment_closure(NplVal value)
{
    return require_segment(value, "segment-closure")->closure;
}

NplVal npl_segment_depth(NplVal value)
{
    uint64_t depth = 0;
    while (segment_tagged(value)) {
        depth++;
        value = ((NplContinuationSegment *)npl_header(value))->parent;
    }
    return npl_fixnum((int64_t)depth);
}

NplVal npl_graft_make(NplVal procedure, NplVal arguments,
                      NplVal prompt, NplVal target)
{
    NplContinuationGraft *graft = (NplContinuationGraft *)
        npl_alloc_object(sizeof(NplContinuationGraft));
    graft->header.tag = BOX_CONTINUATION_GRAFT;
    graft->header.flags = 0;
    graft->procedure = procedure;
    graft->arguments = arguments;
    graft->prompt = prompt;
    graft->target = target;
    return (NplVal)(uintptr_t)graft | TAG_PTR;
}

NplVal npl_graft_p(NplVal value)
{
    if (!npl_is_ptr(value)) return npl_false();
    return npl_header(value)->tag == BOX_CONTINUATION_GRAFT
               ? npl_true()
               : npl_false();
}

static NplContinuationGraft *require_graft(NplVal value, const char *who)
{
    if (!npl_is_ptr(value) ||
        npl_header(value)->tag != BOX_CONTINUATION_GRAFT)
        npl_primitive_error(who, "not a continuation graft", value);
    return (NplContinuationGraft *)npl_header(value);
}

NplVal npl_graft_procedure(NplVal value)
{
    return require_graft(value, "graft-procedure")->procedure;
}

NplVal npl_graft_arguments(NplVal value)
{
    return require_graft(value, "graft-arguments")->arguments;
}

NplVal npl_graft_prompt(NplVal value)
{
    return require_graft(value, "graft-prompt")->prompt;
}

NplVal npl_graft_target(NplVal value)
{
    return require_graft(value, "graft-target")->target;
}

NplVal npl_continuation_make(NplVal procedure, NplVal kind, NplVal origin,
                             NplVal tag, NplVal prompts, NplVal graft)
{
    NplContinuation *continuation = (NplContinuation *)
        npl_alloc_object(sizeof(NplContinuation));
    continuation->header.tag = BOX_CONTINUATION;
    continuation->header.flags = 0;
    continuation->procedure = procedure;
    continuation->kind = kind;
    continuation->origin = origin;
    continuation->tag = tag;
    continuation->prompts = prompts;
    continuation->graft = graft;
    return (NplVal)(uintptr_t)continuation | TAG_PTR;
}

static NplContinuation *require_continuation(NplVal value, const char *who)
{
    if (!npl_is_ptr(value) || npl_header(value)->tag != BOX_CONTINUATION)
        npl_primitive_error(who, "not a continuation", value);
    return (NplContinuation *)npl_header(value);
}

NplVal npl_continuation_p(NplVal value)
{
    return npl_is_ptr(value) && npl_header(value)->tag == BOX_CONTINUATION
               ? npl_true()
               : npl_false();
}

NplVal npl_continuation_kind(NplVal value)
{
    return require_continuation(value, "continuation-kind")->kind;
}

NplVal npl_continuation_origin(NplVal value)
{
    return require_continuation(value, "continuation-origin")->origin;
}

NplVal npl_continuation_tag(NplVal value)
{
    return require_continuation(value, "continuation-tag")->tag;
}

NplVal npl_continuation_prompts(NplVal value)
{
    return require_continuation(value, "continuation-prompts")->prompts;
}

NplVal npl_continuation_graft(NplVal value)
{
    return require_continuation(value, "continuation-graft")->graft;
}

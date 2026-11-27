/*
 * prompt.c — T4-A runtime prompt frames and per-thread meta-continuations.
 *
 * A prompt frame is part of the runtime dynamic context: call/cc captures the
 * current chain in NplDynamicContext and npl_wind_transfer reinstates it on the
 * invoking thread.  This is the shared semantic boundary used by compiled code
 * and the evaluator; the library-level prompt operators no longer hide prompt
 * state in a parameter or in user thread-specific fields.
 *
 * A same-thread composable/non-composable invocation installs a one-shot
 * override in the invoking thread's context.  The prompt terminal consumes it,
 * so the captured prefix returns to that invocation while a later return
 * through the same prompt uses the ordinary continuation.
 */
#include "npl.h"
#include "thread.h"

#define CTX npl_ctx()

static NplPromptFrame *require_prompt(NplVal value, const char *who)
{
    if (!npl_is_ptr(value) || npl_header(value)->tag != BOX_PROMPT_FRAME)
        npl_primitive_error(who, "not a prompt frame", value);
    return (NplPromptFrame *)npl_header(value);
}

NplVal npl_prompt_make(NplVal tag, NplVal handler)
{
    NplPromptFrame *frame =
        (NplPromptFrame *)npl_alloc_object(sizeof(NplPromptFrame));
    frame->header.tag = BOX_PROMPT_FRAME;
    frame->header.flags = 0;
    frame->tag = tag;
    frame->handler = handler;
    frame->escape = npl_nil();
    frame->return_ = npl_nil();
    frame->parent = npl_nil();
    return (NplVal)(uintptr_t)frame | TAG_PTR;
}

NplVal npl_prompt_p(NplVal value)
{
    return npl_is_ptr(value) && npl_header(value)->tag == BOX_PROMPT_FRAME
               ? npl_true()
               : npl_false();
}

NplVal npl_prompt_tag(NplVal value)
{
    return require_prompt(value, "prompt-tag")->tag;
}

NplVal npl_prompt_handler(NplVal value)
{
    return require_prompt(value, "prompt-handler")->handler;
}

NplVal npl_prompt_escape(NplVal value)
{
    return require_prompt(value, "prompt-escape")->escape;
}

NplVal npl_prompt_set_escape(NplVal value, NplVal escape)
{
    NplPromptFrame *frame = require_prompt(value, "prompt-escape-set!");
    frame->escape = escape;
    npl_gc_write_barrier(frame, escape);
    return npl_unspecified();
}

NplVal npl_prompt_return(NplVal value)
{
    return require_prompt(value, "prompt-return")->return_;
}

NplVal npl_prompt_set_return(NplVal value, NplVal return_value)
{
    NplPromptFrame *frame = require_prompt(value, "prompt-return-set!");
    frame->return_ = return_value;
    npl_gc_write_barrier(frame, return_value);
    return npl_unspecified();
}

NplVal npl_prompt_push(NplVal value)
{
    NplPromptFrame *frame = require_prompt(value, "prompt-push!");
    frame->parent = CTX->current_prompts;
    CTX->current_prompts = value;
    return npl_unspecified();
}

NplVal npl_prompt_pop(NplVal value)
{
    NplPromptFrame *frame = require_prompt(value, "prompt-pop!");
    if (CTX->current_prompts == value)
        CTX->current_prompts = frame->parent;
    return npl_unspecified();
}

NplVal npl_prompt_find(NplVal tag)
{
    for (NplVal rest = CTX->current_prompts;
         npl_is_ptr(rest) && npl_header(rest)->tag == BOX_PROMPT_FRAME;
         rest = ((NplPromptFrame *)npl_header(rest))->parent) {
        if (((NplPromptFrame *)npl_header(rest))->tag == tag)
            return rest;
    }
    return npl_false();
}

NplVal npl_prompt_current(void)
{
    return CTX->current_prompts;
}

static NplVal prompt_list(NplVal frame, NplVal origin)
{
    if (!npl_is_ptr(frame) || npl_header(frame)->tag != BOX_PROMPT_FRAME)
        return npl_nil();
    NplVal parent = ((NplPromptFrame *)npl_header(frame))->parent;
    if (frame == origin)
        return npl_cons(frame, npl_nil());
    return npl_cons(frame, prompt_list(parent, origin));
}

NplVal npl_prompt_list_current(void)
{
    return prompt_list(CTX->current_prompts, npl_nil());
}

NplVal npl_prompt_list_from(NplVal origin)
{
    if (!npl_is_ptr(origin) ||
        npl_header(origin)->tag != BOX_PROMPT_FRAME)
        return npl_nil();
    return prompt_list(CTX->current_prompts, origin);
}

NplVal npl_prompt_install_override(NplVal prompt, NplVal target)
{
    (void)require_prompt(prompt, "prompt-install-override!");
    NplVal entry = npl_cons(prompt, target);
    CTX->prompt_overrides = npl_cons(entry, CTX->prompt_overrides);
    return npl_unspecified();
}

NplVal npl_prompt_take_return(NplVal prompt)
{
    (void)require_prompt(prompt, "prompt-take-return!");
    NplVal previous = npl_false();
    NplVal rest = CTX->prompt_overrides;
    while (npl_is_ptr(rest) && npl_header(rest)->tag == BOX_PAIR) {
        NplVal entry = npl_car(rest);
        NplVal next = npl_cdr(rest);
        if (npl_is_ptr(entry) && npl_header(entry)->tag == BOX_PAIR &&
            npl_car(entry) == prompt) {
            if (npl_is_false(previous))
                CTX->prompt_overrides = next;
            else
                npl_set_cdr(previous, next);
            return npl_cdr(entry);
        }
        previous = rest;
        rest = next;
    }
    return npl_false();
}

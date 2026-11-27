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
#include "thread-sched.h"
#include <stdlib.h>

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
    frame->origin = npl_nil();
    frame->barrier = 0;
    frame->root = 0;
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
    frame->origin = CTX->current_segment;
    npl_gc_write_barrier(frame, frame->parent);
    npl_gc_write_barrier(frame, frame->origin);
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

/* T4-B0: activation-local splice from the captured delimiter to the
 * invoking continuation's prompt chain.  Entries are (origin . tail); the
 * head is the innermost activation. */
NplVal npl_prompt_splice_next(NplVal frame)
{
    for (NplVal rest = CTX->prompt_splices;
         npl_is_ptr(rest) && npl_header(rest)->tag == BOX_PAIR;
         rest = npl_pair_ptr(rest)->cdr) {
        NplVal entry = npl_pair_ptr(rest)->car;
        if (npl_is_ptr(entry) && npl_header(entry)->tag == BOX_PAIR &&
            npl_pair_ptr(entry)->car == frame)
            return npl_pair_ptr(entry)->cdr;
    }
    return npl_false();
}

NplVal npl_prompt_install_splice(NplVal origin, NplVal tail)
{
    (void)require_prompt(origin, "prompt-install-splice!");
    NplVal entry = npl_cons(origin, tail);
    CTX->prompt_splices = npl_cons(entry, CTX->prompt_splices);
    return npl_unspecified();
}

NplVal npl_prompt_remove_splice(NplVal origin)
{
    NplVal previous = npl_false();
    NplVal rest = CTX->prompt_splices;
    while (npl_is_ptr(rest) && npl_header(rest)->tag == BOX_PAIR) {
        NplVal entry = npl_pair_ptr(rest)->car;
        NplVal next = npl_pair_ptr(rest)->cdr;
        if (npl_is_ptr(entry) && npl_header(entry)->tag == BOX_PAIR &&
            npl_pair_ptr(entry)->car == origin) {
            if (npl_is_false(previous))
                CTX->prompt_splices = next;
            else
                npl_set_cdr(previous, next);
            return npl_unspecified();
        }
        previous = rest;
        rest = next;
    }
    return npl_unspecified();
}

static NplVal prompt_next(NplVal frame)
{
    NplVal tail = npl_prompt_splice_next(frame);
    if (npl_is_false(tail))
        return ((NplPromptFrame *)npl_header(frame))->parent;
    return tail;
}

NplVal npl_prompt_find(NplVal tag)
{
    NplVal rest = CTX->current_prompts;
    while (npl_is_ptr(rest) && npl_header(rest)->tag == BOX_PROMPT_FRAME) {
        if (((NplPromptFrame *)npl_header(rest))->tag == tag)
            return rest;
        rest = prompt_next(rest);
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
    if (frame == origin)
        return npl_cons(frame, npl_nil());
    return npl_cons(frame, prompt_list(prompt_next(frame), origin));
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

NplVal npl_prompt_origin(NplVal value)
{
    return require_prompt(value, "prompt-origin")->origin;
}

NplVal npl_prompt_set_barrier(NplVal value, NplVal flag)
{
    NplPromptFrame *frame = require_prompt(value, "prompt-set-barrier!");
    frame->barrier = npl_is_false(flag) ? 0 : 1;
    return npl_unspecified();
}

NplVal npl_prompt_barrier_p(NplVal value)
{
    return require_prompt(value, "prompt-barrier?")->barrier
               ? npl_true()
               : npl_false();
}

NplVal npl_prompt_root_p(NplVal value)
{
    return require_prompt(value, "prompt-root?")->root
               ? npl_true()
               : npl_false();
}

NplVal npl_prompt_override_total(void)
{
    int64_t count = 0;
    for (NplVal rest = CTX->prompt_overrides;
         npl_is_ptr(rest) && npl_header(rest)->tag == BOX_PAIR;
         rest = npl_cdr(rest))
        count++;
    return npl_fixnum(count);
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
            /* A composable activation's splice lives exactly as long as its
             * one-shot return relation at this prompt. */
            npl_prompt_remove_splice(prompt);
            return npl_cdr(entry);
        }
        previous = rest;
        rest = next;
    }
    npl_prompt_remove_splice(prompt);
    return npl_false();
}

/* ---- default prompt root (T4-A) ---- */

static NplVal g_default_prompt_tag = 0;

/* Initialized by program entry (or the first prompt in a C harness) before
 * user threads run.  The allocation intentionally stays outside pthread_once:
 * npl_alloc_object may poll and collect, and running that inside the once
 * guard would invert the allocator/GC lock order. */
NplVal npl_default_prompt_tag(void)
{
    if (g_default_prompt_tag == 0) {
        NplPromptTag *tag =
            (NplPromptTag *)npl_alloc_object(sizeof(NplPromptTag));
        tag->header.tag = BOX_PROMPT_TAG;
        tag->header.flags = NPL_FLAG_LITERAL;
        g_default_prompt_tag = (NplVal)(uintptr_t)tag | TAG_PTR;
    }
    return g_default_prompt_tag;
}

static NplVal default_root_return(NplVal environment, int64_t argc,
                                  NplVal *argv)
{
    if (argc != 1 || !npl_is_ptr(environment) ||
        npl_header(environment)->tag != BOX_PROMPT_FRAME) {
        fprintf(stderr, "default prompt: invalid root return\n");
        exit(1);
    }
    NplPromptFrame *prompt = (NplPromptFrame *)npl_header(environment);
    NplVal target = npl_prompt_take_return(environment);
    if (CTX->current_prompts == environment)
        CTX->current_prompts = prompt->parent;
    NplVal continuation = npl_is_false(target) ? prompt->return_ : target;
    npl_tail_request_closure(continuation, 1, &argv[0]);
    return npl_nil();
}

void npl_default_prompt_mark_roots(void)
{
    if (g_default_prompt_tag != 0)
        npl_gc_mark_candidate_internal(g_default_prompt_tag);
}

NplVal npl_default_prompt_run(NplVal body, NplVal halt)
{
    NplVal prompt = npl_prompt_make(npl_default_prompt_tag(), npl_nil());
    NplPromptFrame *frame = (NplPromptFrame *)npl_header(prompt);
    frame->root = 1;
    frame->escape = halt;
    frame->return_ = halt;
    npl_gc_write_barrier(frame, halt);
    npl_prompt_push(prompt);
    NplVal continuation = npl_make_closure(default_root_return, prompt, 1);
    NplVal arguments[1] = { continuation };
    return npl_apply(body, 1, arguments);
}

static NplVal program_root_exit(NplVal environment, int64_t argc,
                                NplVal *argv)
{
    (void)environment;
    (void)argc;
    (void)argv;
    return npl_fixnum(0);
}

NplVal npl_default_prompt_begin(void)
{
    if (!npl_is_nil(CTX->default_prompt)) return npl_unspecified();
    NplVal prompt = npl_prompt_make(npl_default_prompt_tag(), npl_nil());
    NplPromptFrame *frame = (NplPromptFrame *)npl_header(prompt);
    frame->root = 1;
    frame->escape = npl_make_closure(program_root_exit, npl_nil(), -1);
    frame->return_ = frame->escape;
    /* Capture the environment outside the root prompt: a root abort
     * transfers here, so the default handler runs with the root params,
     * handlers and prompt chain, after every wind has been exited. */
    frame->root_context = npl_capture_dynamic_context();
    npl_prompt_push(prompt);
    CTX->default_prompt = prompt;
    return npl_unspecified();
}

void npl_default_prompt_set_root(NplVal target, uint32_t kind)
{
    if (npl_is_nil(CTX->default_prompt)) return;
    NplPromptFrame *frame =
        (NplPromptFrame *)npl_header(CTX->default_prompt);
    frame->root_target = target;
    frame->root_kind = kind;
}

NplVal npl_prompt_root_kind(NplVal value)
{
    return npl_fixnum((int64_t)require_prompt(value, "prompt-root-kind")
                          ->root_kind);
}

NplVal npl_default_prompt_current(void)
{
    return CTX->default_prompt;
}

NplVal npl_default_prompt_finish_value(NplVal value)
{
    if (npl_is_nil(CTX->default_prompt)) return value;
    NplPromptFrame *frame =
        (NplPromptFrame *)npl_header(CTX->default_prompt);
    NplVal target = npl_prompt_take_return(CTX->default_prompt);
    if (npl_is_false(target)) return value;
    /* The root escape/return closure is CPS-shaped (values plus a trailing
     * continuation); this packed completion delivers one value.  Identity
     * means the ordinary root return, whose caller already owns delivery. */
    if (target == frame->return_ || target == frame->escape)
        return value;
    NplVal argument = value;
    return npl_apply(target, 1, &argument);
}

NplVal npl_default_prompt_finish(NplVal value)
{
    if (npl_is_nil(CTX->default_prompt)) return npl_fixnum(0);
    NplVal target = npl_prompt_take_return(CTX->default_prompt);
    if (npl_is_false(target)) return npl_fixnum(0);
    NplVal argument = value;
    return npl_apply(target, 1, &argument);
}

/* ---- T4-B0 root abort and completion ---- */

static NplVal root_handler_done(NplVal environment, int64_t argc,
                                NplVal *argv)
{
    if (argc != 1 || !npl_is_ptr(environment) ||
        npl_header(environment)->tag != BOX_ROOT_ABORT) {
        fprintf(stderr, "default prompt: invalid handler completion\n");
        exit(1);
    }
    NplRootAbort *abort = (NplRootAbort *)npl_header(environment);
    return npl_prompt_root_finish(abort->prompt, argv[0]);
}

/* Runs after npl_wind_transfer has installed the root environment. */
static NplVal root_abort_entry(NplVal environment, int64_t argc,
                               NplVal *argv)
{
    (void)argc;
    (void)argv;
    if (!npl_is_ptr(environment) ||
        npl_header(environment)->tag != BOX_ROOT_ABORT) {
        fprintf(stderr, "default prompt: invalid root abort\n");
        exit(1);
    }
    NplRootAbort *abort = (NplRootAbort *)npl_header(environment);
    /* The root prompt is no longer active: a handler may install a fresh
     * prompt with the same tag, as SRFI 226's default handler does. */
    if (CTX->default_prompt == abort->prompt)
        CTX->default_prompt = npl_nil();

    uint32_t count = 0;
    for (NplVal rest = abort->arguments; !npl_is_nil(rest);
         rest = npl_cdr(rest)) {
        if (!npl_is_ptr(rest) || npl_header(rest)->tag != BOX_PAIR) {
            fprintf(stderr, "default prompt: arguments are not a list\n");
            exit(1);
        }
        count++;
    }
    NplVal *arguments =
        (NplVal *)malloc((size_t)(count + 1) * sizeof(NplVal));
    if (!arguments) npl_runtime_oom_internal();
    uint32_t index = 0;
    for (NplVal rest = abort->arguments; !npl_is_nil(rest);
         rest = npl_cdr(rest))
        arguments[index++] = npl_car(rest);
    NplVal continuation = npl_make_closure(root_handler_done, environment, 1);
    arguments[count] = continuation;
    npl_tail_request_closure(abort->handler, (int64_t)count + 1, arguments);
    free(arguments);
    return npl_nil();
}

/* The abort primitive returns a closure that performs the transfer.  It is
 * applied in tail position so the transfer's tail request is not overwritten
 * by the caller's normal return continuation. */
static NplVal root_abort_start(NplVal environment, int64_t argc, NplVal *argv)
{
    if (!npl_is_ptr(environment) ||
        npl_header(environment)->tag != BOX_ROOT_ABORT || argc < 1) {
        fprintf(stderr, "default prompt: invalid root abort start\n");
        exit(1);
    }
    NplRootAbort *abort = (NplRootAbort *)npl_header(environment);
    abort->arguments = argv[0];
    npl_wind_transfer(((NplPromptFrame *)npl_header(abort->prompt))->root_context,
                      npl_nil(),
                      npl_make_closure(root_abort_entry, environment, 1),
                      npl_nil());
    return npl_nil();
}

NplVal npl_prompt_root_abort(NplVal prompt, NplVal handler)
{
    NplPromptFrame *frame = require_prompt(prompt, "prompt-root-abort");
    if (!frame->root) {
        fprintf(stderr, "default prompt: root abort on a non-root prompt\n");
        exit(1);
    }
    NplRootAbort *abort =
        (NplRootAbort *)npl_alloc_object(sizeof(NplRootAbort));
    abort->header.tag = BOX_ROOT_ABORT;
    abort->header.flags = 0;
    abort->prompt = prompt;
    abort->handler = handler;
    abort->arguments = npl_nil();
    NplVal abort_value = (NplVal)(uintptr_t)abort | TAG_PTR;
    npl_gc_root(&abort_value);
    NplVal closure = npl_make_closure(root_abort_start, abort_value, 2);
    npl_gc_unroot(&abort_value);
    return closure;
}

NplVal npl_prompt_root_finish(NplVal prompt, NplVal values)
{
    NplPromptFrame *frame = require_prompt(prompt, "prompt-root-finish");
    if (!frame->root) {
        fprintf(stderr, "default prompt: finish on a non-root prompt\n");
        exit(1);
    }
    switch (frame->root_kind) {
    case NPL_PROMPT_ROOT_PROGRAM:
        if (!CTX->program_jump) {
            fprintf(stderr, "default prompt: no program root\n");
            exit(1);
        }
        CTX->program_result = npl_fixnum(0);
        longjmp(*CTX->program_jump, 1);
        break;
    case NPL_PROMPT_ROOT_NATIVE: {
        NplThread *thread =
            (NplThread *)npl_header(npl_thread_current_object());
        thread->end_result = values;
        npl_gc_write_barrier(thread, values);
        if (!thread->armed) {
            fprintf(stderr, "default prompt: native root is not armed\n");
            exit(1);
        }
        longjmp(thread->terminate_jump, 1);
        break;
    }
    case NPL_PROMPT_ROOT_VIRTUAL:
        if (!frame->root_target || npl_is_nil(frame->root_target)) {
            fprintf(stderr, "default prompt: no virtual root\n");
            exit(1);
        }
        npl_vthread_finish_root(frame->root_target, values);
        if (!CTX->mount_jump) {
            fprintf(stderr, "default prompt: no virtual mount root\n");
            exit(1);
        }
        longjmp(*CTX->mount_jump, 1);
        break;
    default:
        fprintf(stderr, "default prompt: unset root kind\n");
        exit(1);
    }
    return npl_nil();
}

/* Generated main runs the program body here so a root completion can unwind
 * nested trampolines (including synchronous library callbacks) to one C
 * landing pad. */
NplVal npl_program_run(NplVal body, NplVal halt)
{
    jmp_buf jump;
    jmp_buf *previous_jump = CTX->program_jump;
    NplVal previous_result = CTX->program_result;
    NplVal previous_prompts = CTX->current_prompts;
    NplVal previous_default = CTX->default_prompt;
    int saved_root_count = npl_gc_root_count_internal();
    uintptr_t saved_stack_bottom = npl_gc_stack_bottom_internal();
    NplVal result;

    CTX->program_jump = &jump;
    CTX->program_result = npl_nil();
    if (setjmp(jump) == 0) {
        npl_default_prompt_begin();
        npl_default_prompt_set_root(npl_nil(), NPL_PROMPT_ROOT_PROGRAM);
        result = npl_apply(body, 1, &halt);
    } else {
        npl_shared_release_all();
        npl_trampoline_release_abandoned();
        npl_gc_restore_root_count_internal(saved_root_count);
        (void)npl_gc_swap_stack_bottom(saved_stack_bottom);
        result = CTX->program_result;
    }
    CTX->program_jump = previous_jump;
    CTX->program_result = previous_result;
    CTX->current_prompts = previous_prompts;
    CTX->default_prompt = previous_default;
    return result;
}

NplVal npl_program_exit_value(void)
{
    return CTX->program_result;
}

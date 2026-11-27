/*
 * virtual-thread-root.c — a virtual thread's saved dynamic state and private
 * CPS root.  Split from virtual-thread.c so the park/await and carrier loops
 * keep one responsibility and this file owns the state-transfer contract.
 */
#include "npl.h"
#include "thread.h"
#include "thread-sched.h"
#include "virtual-thread-internal.h"
#include <stdio.h>
#include <stdlib.h>

#define CTX npl_ctx()

void npl_vthread_save_state(NplVirtualThread *vt)
{
    npl_vthread_set_field(vt, &vt->wind, CTX->current_wind);
    npl_vthread_set_field(vt, &vt->handler, CTX->current_handler);
    npl_vthread_set_field(vt, &vt->parameters, CTX->current_parameters);
    npl_vthread_set_field(vt, &vt->parameter_extents, CTX->parameter_extents);
    npl_vthread_set_field(vt, &vt->pending_extent_key, CTX->pending_extent_key);
    npl_vthread_set_field(vt, &vt->initial_handler, CTX->initial_handler);
    npl_vthread_set_field(vt, &vt->prompts, CTX->current_prompts);
    npl_vthread_set_field(vt, &vt->prompt_overrides, CTX->prompt_overrides);
    npl_vthread_set_field(vt, &vt->prompt_splices, CTX->prompt_splices);
    npl_vthread_set_field(vt, &vt->parameterization, CTX->current_parameterization);
    npl_vthread_set_field(vt, &vt->default_prompt, CTX->default_prompt);
    npl_vthread_set_field(vt, &vt->segment, CTX->current_segment);
    vt->next_segment_id = CTX->next_segment_id;
    vt->next_wind_id = CTX->next_wind_id;
    vt->carrier_context = (void *)npl_ctx();
}

void npl_vthread_restore_state(NplVirtualThread *vt)
{
    /* Establish the logical-thread identity FIRST: parameter binding owners
     * are per-virtual-thread, so any re-stamping must use this vt, not the
     * carrier's context id. The owner is migration-stable, so re-stamping is
     * only needed when a chain came from a different logical thread. */
    CTX->logical_thread = vt;
    if (vt->carrier_context != (void *)npl_ctx())
        npl_relocalize_parameter_bindings(vt->parameters,
                                          (uintptr_t)vt);
    CTX->current_wind = vt->wind;
    CTX->current_handler = vt->handler;
    CTX->current_parameters = vt->parameters;
    CTX->parameter_extents = vt->parameter_extents;
    CTX->pending_extent_key = vt->pending_extent_key;
    CTX->initial_handler = vt->initial_handler;
    CTX->current_prompts = vt->prompts;
    CTX->prompt_overrides = vt->prompt_overrides;
    CTX->prompt_splices = vt->prompt_splices;
    CTX->current_parameterization = vt->parameterization;
    CTX->default_prompt = vt->default_prompt;
    CTX->current_segment = vt->segment;
    CTX->next_segment_id = vt->next_segment_id;
    CTX->next_wind_id = vt->next_wind_id;
}

void npl_vthread_clear_carrier_state(void)
{
    CTX->current_wind = npl_nil();
    CTX->current_handler = npl_nil();
    CTX->current_parameters = npl_nil();
    CTX->parameter_extents = npl_nil();
    CTX->pending_extent_key = npl_nil();
    CTX->initial_handler = npl_nil();
    CTX->current_prompts = npl_nil();
    CTX->prompt_overrides = npl_nil();
    CTX->prompt_splices = npl_nil();
    CTX->current_parameterization = npl_nil();
    CTX->default_prompt = npl_nil();
    CTX->current_segment = npl_nil();
    CTX->next_segment_id = 1;
    CTX->next_wind_id = 1;
    CTX->logical_thread = NULL;
}

/* The thread's initial continuation closes the default prompt: a composable
 * invocation that reaches the thread root returns through its one-shot
 * override, and an abort to the default prompt ends the thread with the
 * handler's values.  argc > 1 distinguishes the CPS call (values plus a
 * trailing continuation) from the packed one-value completion path. */
static NplVal vthread_root_return(NplVal environment, int64_t argc,
                                  NplVal *argv)
{
    NplVal result = argc > 1 ? argv[0] : npl_nil();
    npl_tail_request_closure(environment, 1, &result);
    return npl_nil();
}

static NplVal vthread_root_finish(NplVal environment, int64_t argc,
                                  NplVal *argv)
{
    if (argc != 1) {
        fprintf(stderr, "virtual thread: invalid root result\n");
        exit(1);
    }
    NplVal result = npl_default_prompt_finish_value(argv[0]);
    npl_tail_request_closure(environment, 1, &result);
    return npl_nil();
}

NplVal npl_vthread_root_finish_closure(NplVal root)
{
    return npl_make_closure(vthread_root_finish, root, 1);
}

void npl_vthread_mount_new(NplVirtualThread *vt)
{
    npl_vthread_clear_carrier_state();
    /* The clone stamps bindings with the LOGICAL thread owner, so publish
     * the vt identity before cloning (clear_carrier_state reset it). */
    CTX->logical_thread = vt;
    CTX->current_parameters = npl_clone_parameter_bindings(vt->parameters);
    NplVal handler = npl_make_closure(npl_vthread_uncaught,
                                      npl_boxed_thread(vt), 2);
    CTX->initial_handler = npl_handler_push_initial(handler);
    npl_default_prompt_begin();
    npl_default_prompt_set_root(npl_boxed_thread(vt),
                                NPL_PROMPT_ROOT_VIRTUAL);
    NplVal prompt = npl_default_prompt_current();
    NplVal root_return =
        npl_make_closure(vthread_root_return, vt->root, -1);
    npl_prompt_set_escape(prompt, root_return);
    npl_prompt_set_return(prompt, root_return);
    npl_vthread_save_state(vt);
}

void npl_vthread_finish_root(NplVal value, NplVal result)
{
    if (!npl_is_ptr(value) ||
        npl_header(value)->tag != BOX_VIRTUAL_THREAD) {
        fprintf(stderr, "virtual thread: invalid root target\n");
        exit(1);
    }
    npl_vthread_finish_record((NplVirtualThread *)npl_header(value),
                              result, npl_false());
}

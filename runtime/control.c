/* Runtime control, dynamic state, and tail-call machinery.
 *
 * T1: the wind/handler/parameter chains are per-thread, held in the current
 * NplThreadContext.  The three chains are registered as GC roots once, at
 * thread attach (thread.c), so every thread's dynamic state is visible to a
 * collection.  Parameter *objects* stay process-wide; their bindings below
 * are not. */

#include "npl.h"
#include "thread.h"
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CTX npl_ctx()

/* SRFI 18: the primordial thread also has an initial exception handler.
 * It reports the uncaught object and exits (the same fallback npl_raise used
 * when no handler chain exists). Installing it as a marked initial frame
 * means current-exception-handler returns a procedure on every thread, and a
 * foreign continuation's INITIAL_HANDLER frame is resolved against this
 * thread's own handler instead of running the source thread's termination. */
static NplVal primordial_uncaught(NplVal environment, int64_t argc,
                                  NplVal *argv)
{
    (void)environment;
    (void)argc;
    npl_report_error_object(argv[0]);
    exit(1);
}

void npl_control_init(void)
{
    /* Roots are registered by npl_thread_attach / npl_thread_init_internal. */
    CTX->current_wind = npl_nil();
    CTX->current_handler = npl_nil();
    CTX->current_parameters = npl_nil();
    NplVal handler = npl_make_closure(primordial_uncaught, npl_nil(), 2);
    CTX->initial_handler = npl_handler_push_initial(handler);
}

NplVal npl_make_values(int64_t argc, NplVal *argv)
{
    if (argc == 1) return argv[0];
    if (argc < 0 || (uint64_t)argc > UINT32_MAX) {
        fprintf(stderr, "values: unsupported value count\n");
        exit(1);
    }
    NplValues *values = (NplValues *)npl_alloc_object(
        sizeof(NplValues) + (size_t)argc * sizeof(NplVal));
    values->header.tag = BOX_VALUES;
    values->header.flags = 0;
    values->count = (uint32_t)argc;
    values->capacity = (uint32_t)argc;
    for (int64_t i = 0; i < argc; i++) values->data[i] = argv[i];
    return (NplVal)(uintptr_t)values | TAG_PTR;
}

NplVal npl_list_to_values(NplVal list)
{
    int64_t count = 0;
    for (NplVal rest = list; !npl_is_nil(rest); rest = npl_cdr(rest)) {
        if (!npl_is_ptr(rest) || npl_header(rest)->tag != BOX_PAIR) {
            fprintf(stderr, "continuation: arguments are not a proper list\n");
            exit(1);
        }
        if ((uint64_t)count == UINT32_MAX) npl_runtime_oom_internal();
        count++;
    }
    if (count == 1) return npl_car(list);

    npl_gc_root(&list);
    NplValues *values = (NplValues *)npl_alloc_object(
        sizeof(NplValues) + (size_t)count * sizeof(NplVal));
    values->header.tag = BOX_VALUES;
    values->header.flags = 0;
    values->count = (uint32_t)count;
    values->capacity = (uint32_t)count;
    NplVal rest = list;
    for (int64_t i = 0; i < count; i++) {
        values->data[i] = npl_car(rest);
        rest = npl_cdr(rest);
    }
    npl_gc_unroot(&list);
    return (NplVal)(uintptr_t)values | TAG_PTR;
}

NplVal npl_expect_one(NplVal value)
{
    if (npl_is_ptr(value) && npl_header(value)->tag == BOX_VALUES) {
        NplValues *values = (NplValues *)npl_header(value);
        npl_primitive_error("value context", "expected 1 value",
                            npl_fixnum((int64_t)values->count));
    }
    return value;
}

void npl_values_dispatch(NplVal consumer_value, NplVal continuation,
                         NplVal value)
{
    if (!npl_is_ptr(consumer_value) ||
        npl_header(consumer_value)->tag != BOX_CLOSURE)
        npl_primitive_error("call-with-values", "expected a procedure",
                            consumer_value);

    uint32_t count = 1;
    NplVal *values = &value;
    if (npl_is_ptr(value) && npl_header(value)->tag == BOX_VALUES) {
        NplValues *multiple = (NplValues *)npl_header(value);
        count = multiple->count;
        values = multiple->data;
    }

    size_t argc = (size_t)count + 1;
    NplVal *arguments = (NplVal *)malloc(argc * sizeof(NplVal));
    if (!arguments) npl_runtime_oom_internal();
    for (uint32_t i = 0; i < count; i++) arguments[i] = values[i];
    arguments[count] = continuation;
    npl_tail_request_closure(consumer_value, (int64_t)argc, arguments);
    free(arguments);
}

NplWindFrame *require_wind_frame(NplVal value, const char *who)
{
    if (!npl_is_ptr(value) || npl_header(value)->tag != BOX_WIND_FRAME) {
        fprintf(stderr, "%s: invalid wind frame\n", who);
        exit(1);
    }
    return (NplWindFrame *)npl_header(value);
}

NplVal npl_current_wind(void)
{
    return CTX->current_wind;
}

NplVal npl_current_handler(void)
{
    return CTX->current_handler;
}

NplParameter *require_parameter(NplVal value, const char *who)
{
    if (!npl_is_ptr(value) || npl_header(value)->tag != BOX_PARAMETER)
        npl_primitive_error(who, "not a parameter", value);
    return (NplParameter *)npl_header(value);
}

NplVal npl_make_parameter(NplVal converter, NplVal value)
{
    if (!npl_is_nil(converter) &&
        (!npl_is_ptr(converter) || npl_header(converter)->tag != BOX_CLOSURE))
        npl_primitive_error("make-parameter", "expected a procedure",
                            converter);
    NplParameter *parameter =
        (NplParameter *)npl_alloc_object(sizeof(NplParameter));
    parameter->header.tag = BOX_PARAMETER;
    parameter->header.flags = 0;
    parameter->converter = converter;
    parameter->value = value;
    return (NplVal)(uintptr_t)parameter | TAG_PTR;
}

NplVal npl_parameter_ref(NplVal parameter_value)
{
    (void)require_parameter(parameter_value, "parameter");
    for (NplVal binding_value = CTX->current_parameters;
         !npl_is_nil(binding_value);) {
        if (!npl_is_ptr(binding_value) ||
            npl_header(binding_value)->tag != BOX_PARAMETER_BINDING) {
            fprintf(stderr, "parameter: invalid binding chain\n");
            exit(1);
        }
        NplParameterBinding *binding =
            (NplParameterBinding *)npl_header(binding_value);
        if (binding->parameter == parameter_value) return binding->value;
        binding_value = binding->parent;
    }
    return require_parameter(parameter_value, "parameter")->value;
}

/* T1.3 parameter copy-on-write.  A binding created by this thread's
 * parameterize (owner == this context) is mutated in place, preserving the
 * continuation-reentry behavior the frames vector exists for.  A binding
 * owned by another thread (inherited chain or a shared continuation) is
 * shadowed with a fresh thread-owned copy instead of being written, and the
 * parameter object's shared default is never mutated. */
static NplVal new_parameter_binding(NplVal parameter, NplVal value)
{
    NplParameterBinding *binding = (NplParameterBinding *)
        npl_alloc_object(sizeof(NplParameterBinding));
    binding->header.tag = BOX_PARAMETER_BINDING;
    binding->header.flags = 0;
    binding->parent = CTX->current_parameters;
    binding->parameter = parameter;
    binding->value = value;
    /* Owner is the stable monotonic context id, not the context address: a
     * freed context's address can be reused by a new thread, which would let
     * it mutate bindings retained by a captured continuation. */
    binding->owner = npl_binding_owner();
    NplVal boxed = (NplVal)(uintptr_t)binding | TAG_PTR;
    CTX->current_parameters = boxed;
    return boxed;
}

NplVal npl_parameter_set(NplVal parameter_value, NplVal value)
{
    (void)require_parameter(parameter_value, "parameter");
    for (NplVal binding_value = CTX->current_parameters;
         !npl_is_nil(binding_value);) {
        if (!npl_is_ptr(binding_value) ||
            npl_header(binding_value)->tag != BOX_PARAMETER_BINDING)
            npl_primitive_error("parameter", "invalid binding chain",
                                binding_value);
        NplParameterBinding *binding =
            (NplParameterBinding *)npl_header(binding_value);
        if (binding->parameter == parameter_value) {
            if (binding->owner == npl_binding_owner()) {
                binding->value = value;
                npl_gc_write_barrier(binding, value);
                return npl_nil();
            }
            /* Shared node: copy on write instead of mutating it. */
            (void)new_parameter_binding(parameter_value, value);
            return npl_nil();
        }
        binding_value = binding->parent;
    }
    /* No active binding: shadow the shared default with a thread-owned one. */
    (void)new_parameter_binding(parameter_value, value);
    return npl_nil();
}

NplVal npl_parameter_converter(NplVal parameter_value)
{
    NplVal converter = require_parameter(
        parameter_value, "parameterize")->converter;
    return npl_is_nil(converter) ? npl_false() : converter;
}

/* npl_parameters_enter / npl_parameters_leave and the wind transfer live in
 * dynamic-context.c, which owns the SRFI 18 dynamic-environment machinery.
 * This file keeps the parameter object, COW set!, and primitive machinery. */

static NplVal parameter_converted(NplVal environment, int64_t argc,
                                  NplVal *argv)
{
    if (argc != 1 || !npl_is_ptr(environment) ||
        npl_header(environment)->tag != BOX_PARAMETER_CONTEXT) {
        fprintf(stderr, "parameter: invalid converter callback\n");
        exit(1);
    }
    NplParameterContext *context =
        (NplParameterContext *)npl_header(environment);
    if (context->update) (void)npl_parameter_set(context->parameter, argv[0]);
    NplVal result = context->update ? npl_nil() : argv[0];
    npl_tail_request_closure(context->continuation, 1, &result);
    return npl_nil();
}

static void parameter_convert(NplVal parameter_value, NplVal value,
                              NplVal continuation, uint32_t update)
{
    NplParameter *parameter = require_parameter(parameter_value, "parameter");
    if (npl_is_nil(parameter->converter)) {
        if (update) (void)npl_parameter_set(parameter_value, value);
        NplVal result = update ? npl_nil() : value;
        npl_tail_request_closure(continuation, 1, &result);
        return;
    }
    NplParameterContext *context =
        (NplParameterContext *)npl_alloc_object(sizeof(NplParameterContext));
    context->header.tag = BOX_PARAMETER_CONTEXT;
    context->header.flags = 0;
    context->parameter = parameter_value;
    context->continuation = continuation;
    context->update = update;
    context->reserved = 0;
    NplVal callback = npl_make_closure(
        parameter_converted,
        (NplVal)(uintptr_t)context | TAG_PTR, 1);
    NplVal arguments[2] = { value, callback };
    npl_tail_request_closure(parameter->converter, 2, arguments);
}

void npl_parameter_dispatch(NplVal parameter, int64_t argc, NplVal *argv)
{
    if (argc == 1) {
        NplVal result = npl_parameter_ref(parameter);
        npl_tail_request_closure(argv[0], 1, &result);
        return;
    }
    if (argc == 2) {
        parameter_convert(parameter, argv[0], argv[1], 1);
        return;
    }
    /* argc counts the trailing continuation. */
    npl_primitive_error("parameter", "expected zero or one argument",
                        npl_fixnum(argc - 1));
}

#define PROMISE_DONE 0u
#define PROMISE_DELAY 1u
#define PROMISE_CHAIN 2u
#define PROMISE_REDIRECT 3u

static NplVal make_promise_state(uint32_t state, NplVal value)
{
    NplPromise *promise = (NplPromise *)npl_alloc_object(sizeof(NplPromise));
    promise->header.tag = BOX_PROMISE;
    promise->header.flags = 0;
    promise->state = state;
    promise->reserved = 0;
    promise->value = value;
    return (NplVal)(uintptr_t)promise | TAG_PTR;
}

static NplVal make_promise(NplVal value)
{
    if (npl_is_ptr(value) && npl_header(value)->tag == BOX_PROMISE)
        return value;
    return make_promise_state(PROMISE_DONE, value);
}

static NplVal make_delayed_promise(NplVal thunk, uint32_t state,
                                   const char *who)
{
    if (!npl_is_ptr(thunk) || npl_header(thunk)->tag != BOX_CLOSURE)
        npl_primitive_error(who, "expected a procedure", thunk);
    return make_promise_state(state, thunk);
}

NplVal npl_promise_op(int64_t argc, NplVal *argv)
{
    if (argc != 2 || !npl_is_fixnum(argv[0])) {
        fprintf(stderr, "promise operation: expected two arguments\n");
        exit(1);
    }
    switch (npl_fixnum_value(argv[0])) {
    case 0:
        return make_promise(argv[1]);
    case 1:
        return (npl_is_ptr(argv[1]) &&
                npl_header(argv[1])->tag == BOX_PROMISE)
            ? npl_true() : npl_false();
    case 2:
        return make_delayed_promise(argv[1], PROMISE_DELAY, "delay");
    case 3:
        return make_delayed_promise(argv[1], PROMISE_CHAIN, "delay-force");
    default:
        fprintf(stderr, "promise operation: invalid operation\n");
        exit(1);
    }
}

static void promise_force_step(NplVal context_value, NplVal value);

static NplVal promise_force_resume(NplVal environment, int64_t argc,
                                   NplVal *argv)
{
    if (argc != 1 || !npl_is_ptr(environment) ||
        npl_header(environment)->tag != BOX_PROMISE_CONTEXT) {
        fprintf(stderr, "force: invalid promise callback\n");
        exit(1);
    }
    NplPromiseContext *context =
        (NplPromiseContext *)npl_header(environment);
    if (!npl_is_ptr(context->current) ||
        npl_header(context->current)->tag != BOX_PROMISE) {
        fprintf(stderr, "force: invalid promise state\n");
        exit(1);
    }
    NplPromise *promise = (NplPromise *)npl_header(context->current);
    if (promise->state == PROMISE_DELAY) {
        promise->state = PROMISE_DONE;
        promise->value = argv[0];
        npl_gc_write_barrier(promise, argv[0]);
        promise_force_step(environment, context->current);
    } else if (promise->state == PROMISE_CHAIN) {
        promise->state = PROMISE_REDIRECT;
        promise->value = argv[0];
        npl_gc_write_barrier(promise, argv[0]);
        promise_force_step(environment, argv[0]);
    } else {
        promise_force_step(environment, context->current);
    }
    return npl_nil();
}

static void promise_force_step(NplVal context_value, NplVal value)
{
    NplPromiseContext *context =
        (NplPromiseContext *)npl_header(context_value);
    while (npl_is_ptr(value) &&
           npl_header(value)->tag == BOX_PROMISE) {
        NplPromise *promise = (NplPromise *)npl_header(value);
        if (promise->state == PROMISE_DONE) {
            value = promise->value;
            break;
        }
        if (promise->state == PROMISE_REDIRECT) {
            value = promise->value;
            continue;
        }
        context->current = value;
        npl_gc_write_barrier(context, value);
        NplVal callback = npl_make_closure(
            promise_force_resume, context_value, 1);
        npl_tail_request_closure(promise->value, 1, &callback);
        return;
    }

    if (npl_is_ptr(context->root) &&
        npl_header(context->root)->tag == BOX_PROMISE) {
        NplPromise *root = (NplPromise *)npl_header(context->root);
        if (root->state != PROMISE_DONE) {
            root->state = PROMISE_DONE;
            root->value = value;
            npl_gc_write_barrier(root, value);
        } else {
            value = root->value;
        }
    }
    npl_tail_request_closure(context->continuation, 1, &value);
}

void npl_force(NplVal value, NplVal continuation)
{
    if (!npl_is_ptr(value) || npl_header(value)->tag != BOX_PROMISE) {
        npl_tail_request_closure(continuation, 1, &value);
        return;
    }
    NplPromiseContext *context =
        (NplPromiseContext *)npl_alloc_object(sizeof(NplPromiseContext));
    context->header.tag = BOX_PROMISE_CONTEXT;
    context->header.flags = 0;
    context->root = value;
    context->current = value;
    context->continuation = continuation;
    promise_force_step((NplVal)(uintptr_t)context | TAG_PTR, value);
}

NplVal npl_handler_push(NplVal procedure)
{
    if (!npl_is_ptr(procedure) || npl_header(procedure)->tag != BOX_CLOSURE)
        npl_primitive_error("with-exception-handler",
                            "handler is not a procedure", procedure);
    NplHandlerFrame *frame =
        (NplHandlerFrame *)npl_alloc_object(sizeof(NplHandlerFrame));
    frame->header.tag = BOX_HANDLER_FRAME;
    frame->header.flags = 0;
    frame->parent = CTX->current_handler;
    frame->procedure = procedure;
    CTX->current_handler = (NplVal)(uintptr_t)frame | TAG_PTR;
    return CTX->current_handler;
}

/* Mark a handler frame as the thread's initial uncaught handler. A captured
 * handler chain keeps its user frames but this bottom frame is resolved
 * against the invoking thread (SRFI 18). The flag lives in header.flags, so
 * no GC traversal changes are needed. */
NplVal npl_handler_push_initial(NplVal procedure)
{
    NplVal frame = npl_handler_push(procedure);
    npl_header(frame)->flags |= NPL_FLAG_INITIAL_HANDLER;
    return frame;
}

NplVal npl_handler_pop(NplVal value)
{
    if (!npl_is_ptr(value) || npl_header(value)->tag != BOX_HANDLER_FRAME ||
        CTX->current_handler != value) {
        fprintf(stderr, "with-exception-handler: non-current handler exit\n");
        exit(1);
    }
    NplHandlerFrame *frame = (NplHandlerFrame *)npl_header(value);
    CTX->current_handler = frame->parent;
    return npl_nil();
}

static NplVal raise_handler_returned(NplVal environment, int64_t argc,
                                     NplVal *argv);

void npl_raise(NplVal value, NplVal continuation, int64_t continuable)
{
    if (npl_is_nil(CTX->current_handler)) {
        /* A continuation captured on the primordial thread has an empty
         * handler chain. Invoked on a worker, its uncaught raise must reach
         * the INVOKING worker's initial handler (which terminates that
         * thread with an uncaught-exception), not exit the whole process.
         * The primordial fallback stays when no initial handler exists. */
        if (!npl_is_nil(CTX->initial_handler)) {
            CTX->current_handler = CTX->initial_handler;
        } else {
            npl_report_error_object(value);
            exit(1);
        }
    }
    if (!npl_is_ptr(CTX->current_handler) ||
        npl_header(CTX->current_handler)->tag != BOX_HANDLER_FRAME) {
        fprintf(stderr, "raise: invalid handler chain\n");
        exit(1);
    }

    NplVal frame_value = CTX->current_handler;
    NplHandlerFrame *frame = (NplHandlerFrame *)npl_header(frame_value);
    /* SRFI 18: the bottom frame of a captured handler chain is the source
     * thread's initial uncaught handler. Resolve it against the invoking
     * thread's own initial handler instead of carrying the source thread's
     * termination path. User frames are kept by identity, so a later
     * npl_handler_pop still validates. */
    if ((frame->header.flags & NPL_FLAG_INITIAL_HANDLER) &&
        frame_value != CTX->initial_handler &&
        !npl_is_nil(CTX->initial_handler)) {
        frame_value = CTX->initial_handler;
        frame = (NplHandlerFrame *)npl_header(frame_value);
    }
    NplRaiseContext *context =
        (NplRaiseContext *)npl_alloc_object(sizeof(NplRaiseContext));
    context->header.tag = BOX_RAISE_CONTEXT;
    context->header.flags = 0;
    context->frame = frame_value;
    context->continuation = continuation;
    context->value = value;
    context->continuable = continuable ? 1u : 0u;
    context->reserved = 0;
    NplVal context_value = (NplVal)(uintptr_t)context | TAG_PTR;

    CTX->current_handler = frame->parent;
    NplVal callback = npl_make_closure(
        raise_handler_returned, context_value, 1);
    NplVal arguments[2] = { value, callback };
    npl_tail_request_closure(frame->procedure, 2, arguments);
}

static NplVal raise_handler_returned(NplVal environment, int64_t argc,
                                     NplVal *argv)
{
    if (argc != 1 || !npl_is_ptr(environment) ||
        npl_header(environment)->tag != BOX_RAISE_CONTEXT) {
        fprintf(stderr, "raise: invalid handler callback\n");
        exit(1);
    }
    NplRaiseContext *context = (NplRaiseContext *)npl_header(environment);
    if (context->continuable) {
        CTX->current_handler = context->frame;
        npl_tail_request_closure(context->continuation, 1, argv);
    } else {
        npl_raise(context->value, npl_nil(), 0);
    }
    return npl_nil();
}

NplVal npl_wind_push(NplVal before, NplVal after)
{
    if (!npl_is_ptr(before) || npl_header(before)->tag != BOX_CLOSURE)
        npl_primitive_error("dynamic-wind", "expected a procedure", before);
    if (!npl_is_ptr(after) || npl_header(after)->tag != BOX_CLOSURE)
        npl_primitive_error("dynamic-wind", "expected a procedure", after);
    NplWindFrame *frame = (NplWindFrame *)npl_alloc_object(sizeof(NplWindFrame));
    frame->header.tag = BOX_WIND_FRAME;
    frame->header.flags = 0;
    frame->parent = CTX->current_wind;
    frame->before = before;
    frame->after = after;
    /* SRFI 18: record the environment in effect in this extent, so a wind
     * transfer can reinstall it for each before/after thunk. pending_extent_key
     * was published by npl_parameters_enter in the before thunk. */
    frame->handler = CTX->current_handler;
    frame->parameters = CTX->current_parameters;
    frame->extent_key = CTX->pending_extent_key;
    CTX->pending_extent_key = npl_nil();
    frame->id = CTX->next_wind_id++;
    CTX->current_wind = (NplVal)(uintptr_t)frame | TAG_PTR;
    return CTX->current_wind;
}

NplVal npl_wind_pop(NplVal value)
{
    NplWindFrame *frame = require_wind_frame(value, "dynamic-wind");
    if (CTX->current_wind != value) {
        fprintf(stderr, "dynamic-wind: non-current frame exit\n");
        exit(1);
    }
    CTX->current_wind = frame->parent;
    return npl_nil();
}

/* One step of exit (R7RS 6.14, lib/scheme/process-context.sld): leave the
 * innermost dynamic-wind and return its after thunk, which then runs
 * outside that extent; #f when none is in effect. */
NplVal npl_wind_exit_step(void)
{
    if (npl_is_nil(CTX->current_wind)) return npl_false();
    NplWindFrame *frame = require_wind_frame(CTX->current_wind, "exit");
    CTX->current_wind = frame->parent;
    return frame->after;
}


/* The SRFI 18 wind transfer, wind-transition scheduling and captured
 * dynamic-context machinery live in dynamic-context.c. control.c keeps
 * the dynamic-state primitives (wind/handler push-pop, raise, parameter
 * objects and copy-on-write parameter-set!). */

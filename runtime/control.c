/* Runtime control, dynamic state, and tail-call machinery. */

#include "npl.h"
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static NplVal g_current_wind = 2;
static NplVal g_current_handler = 2;
static NplVal g_current_parameters = 2;
static uint64_t g_next_wind_id = 1;

void npl_control_init(void)
{
    g_current_wind = npl_nil();
    g_current_handler = npl_nil();
    g_current_parameters = npl_nil();
    npl_gc_root(&g_current_wind);
    npl_gc_root(&g_current_handler);
    npl_gc_root(&g_current_parameters);
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
        fprintf(stderr, "value context: expected 1 value, got %u\n",
                values->count);
        exit(1);
    }
    return value;
}

void npl_values_dispatch(NplVal consumer_value, NplVal continuation,
                         NplVal value)
{
    if (!npl_is_ptr(consumer_value) ||
        npl_header(consumer_value)->tag != BOX_CLOSURE) {
        fprintf(stderr, "call-with-values: consumer is not a procedure\n");
        exit(1);
    }

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

static NplWindFrame *require_wind_frame(NplVal value, const char *who)
{
    if (!npl_is_ptr(value) || npl_header(value)->tag != BOX_WIND_FRAME) {
        fprintf(stderr, "%s: invalid wind frame\n", who);
        exit(1);
    }
    return (NplWindFrame *)npl_header(value);
}

NplVal npl_current_wind(void)
{
    return g_current_wind;
}

NplVal npl_current_handler(void)
{
    return g_current_handler;
}

static NplParameter *require_parameter(NplVal value, const char *who)
{
    if (!npl_is_ptr(value) || npl_header(value)->tag != BOX_PARAMETER) {
        fprintf(stderr, "%s: not a parameter\n", who);
        exit(1);
    }
    return (NplParameter *)npl_header(value);
}

NplVal npl_make_parameter(NplVal converter, NplVal value)
{
    if (!npl_is_nil(converter) &&
        (!npl_is_ptr(converter) || npl_header(converter)->tag != BOX_CLOSURE)) {
        fprintf(stderr, "make-parameter: converter is not a procedure\n");
        exit(1);
    }
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
    for (NplVal binding_value = g_current_parameters;
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

NplVal npl_parameter_set(NplVal parameter_value, NplVal value)
{
    NplParameter *parameter = require_parameter(parameter_value, "parameter");
    for (NplVal binding_value = g_current_parameters;
         !npl_is_nil(binding_value);) {
        NplParameterBinding *binding =
            (NplParameterBinding *)npl_header(binding_value);
        if (binding->parameter == parameter_value) {
            binding->value = value;
            return npl_nil();
        }
        binding_value = binding->parent;
    }
    parameter->value = value;
    return npl_nil();
}

NplVal npl_parameter_converter(NplVal parameter_value)
{
    NplVal converter = require_parameter(
        parameter_value, "parameterize")->converter;
    return npl_is_nil(converter) ? npl_false() : converter;
}

static NplVector *require_parameter_vector(NplVal value)
{
    if (!npl_is_ptr(value) || npl_header(value)->tag != BOX_VECTOR) {
        fprintf(stderr, "parameterize: invalid binding vectors\n");
        exit(1);
    }
    return (NplVector *)npl_header(value);
}

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
    for (uint32_t i = 0; i < parameters->length; i++) {
        (void)require_parameter(parameters->data[i], "parameterize");
        NplVal frame_value = frames->data[i];
        if (frame_value == npl_false()) {
            NplParameterBinding *created =
                (NplParameterBinding *)npl_alloc_object(sizeof(NplParameterBinding));
            created->header.tag = BOX_PARAMETER_BINDING;
            created->header.flags = 0;
            created->parent = g_current_parameters;
            created->parameter = parameters->data[i];
            created->value = values->data[i];
            frame_value = (NplVal)(uintptr_t)created | TAG_PTR;
            frames->data[i] = frame_value;
        } else if (!npl_is_ptr(frame_value) ||
                   npl_header(frame_value)->tag != BOX_PARAMETER_BINDING) {
            fprintf(stderr, "parameterize: invalid binding reentry\n");
            exit(1);
        }
        NplParameterBinding *binding =
            (NplParameterBinding *)npl_header(frame_value);
        if (binding->parameter != parameters->data[i] ||
            binding->parent != g_current_parameters) {
            fprintf(stderr, "parameterize: invalid binding reentry\n");
            exit(1);
        }
        g_current_parameters = frame_value;
    }
    return npl_nil();
}

NplVal npl_parameters_leave(NplVal frames_value)
{
    NplVector *frames = require_parameter_vector(frames_value);
    for (uint32_t i = frames->length; i > 0; i--) {
        NplVal frame_value = frames->data[i - 1];
        if (!npl_is_ptr(frame_value) ||
            npl_header(frame_value)->tag != BOX_PARAMETER_BINDING ||
            g_current_parameters != frame_value) {
            fprintf(stderr, "parameterize: non-current binding exit\n");
            exit(1);
        }
        NplParameterBinding *binding =
            (NplParameterBinding *)npl_header(frame_value);
        g_current_parameters = binding->parent;
    }
    return npl_nil();
}

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

static void npl_parameter_dispatch(NplVal parameter, int64_t argc,
                                   NplVal *argv)
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
    fprintf(stderr, "parameter: expected zero or one argument\n");
    exit(1);
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
    if (!npl_is_ptr(thunk) || npl_header(thunk)->tag != BOX_CLOSURE) {
        fprintf(stderr, "%s: not a procedure\n", who);
        exit(1);
    }
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
        promise_force_step(environment, context->current);
    } else if (promise->state == PROMISE_CHAIN) {
        promise->state = PROMISE_REDIRECT;
        promise->value = argv[0];
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
    if (!npl_is_ptr(procedure) || npl_header(procedure)->tag != BOX_CLOSURE) {
        fprintf(stderr, "with-exception-handler: handler is not a procedure\n");
        exit(1);
    }
    NplHandlerFrame *frame =
        (NplHandlerFrame *)npl_alloc_object(sizeof(NplHandlerFrame));
    frame->header.tag = BOX_HANDLER_FRAME;
    frame->header.flags = 0;
    frame->parent = g_current_handler;
    frame->procedure = procedure;
    g_current_handler = (NplVal)(uintptr_t)frame | TAG_PTR;
    return g_current_handler;
}

NplVal npl_handler_pop(NplVal value)
{
    if (!npl_is_ptr(value) || npl_header(value)->tag != BOX_HANDLER_FRAME ||
        g_current_handler != value) {
        fprintf(stderr, "with-exception-handler: non-current handler exit\n");
        exit(1);
    }
    NplHandlerFrame *frame = (NplHandlerFrame *)npl_header(value);
    g_current_handler = frame->parent;
    return npl_nil();
}

static NplVal raise_handler_returned(NplVal environment, int64_t argc,
                                     NplVal *argv);

void npl_raise(NplVal value, NplVal continuation, int64_t continuable)
{
    if (npl_is_nil(g_current_handler)) {
        npl_report_error_object(value);
        exit(1);
    }
    if (!npl_is_ptr(g_current_handler) ||
        npl_header(g_current_handler)->tag != BOX_HANDLER_FRAME) {
        fprintf(stderr, "raise: invalid handler chain\n");
        exit(1);
    }

    NplVal frame_value = g_current_handler;
    NplHandlerFrame *frame = (NplHandlerFrame *)npl_header(frame_value);
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

    g_current_handler = frame->parent;
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
        g_current_handler = context->frame;
        npl_tail_request_closure(context->continuation, 1, argv);
    } else {
        npl_raise(context->value, npl_nil(), 0);
    }
    return npl_nil();
}

NplVal npl_wind_push(NplVal before, NplVal after)
{
    if (!npl_is_ptr(before) || npl_header(before)->tag != BOX_CLOSURE ||
        !npl_is_ptr(after) || npl_header(after)->tag != BOX_CLOSURE) {
        fprintf(stderr, "dynamic-wind: before and after must be procedures\n");
        exit(1);
    }
    NplWindFrame *frame = (NplWindFrame *)npl_alloc_object(sizeof(NplWindFrame));
    frame->header.tag = BOX_WIND_FRAME;
    frame->header.flags = 0;
    frame->parent = g_current_wind;
    frame->before = before;
    frame->after = after;
    frame->id = g_next_wind_id++;
    g_current_wind = (NplVal)(uintptr_t)frame | TAG_PTR;
    return g_current_wind;
}

NplVal npl_wind_pop(NplVal value)
{
    NplWindFrame *frame = require_wind_frame(value, "dynamic-wind");
    if (g_current_wind != value) {
        fprintf(stderr, "dynamic-wind: non-current frame exit\n");
        exit(1);
    }
    g_current_wind = frame->parent;
    return npl_nil();
}

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

static void schedule_wind_transition(NplVal transition_value)
{
    NplWindTransition *transition =
        (NplWindTransition *)npl_header(transition_value);
    if (!npl_is_nil(transition->entering)) {
        g_current_wind = transition->entering;
        transition->entering = npl_nil();
    }

    NplVal thunk = npl_nil();
    if (transition->exit_index < transition->exit_count) {
        NplVal frame_value = transition->frames[transition->exit_index++];
        NplWindFrame *frame = require_wind_frame(frame_value, "continuation");
        g_current_wind = frame->parent;
        thunk = frame->after;
    } else if (transition->entry_index < transition->entry_count) {
        g_current_handler = transition->target_handler;
        uint32_t reverse = transition->entry_count -
                           transition->entry_index - 1;
        NplVal frame_value = transition->frames[transition->exit_count + reverse];
        NplWindFrame *frame = require_wind_frame(frame_value, "continuation");
        transition->entry_index++;
        transition->entering = frame_value;
        thunk = frame->before;
    } else {
        g_current_wind = transition->target;
        g_current_handler = transition->target_handler;
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

void npl_wind_transfer(NplVal target, NplVal target_handler,
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
    if (g_current_wind == target) {
        g_current_handler = target_handler;
        npl_tail_request_closure(continuation, 1, &value);
        return;
    }

    NplVal common = wind_common_ancestor(g_current_wind, target);
    uint32_t exit_count = 0;
    for (NplVal frame = g_current_wind; frame != common;
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

    size_t index = 0;
    for (NplVal frame = g_current_wind; frame != common;
         frame = wind_parent(frame))
        transition->frames[index++] = frame;
    for (NplVal frame = target; frame != common;
         frame = wind_parent(frame))
        transition->frames[index++] = frame;

    schedule_wind_transition((NplVal)(uintptr_t)transition | TAG_PTR);
}

/* ---- Proper tail-call trampoline ---- */

typedef struct NplTailContext {
    struct NplTailContext *previous;
    NplGenericFunction function;
    NplVal environment;
    int64_t argc;
    NplVal *arguments;
    size_t argument_capacity;
    int pending;
    /* Primitive-error unwind target (error.c): state restored on longjmp. */
    jmp_buf unwind;
    int root_count;
    uintptr_t stack_bottom;
    int escape_depth;
} NplTailContext;

static NplTailContext *g_tail_context = NULL;
static int g_escape_depth = 0;

/* Raise a primitive's error object from inside a C primitive: discard the
 * primitive's C frames up to the innermost trampoline, which then calls the
 * current handler with a continuation that re-raises (the raise is
 * non-continuable). Returns 0 when there is no handler or trampoline. */
int npl_raise_from_primitive(NplVal object)
{
    NplTailContext *context = g_tail_context;
    if (!context || npl_is_nil(npl_current_handler())) return 0;
    npl_gc_restore_root_count_internal(context->root_count);
    (void)npl_gc_swap_stack_bottom(context->stack_bottom);
    g_escape_depth = context->escape_depth;
    g_tail_context = context;
    npl_raise(object, npl_nil(), 0);
    longjmp(context->unwind, 1);
}

void npl_control_mark_roots(void)
{
    for (NplTailContext *context = g_tail_context; context;
         context = context->previous) {
        npl_gc_mark_candidate_internal(context->environment);
        for (int64_t i = 0; i < context->argc; i++)
            npl_gc_mark_candidate_internal(context->arguments[i]);
    }
}

void npl_tail_request(NplGenericFunction function, NplVal environment,
                      int64_t argc, NplVal *argv)
{
    if (!g_tail_context) {
        fprintf(stderr, "npl_tail_request: no active trampoline\n");
        exit(1);
    }
    if (argc < 0 || (uint64_t)argc > UINT32_MAX) {
        fprintf(stderr, "npl_tail_request: unsupported arity %lld\n",
                (long long)argc);
        exit(1);
    }

    if ((uint64_t)argc > g_tail_context->argument_capacity) {
        size_t capacity = g_tail_context->argument_capacity;
        if (capacity == 0) capacity = 8;
        while (capacity < (uint64_t)argc) {
            if (capacity > SIZE_MAX / 2) npl_runtime_oom_internal();
            capacity *= 2;
        }
        NplVal *arguments = (NplVal *)realloc(
            g_tail_context->arguments, capacity * sizeof(NplVal));
        if (!arguments) npl_runtime_oom_internal();
        g_tail_context->arguments = arguments;
        g_tail_context->argument_capacity = capacity;
    }
    for (int64_t i = 0; i < argc; i++)
        g_tail_context->arguments[i] = argv[i];
    g_tail_context->function = function;
    g_tail_context->environment = environment;
    g_tail_context->argc = argc;
    g_tail_context->pending = 1;
}

/* apply in CPS: argv = procedure, fixed arguments..., list, continuation.
 * Spread the list and tail-call the procedure with the continuation last. */
void npl_apply_tail(int64_t argc, NplVal *argv)
{
    if (argc < 3)
        npl_primitive_error("apply", "wrong number of arguments",
                            npl_fixnum(argc - 1));
    NplVal procedure = argv[0];
    NplVal list = argv[argc - 2];
    NplVal continuation = argv[argc - 1];
    int64_t fixed = argc - 3;
    int64_t spread = 0;
    for (NplVal rest = list; !npl_is_nil(rest); rest = npl_cdr(rest)) {
        if (!npl_is_ptr(rest) || npl_header(rest)->tag != BOX_PAIR)
            npl_primitive_error("apply", "expected a proper list", list);
        spread++;
    }
    int64_t count = fixed + spread + 1;
    NplVal *arguments = (NplVal *)malloc((size_t)count * sizeof(NplVal));
    if (!arguments) npl_runtime_oom_internal();
    for (int64_t i = 0; i < fixed; i++) arguments[i] = argv[i + 1];
    int64_t index = fixed;
    for (NplVal rest = list; !npl_is_nil(rest); rest = npl_cdr(rest))
        arguments[index++] = npl_car(rest);
    arguments[count - 1] = continuation;
    /* npl_tail_request copies the arguments, so they may be freed here. */
    npl_tail_request_closure(procedure, count, arguments);
    free(arguments);
}

/* argc counts the trailing continuation of the CPS closure convention. */
void npl_wrong_argument_count(int64_t argc)
{
    npl_primitive_error("procedure", "wrong number of arguments",
                        npl_fixnum(argc - 1));
}

static int closure_accepts_arity(const NplClosure *closure, int64_t argc)
{
    if (argc < 0) return 0;
    if (closure->arity >= 0) return argc == closure->arity;
    return argc >= -closure->arity - 1;
}

/* argc counts the trailing continuation; the irritant is the user-visible
 * argument count. */
static void report_closure_arity(const char *who, const NplClosure *closure,
                                 int64_t argc)
{
    (void)who;
    (void)closure;
    npl_primitive_error("procedure", "wrong number of arguments",
                        npl_fixnum(argc - 1));
}

void npl_tail_request_closure(NplVal value, int64_t argc, NplVal *argv)
{
    if (npl_is_ptr(value) && npl_header(value)->tag == BOX_PROMISE) {
        if (argc != 1) {
            fprintf(stderr, "force: expected zero arguments\n");
            exit(1);
        }
        npl_force(value, argv[0]);
        return;
    }
    if (npl_is_ptr(value) && npl_header(value)->tag == BOX_PARAMETER) {
        npl_parameter_dispatch(value, argc, argv);
        return;
    }
    if (!npl_is_ptr(value) || npl_header(value)->tag != BOX_CLOSURE)
        npl_primitive_error("application", "not a procedure", value);
    NplClosure *closure = (NplClosure *)npl_header(value);
    if (!closure_accepts_arity(closure, argc))
        report_closure_arity("npl_tail_request_closure", closure, argc);
    npl_tail_request(closure->function, closure->environment, argc, argv);
}

NplVal npl_trampoline(NplGenericFunction function, NplVal environment,
                      int64_t argc, NplVal *argv)
{
    uintptr_t stack_boundary = 0;
    uintptr_t saved_stack_bottom = npl_gc_swap_stack_bottom((uintptr_t)(void *)&stack_boundary);
    NplTailContext context;
    context.previous = g_tail_context;
    context.function = function;
    context.environment = environment;
    context.argc = argc;
    context.arguments = NULL;
    context.argument_capacity = 0;
    if (argc < 0 || (uint64_t)argc > UINT32_MAX) {
        fprintf(stderr, "npl_trampoline: unsupported arity %lld\n",
                (long long)argc);
        exit(1);
    }
    if (argc > 0) {
        context.arguments = (NplVal *)malloc((size_t)argc * sizeof(NplVal));
        if (!context.arguments) npl_runtime_oom_internal();
        context.argument_capacity = (size_t)argc;
        for (int64_t i = 0; i < argc; i++) context.arguments[i] = argv[i];
    }
    g_tail_context = &context;
    context.root_count = npl_gc_root_count_internal();
    context.stack_bottom = (uintptr_t)(void *)&stack_boundary;
    context.escape_depth = g_escape_depth;

    NplVal result = npl_nil();
    argv = context.arguments;
    if (setjmp(context.unwind) != 0) {
        /* A primitive error unwound to here: its handler call is pending. */
        function = context.function;
        environment = context.environment;
        argc = context.argc;
        argv = context.arguments;
    }
    for (;;) {
        context.pending = 0;
        result = function(environment, argc, argv);
        if (!context.pending) break;
        function = context.function;
        environment = context.environment;
        argc = context.argc;
        argv = context.arguments;
    }

    (void)npl_gc_swap_stack_bottom(saved_stack_bottom);
    free(context.arguments);
    g_tail_context = context.previous;
    return result;
}

NplVal npl_apply(NplVal value, int64_t argc, NplVal *argv)
{
    if (npl_is_ptr(value) && npl_header(value)->tag == BOX_PARAMETER) {
        npl_parameter_dispatch(value, argc, argv);
        return npl_nil();
    }
    if (!npl_is_ptr(value) || npl_header(value)->tag != BOX_CLOSURE)
        npl_primitive_error("application", "not a procedure", value);
    NplClosure *closure = (NplClosure *)npl_header(value);
    if (!closure_accepts_arity(closure, argc))
        report_closure_arity("npl_apply", closure, argc);
    return npl_trampoline(closure->function, closure->environment, argc, argv);
}

/* ---- Escape-only continuations ---- */

#define NPL_MAX_ESCAPE_DEPTH 64

typedef struct {
    jmp_buf jump;
    uint64_t id;
    NplVal value;
    NplTailContext *saved_tail_context;
} NplEscapeFrame;

static NplEscapeFrame g_escape_frames[NPL_MAX_ESCAPE_DEPTH];
static uint64_t g_next_escape_id = 1;

static NplVal escape_continuation(NplVal environment, int64_t argc,
                                  NplVal *argv)
{
    if (argc != 1 || !npl_is_fixnum(environment)) {
        fprintf(stderr, "continuation: expected one argument\n");
        exit(1);
    }
    uint64_t id = (uint64_t)npl_fixnum_value(environment);
    for (int i = g_escape_depth - 1; i >= 0; i--) {
        NplEscapeFrame *frame = &g_escape_frames[i];
        if (frame->id == id) {
            frame->value = argv[0];
            g_tail_context = frame->saved_tail_context;
            g_escape_depth = i + 1;
            longjmp(frame->jump, 1);
        }
    }
    fprintf(stderr, "continuation: invoked outside its dynamic extent\n");
    exit(1);
}

NplVal npl_call_cc(NplVal procedure)
{
    if (g_escape_depth >= NPL_MAX_ESCAPE_DEPTH) {
        fprintf(stderr, "call/cc: escape nesting limit exceeded\n");
        exit(1);
    }

    int index = g_escape_depth++;
    NplEscapeFrame *frame = &g_escape_frames[index];
    frame->id = g_next_escape_id++;
    frame->saved_tail_context = g_tail_context;

    NplVal result;
    if (setjmp(frame->jump) == 0) {
        NplVal continuation = npl_make_closure(
            escape_continuation, npl_fixnum((int64_t)frame->id), 1);
        NplVal argv[1] = { continuation };
        result = npl_apply(procedure, 1, argv);
    } else {
        result = frame->value;
    }

    g_escape_depth = index;
    return result;
}

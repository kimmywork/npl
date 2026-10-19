/* Proper tail calls (trampoline), CPS application, and escape-only
 * continuations. Split from control.c, which owns dynamic state. */

#include "npl.h"
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

/* ---- Synchronous calls of library procedures ----
 *
 * npl_call_sync applies a registered function in value position: it supplies
 * the continuation itself and runs a nested trampoline. Compiled code uses it
 * only for library procedures that neither capture continuations nor call
 * user code (the public numeric aliases, compiler backend/numeric.npl).
 *
 * A raise inside the callee must not run the caller's handler on the nested
 * trampoline: a handler that escapes (guard) would resume the caller's code
 * on the wrong C frame. A sentinel handler therefore takes the raised object
 * out of the nested trampoline, and the call re-raises it non-continuably in
 * the caller's trampoline, as a primitive error is (npl_raise_from_primitive).
 * The library raises its argument errors with raise, so the observable
 * handler call is unchanged. As with escape continuations, the nested
 * trampoline's argument buffer is not freed on that path. */

typedef struct NplSyncFrame {
    struct NplSyncFrame *previous;
    jmp_buf jump;
    uint64_t id;
    NplVal raised;
    NplTailContext *saved_tail_context;
    int saved_escape_depth;
    int saved_root_count;
    uintptr_t saved_stack_bottom;
} NplSyncFrame;

static NplSyncFrame *g_sync_frame = NULL;
static uint64_t g_next_sync_id = 1;

static NplVal sync_return(NplVal environment, int64_t argc, NplVal *argv)
{
    (void)environment;
    if (argc != 1) {
        fprintf(stderr, "npl_call_sync: expected one result\n");
        exit(1);
    }
    return argv[0];
}

/* Handler procedure (value, continuation): leave the nested trampoline. */
static NplVal sync_raise_forward(NplVal environment, int64_t argc,
                                 NplVal *argv)
{
    NplSyncFrame *frame = g_sync_frame;
    if (argc != 2 || !frame || !npl_is_fixnum(environment) ||
        (uint64_t)npl_fixnum_value(environment) != frame->id) {
        fprintf(stderr, "npl_call_sync: invalid raise forward\n");
        exit(1);
    }
    frame->raised = argv[0];
    longjmp(frame->jump, 1);
}

NplVal npl_call_sync(NplGenericFunction function, NplVal environment,
                     int64_t argc, NplVal *argv)
{
    if (argc < 0 || (uint64_t)argc >= UINT32_MAX) {
        fprintf(stderr, "npl_call_sync: unsupported arity %lld\n",
                (long long)argc);
        exit(1);
    }
    NplSyncFrame frame;
    frame.previous = g_sync_frame;
    frame.id = g_next_sync_id++;
    frame.raised = npl_nil();
    frame.saved_tail_context = g_tail_context;
    frame.saved_escape_depth = g_escape_depth;
    frame.saved_root_count = npl_gc_root_count_internal();
    frame.saved_stack_bottom = npl_gc_stack_bottom_internal();

    /* Allocate before the copy: the malloc'd buffer is not a GC root, but
     * these locals are on the scanned C stack. */
    NplVal continuation = npl_make_closure(sync_return, npl_nil(), 1);
    NplVal sentinel = npl_make_closure(
        sync_raise_forward, npl_fixnum((int64_t)frame.id), 2);
    NplVal handler = npl_handler_push(sentinel);
    NplVal *arguments =
        (NplVal *)malloc((size_t)(argc + 1) * sizeof(NplVal));
    if (!arguments) npl_runtime_oom_internal();
    for (int64_t i = 0; i < argc; i++) arguments[i] = argv[i];
    arguments[argc] = continuation;
    g_sync_frame = &frame;

    if (setjmp(frame.jump) == 0) {
        NplVal result = npl_trampoline(function, environment, argc + 1,
                                       arguments);
        g_sync_frame = frame.previous;
        free(arguments);
        npl_handler_pop(handler);
        return result;
    }

    /* npl_raise already made the sentinel's parent the current handler. */
    g_sync_frame = frame.previous;
    free(arguments);
    g_tail_context = frame.saved_tail_context;
    g_escape_depth = frame.saved_escape_depth;
    npl_gc_restore_root_count_internal(frame.saved_root_count);
    (void)npl_gc_swap_stack_bottom(frame.saved_stack_bottom);
    NplVal raised = frame.raised;
    if (!npl_raise_from_primitive(raised)) {
        npl_report_error_object(raised);
        exit(1);
    }
    exit(1);
}

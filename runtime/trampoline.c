/* Proper tail calls (trampoline), CPS application, and escape-only
 * continuations. Split from control.c, which owns dynamic state.
 *
 * T1: the tail/escape/sync contexts point at C stack frames, so they are
 * per-thread fields of NplThreadContext (thread.h).  Their frame types moved
 * there too; the collector walks every registered context's chain. */

#include "npl.h"
#include "thread.h"
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CTX npl_ctx()

/* ---- Proper tail-call trampoline ---- */

/* Raise a primitive's error object from inside a C primitive: discard the
 * primitive's C frames up to the innermost trampoline, which then calls the
 * current handler with a continuation that re-raises (the raise is
 * non-continuable). Returns 0 when there is no handler or trampoline. */
int npl_raise_from_primitive(NplVal object)
{
    NplTailContext *context = CTX->tail_context;
    if (!context || npl_is_nil(npl_current_handler())) return 0;
    npl_gc_restore_root_count_internal(context->root_count);
    (void)npl_gc_swap_stack_bottom(context->stack_bottom);
    CTX->escape_depth = context->escape_depth;
    CTX->tail_context = context;
    npl_raise(object, npl_nil(), 0);
    longjmp(context->unwind, 1);
}

static void mark_context_tails(NplThreadContext *owner, void *data)
{
    (void)data;
    if (!owner->attached) return;
    for (NplTailContext *context = owner->tail_context; context;
         context = context->previous)
        npl_gc_mark_candidate_internal(context->environment);
    for (NplTailBuffer *buffer = owner->tail_buffers; buffer;
         buffer = buffer->previous)
        for (int64_t i = 0; i < buffer->argc; i++)
            npl_gc_mark_candidate_internal(buffer->arguments[i]);
}

void npl_control_mark_roots(void)
{
    /* Every thread's active trampoline frames are roots; a collection on one
     * thread must keep another thread's in-flight arguments alive. */
    npl_thread_foreach(mark_context_tails, NULL);
}

void npl_tail_request(NplGenericFunction function, NplVal environment,
                      int64_t argc, NplVal *argv)
{
    if (!CTX->tail_context) {
        fprintf(stderr, "npl_tail_request: no active trampoline\n");
        exit(1);
    }
    if (argc < 0 || (uint64_t)argc > UINT32_MAX) {
        fprintf(stderr, "npl_tail_request: unsupported arity %lld\n",
                (long long)argc);
        exit(1);
    }

    if ((uint64_t)argc > CTX->tail_context->argument_capacity) {
        size_t capacity = CTX->tail_context->argument_capacity;
        if (capacity == 0) capacity = 8;
        while (capacity < (uint64_t)argc) {
            if (capacity > SIZE_MAX / 2) npl_runtime_oom_internal();
            capacity *= 2;
        }
        NplVal *arguments = (NplVal *)realloc(
            CTX->tail_context->arguments, capacity * sizeof(NplVal));
        if (!arguments) npl_runtime_oom_internal();
        CTX->tail_context->arguments = arguments;
        CTX->tail_context->argument_capacity = capacity;
        CTX->tail_buffers->arguments = arguments;
    }
    for (int64_t i = 0; i < argc; i++)
        CTX->tail_context->arguments[i] = argv[i];
    CTX->tail_context->function = function;
    CTX->tail_context->environment = environment;
    CTX->tail_context->argc = argc;
    CTX->tail_buffers->argc = argc;
    CTX->tail_context->pending = 1;
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
    /* The stack scan must reach every compiled frame, including the caller
     * of a nested trampoline (npl_call_sync), whose temporaries may be live
     * only in its C frame. So a trampoline only extends the scanned range
     * toward older frames (the stack grows down): it moves the bottom when
     * none is set or when it runs below the current one, as a host program
     * may first enter NPL from a frame older than npl_runtime_init's. */
    uintptr_t stack_boundary = 0;
    uintptr_t here = (uintptr_t)(void *)&stack_boundary;
    uintptr_t saved_stack_bottom = npl_gc_stack_bottom_internal();
    uintptr_t stack_bottom =
        saved_stack_bottom > here ? saved_stack_bottom : here;
    (void)npl_gc_swap_stack_bottom(stack_bottom);
    NplTailContext context;
    context.previous = CTX->tail_context;
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
    NplTailBuffer *buffer = (NplTailBuffer *)malloc(sizeof(*buffer));
    if (!buffer) npl_runtime_oom_internal();
    buffer->previous = CTX->tail_buffers;
    buffer->arguments = context.arguments;
    buffer->argc = argc;
    CTX->tail_buffers = buffer;
    CTX->tail_context = &context;
    context.root_count = npl_gc_root_count_internal();
    context.stack_bottom = stack_bottom;
    context.escape_depth = CTX->escape_depth;
    context.stack_limit = npl_stack_limit;

    NplVal result = npl_nil();
    argv = context.arguments;
    if (setjmp(context.unwind) != 0) {
        /* A primitive error unwound to here: its handler call is pending.
         * Any shared-object lock held by the discarded frames must be
         * released before the handler can run (shared-lock.c). */
        npl_shared_release_all();
        npl_stack_limit = context.stack_limit;
        function = context.function;
        environment = context.environment;
        argc = context.argc;
        argv = context.arguments;
    }
    for (;;) {
        /* Safepoint at every CPS dispatch step: a collection may be waiting,
         * and cancellation is observed at the same poll. */
        npl_safepoint_poll();
        context.pending = 0;
        result = function(environment, argc, argv);
        if (!context.pending) break;
        function = context.function;
        environment = context.environment;
        argc = context.argc;
        argv = context.arguments;
    }

    (void)npl_gc_swap_stack_bottom(saved_stack_bottom);
    NplTailBuffer *active_buffer = CTX->tail_buffers;
    if (!active_buffer || active_buffer->arguments != context.arguments) {
        fprintf(stderr, "npl_trampoline: tail buffer stack corrupted\n");
        exit(1);
    }
    CTX->tail_buffers = active_buffer->previous;
    free(active_buffer->arguments);
    free(active_buffer);
    CTX->tail_context = context.previous;
    return result;
}

/* A cancellation / uncaught-exception longjmp to thread_entry skips the
 * trampoline epilogues. Heap-owned records remain valid after that unwind. */
void npl_trampoline_release_abandoned(void)
{
    NplTailBuffer *buffer = CTX->tail_buffers;
    while (buffer) {
        NplTailBuffer *previous = buffer->previous;
        free(buffer->arguments);
        free(buffer);
        buffer = previous;
    }
    CTX->tail_buffers = NULL;
    CTX->tail_context = NULL;
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

/* (procedure argument ...) from C, returning its value: hash table
 * equality and hash procedures (hashtable.c). */
NplVal npl_apply_sync(NplVal value, int64_t argc, NplVal *argv)
{
    if (!npl_is_ptr(value) || npl_header(value)->tag != BOX_CLOSURE)
        npl_primitive_error("application", "not a procedure", value);
    NplClosure *closure = (NplClosure *)npl_header(value);
    if (!closure_accepts_arity(closure, argc + 1))
        report_closure_arity("npl_apply_sync", closure, argc + 1);
    return npl_call_sync(closure->function, closure->environment, argc, argv);
}

/* ---- Escape-only continuations ----
 * NplEscapeFrame and NPL_MAX_ESCAPE_DEPTH are in thread.h. */

static NplVal escape_continuation(NplVal environment, int64_t argc,
                                  NplVal *argv)
{
    if (argc != 1 || !npl_is_fixnum(environment)) {
        fprintf(stderr, "continuation: expected one argument\n");
        exit(1);
    }
    uint64_t id = (uint64_t)npl_fixnum_value(environment);
    for (int i = CTX->escape_depth - 1; i >= 0; i--) {
        NplEscapeFrame *frame = &CTX->escape_frames[i];
        if (frame->id == id) {
            frame->value = argv[0];
            CTX->tail_context = frame->saved_tail_context;
            CTX->escape_depth = i + 1;
            longjmp(frame->jump, 1);
        }
    }
    fprintf(stderr, "continuation: invoked outside its dynamic extent\n");
    exit(1);
}

/* Retained for the runtime ABI.  The self-hosted compiler lowers call/cc
 * through control conversion, so no emitted program calls this entry point;
 * keep it for C extensions that link the runtime directly. */
NplVal npl_call_cc(NplVal procedure)
{
    if (CTX->escape_depth >= NPL_MAX_ESCAPE_DEPTH) {
        fprintf(stderr, "call/cc: escape nesting limit exceeded\n");
        exit(1);
    }

    int index = CTX->escape_depth++;
    NplEscapeFrame *frame = &CTX->escape_frames[index];
    frame->id = CTX->next_escape_id++;
    frame->saved_tail_context = CTX->tail_context;
    frame->saved_stack_limit = npl_stack_limit;

    NplVal result;
    if (setjmp(frame->jump) == 0) {
        NplVal continuation = npl_make_closure(
            escape_continuation, npl_fixnum((int64_t)frame->id), 1);
        NplVal argv[1] = { continuation };
        result = npl_apply(procedure, 1, argv);
    } else {
        npl_shared_release_all();
        result = frame->value;
        npl_stack_limit = frame->saved_stack_limit;
    }

    CTX->escape_depth = index;
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

/* NplSyncFrame is in thread.h; CTX->sync_frame / CTX->next_sync_id are now fields
 * of the current thread's context (see CTX below). */

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
    NplSyncFrame *frame = CTX->sync_frame;
    if (argc != 2 || !frame || !npl_is_fixnum(environment) ||
        (uint64_t)npl_fixnum_value(environment) != frame->id) {
        fprintf(stderr, "npl_call_sync: invalid raise forward\n");
        exit(1);
    }
    frame->raised = argv[0];
    longjmp(frame->jump, 1);
}

/* A nonlocal synchronous recovery can unwind nested trampoline frames while
 * leaving their heap-owned argument-buffer records linked from the thread
 * context. Release only records newer than the checkpoint saved at entry. */
static void tail_buffers_release_to(NplTailBuffer *checkpoint)
{
    while (CTX->tail_buffers != checkpoint) {
        NplTailBuffer *buffer = CTX->tail_buffers;
        if (!buffer) {
            fprintf(stderr, "npl_call_sync: tail buffer checkpoint lost\n");
            exit(1);
        }
        CTX->tail_buffers = buffer->previous;
        free(buffer->arguments);
        free(buffer);
    }
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
    frame.previous = CTX->sync_frame;
    frame.id = CTX->next_sync_id++;
    frame.raised = npl_nil();
    frame.saved_tail_context = CTX->tail_context;
    frame.saved_tail_buffers = CTX->tail_buffers;
    frame.saved_escape_depth = CTX->escape_depth;
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
    CTX->sync_frame = &frame;

    if (setjmp(frame.jump) == 0) {
        NplVal result = npl_trampoline(function, environment, argc + 1,
                                       arguments);
        CTX->sync_frame = frame.previous;
        free(arguments);
        npl_handler_pop(handler);
        return result;
    }

    /* npl_raise already made the sentinel's parent the current handler. */
    tail_buffers_release_to(frame.saved_tail_buffers);
    npl_shared_release_all();
    CTX->sync_frame = frame.previous;
    free(arguments);
    CTX->tail_context = frame.saved_tail_context;
    CTX->escape_depth = frame.saved_escape_depth;
    npl_gc_restore_root_count_internal(frame.saved_root_count);
    (void)npl_gc_swap_stack_bottom(frame.saved_stack_bottom);
    NplVal raised = frame.raised;
    if (!npl_raise_from_primitive(raised)) {
        npl_report_error_object(raised);
        exit(1);
    }
    exit(1);
}

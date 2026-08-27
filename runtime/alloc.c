/*
 * alloc.c — NPL runtime allocation and boxed values.
 *
 * Managed objects live in 64KB mmap chunks. Each object has private allocator
 * metadata immediately before its public NplHeader; sweep recycles metadata
 * blocks through a free list while chunks remain mapped for the process life.
 */

#include "npl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <limits.h>
#include <unistd.h>
#include <sys/mman.h>

#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif

#define NPL_CHUNK_SIZE (64u * 1024u)
#define NPL_ALIGN      16u
#define NPL_META_MAGIC_LIVE UINT64_C(0x4e504c4d45544131)
#define NPL_META_MAGIC_FREE UINT64_C(0x4e504c4d45544130)
#define MAX_ROOTS 1024
#define NPL_MAX_CALL_ARGS 16

typedef struct NplChunk NplChunk;
typedef struct NplAllocMeta NplAllocMeta;

struct NplChunk {
    NplChunk *next;
    size_t map_size;
    size_t used;
    unsigned char data[];
};

struct NplAllocMeta {
    uint64_t magic;
    NplAllocMeta *all_next;
    NplAllocMeta *free_next;
    NplAllocMeta *mark_next;
    size_t size;
    size_t capacity;
    uint32_t marked;
    uint32_t reserved;
};

#define NPL_META_SIZE ((sizeof(NplAllocMeta) + NPL_ALIGN - 1) & ~(NPL_ALIGN - 1))

static NplChunk *g_chunks = NULL;
static NplAllocMeta *g_alloc_head = NULL;
static NplAllocMeta *g_free_head = NULL;
static NplAllocMeta *g_mark_stack = NULL;
static size_t g_allocated = 0;
static size_t g_threshold = 1024 * 1024;
static size_t g_live_objects = 0;
static size_t g_collections = 0;
static NplVal *g_roots[MAX_ROOTS];
static int g_root_count = 0;
static uintptr_t g_stack_bottom = 0;
static int g_runtime_initialized = 0;
static int g_gc_running = 0;

static int g_argc_stored = 0;
static char **g_argv_stored = NULL;
static NplVal g_args_list = 2;

static size_t align_up(size_t n, size_t alignment)
{
    return (n + alignment - 1) & ~(alignment - 1);
}

static void runtime_oom(void)
{
    static const char msg[] = "npl runtime: out of memory\n";
    (void)write(STDERR_FILENO, msg, sizeof(msg) - 1);
    _Exit(1);
}

static NplChunk *map_chunk(size_t need)
{
    size_t header = align_up(sizeof(NplChunk), NPL_ALIGN);
    size_t map_size = NPL_CHUNK_SIZE;
    while (map_size < header + need) map_size += NPL_CHUNK_SIZE;

    void *mem = mmap(NULL, map_size, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED) runtime_oom();

    NplChunk *chunk = (NplChunk *)mem;
    chunk->next = g_chunks;
    chunk->map_size = map_size;
    chunk->used = header;
    g_chunks = chunk;
    return chunk;
}

static void *meta_object(NplAllocMeta *meta)
{
    return (unsigned char *)meta + NPL_META_SIZE;
}

static NplAllocMeta *find_managed_meta(const void *object)
{
    uintptr_t address = (uintptr_t)object;
    if ((address & (NPL_ALIGN - 1)) != 0) return NULL;
    for (NplChunk *chunk = g_chunks; chunk; chunk = chunk->next) {
        uintptr_t begin = (uintptr_t)chunk;
        uintptr_t end = begin + chunk->used;
        if (address < begin + NPL_META_SIZE || address >= end) continue;
        NplAllocMeta *meta = (NplAllocMeta *)(address - NPL_META_SIZE);
        if (meta->magic == NPL_META_MAGIC_LIVE && meta_object(meta) == object)
            return meta;
        return NULL;
    }
    return NULL;
}

static NplAllocMeta *take_free_block(size_t size)
{
    NplAllocMeta **link = &g_free_head;
    while (*link) {
        if ((*link)->capacity >= size) {
            NplAllocMeta *meta = *link;
            *link = meta->free_next;
            return meta;
        }
        link = &(*link)->free_next;
    }
    return NULL;
}

static void *npl_alloc(size_t size)
{
    size = align_up(size, NPL_ALIGN);
    if (g_runtime_initialized && !g_gc_running &&
        g_allocated + size >= g_threshold) {
        npl_gc_collect();
    }

    NplAllocMeta *meta = take_free_block(size);
    if (!meta) {
        size_t total = NPL_META_SIZE + size;
        NplChunk *chunk = g_chunks;
        if (!chunk || chunk->used + total > chunk->map_size)
            chunk = map_chunk(total);
        meta = (NplAllocMeta *)((unsigned char *)chunk + chunk->used);
        chunk->used += total;
        memset(meta, 0, NPL_META_SIZE);
        meta->capacity = size;
    }

    meta->magic = NPL_META_MAGIC_LIVE;
    meta->size = size;
    meta->marked = 0;
    meta->mark_next = NULL;
    meta->free_next = NULL;
    meta->all_next = g_alloc_head;
    g_alloc_head = meta;
    memset(meta_object(meta), 0, meta->capacity);
    g_allocated += size;
    g_live_objects++;
    return meta_object(meta);
}

void npl_runtime_init(void)
{
    uintptr_t marker = 0;
    if (g_runtime_initialized) return;
    g_runtime_initialized = 1;
    g_stack_bottom = (uintptr_t)(void *)&marker + sizeof(marker);
    g_threshold = 1024 * 1024;
    g_root_count = 0;
    g_args_list = npl_nil();
}

/* ---- Value constructors ---- */

NplVal npl_cons(NplVal car, NplVal cdr) {
    NplPair *p = (NplPair *)npl_alloc(sizeof(NplPair));
    p->header.tag = BOX_PAIR;
    p->header.flags = 0;
    p->car = car;
    p->cdr = cdr;
    return (NplVal)(uintptr_t)p | TAG_PTR;
}

NplVal npl_string(const char *s, uint32_t len) {
    size_t size = sizeof(NplString) + len + 1;
    NplString *str = (NplString *)npl_alloc(size);
    str->header.tag = BOX_STRING;
    str->header.flags = 0;
    str->length = len;
    str->capacity = len;
    if (s) {
        memcpy(str->data, s, len);
    }
    str->data[len] = '\0';
    return (NplVal)(uintptr_t)str | TAG_PTR;
}

NplVal npl_float(double v) {
    NplFloat *f = (NplFloat *)npl_alloc(sizeof(NplFloat));
    f->header.tag = BOX_FLOAT;
    f->header.flags = 0;
    f->value = v;
    return (NplVal)(uintptr_t)f | TAG_PTR;
}

NplVal npl_make_environment(int64_t argc, NplVal *argv)
{
    if (argc < 0 || argc > NPL_MAX_CALL_ARGS) {
        fprintf(stderr, "npl_make_environment: unsupported size %lld\n",
                (long long)argc);
        exit(1);
    }
    NplVector *environment = (NplVector *)npl_alloc(
        sizeof(NplVector) + (size_t)argc * sizeof(NplVal));
    environment->header.tag = BOX_VECTOR;
    environment->header.flags = 0;
    environment->length = (uint32_t)argc;
    environment->capacity = (uint32_t)argc;
    for (int64_t i = 0; i < argc; i++) environment->data[i] = argv[i];
    return (NplVal)(uintptr_t)environment | TAG_PTR;
}

NplVal npl_environment_ref(NplVal value, int64_t index)
{
    if (!npl_is_ptr(value) || npl_header(value)->tag != BOX_VECTOR) {
        fprintf(stderr, "npl_environment_ref: not an environment\n");
        exit(1);
    }
    NplVector *environment = (NplVector *)npl_header(value);
    if (index < 0 || (uint64_t)index >= environment->length) {
        fprintf(stderr, "npl_environment_ref: index out of range\n");
        exit(1);
    }
    return environment->data[index];
}

NplVal npl_make_closure(NplGenericFunction function, NplVal environment,
                        int64_t arity)
{
    if (arity < 0 || arity > NPL_MAX_CALL_ARGS) {
        fprintf(stderr, "npl_make_closure: unsupported arity %lld\n",
                (long long)arity);
        exit(1);
    }
    NplClosure *closure = (NplClosure *)npl_alloc(sizeof(NplClosure));
    closure->header.tag = BOX_CLOSURE;
    closure->header.flags = 0;
    closure->function = function;
    closure->environment = environment;
    closure->arity = arity;
    return (NplVal)(uintptr_t)closure | TAG_PTR;
}

int32_t npl_guard_integer(NplVal value)
{
    if (!npl_is_fixnum(value)) {
        fprintf(stderr, "type guard: expected Integer\n");
        exit(1);
    }
    int64_t integer = npl_fixnum_value(value);
    if (integer < INT32_MIN || integer > INT32_MAX) {
        fprintf(stderr, "type guard: Integer outside unboxed range\n");
        exit(1);
    }
    return (int32_t)integer;
}

int32_t npl_guard_bool(NplVal value)
{
    if (value == npl_true()) return 1;
    if (value == npl_false()) return 0;
    fprintf(stderr, "type guard: expected Bool\n");
    exit(1);
}

NplVal npl_box_integer(int32_t value) { return npl_fixnum(value); }
NplVal npl_box_bool(int32_t value) { return value ? npl_true() : npl_false(); }

unsigned long npl_runtime_anchor(void) {
    return (unsigned long)(uintptr_t)&g_alloc_head;
}

void npl_tracev(unsigned long v) {
    fprintf(stderr, "[V=%lu]", v);
}

void npl_trace2(unsigned long tag, unsigned long v) {
    fprintf(stderr, "[T%lu=%lu]", tag, v);
}

#include <execinfo.h>

void npl_dump_addrs(void) {
    fprintf(stderr, "anchor_g_alloc_head=%p\n", (void*)&g_alloc_head);
    void *bt[32];
    int n = backtrace(bt, 32);
    fprintf(stderr, "BACKTRACE:\n");
    for (int i = 0; i < n; i++)
        fprintf(stderr, "  #%d %p\n", i, bt[i]);
}

/* ---- Value accessors ---- */

NplVal npl_car(NplVal v) {
    if (!npl_is_ptr(v)) {
        fprintf(stderr, "npl_car: not a pair (v=%lu)\n", (unsigned long)v);
        exit(1);
    }
    NplPair *p = npl_pair_ptr(v);
    if (p->header.tag != BOX_PAIR) {
        fprintf(stderr, "npl_car: not a pair (v=%lu)\n", (unsigned long)v);
        exit(1);
    }
    return p->car;
}

NplVal npl_cdr(NplVal v) {
#ifdef NPL_TRACE_APPEND
    extern int g_trace_depth;
#endif
    if (!npl_is_ptr(v)) {
#ifdef NPL_TRACE_APPEND
        fprintf(stderr, "[cdr-on-nonptr v=%lu]\n", (unsigned long)v);
#endif
        fprintf(stderr, "npl_cdr: NONPTR v=%lu\n", (unsigned long)v);
        {
            extern void npl_dump_addrs(void);
            npl_dump_addrs();
        }
        exit(1);
    }
    NplPair *p = npl_pair_ptr(v);
    if (p->header.tag != BOX_PAIR) {
        void *lr1b = __builtin_return_address(0);
        fprintf(stderr, "npl_cdr: badtag v=%lu from=%p\n",
                (unsigned long)v, lr1b);
        exit(1);
    }
    return p->cdr;
}

void npl_set_car(NplVal pair, NplVal val) {
    if (!npl_is_ptr(pair)) return;
    NplPair *p = npl_pair_ptr(pair);
    if (p->header.tag != BOX_PAIR) return;
    p->car = val;
}

void npl_set_cdr(NplVal pair, NplVal val) {
    if (!npl_is_ptr(pair)) return;
    NplPair *p = npl_pair_ptr(pair);
    if (p->header.tag != BOX_PAIR) return;
    p->cdr = val;
}

/* ---- GC root management and collector internals ---- */

void npl_gc_root(NplVal *root)
{
    if (g_root_count >= MAX_ROOTS) {
        fprintf(stderr, "npl_gc_root: root table overflow\n");
        exit(1);
    }
    g_roots[g_root_count++] = root;
}

void npl_gc_unroot(NplVal *root)
{
    for (int i = 0; i < g_root_count; i++) {
        if (g_roots[i] == root) {
            g_roots[i] = g_roots[--g_root_count];
            return;
        }
    }
}

void npl_gc_begin_internal(void)
{
    g_gc_running = 1;
    g_mark_stack = NULL;
}

void npl_gc_mark_candidate_internal(NplVal value)
{
    if (!npl_is_ptr(value)) return;
    NplAllocMeta *meta = find_managed_meta(npl_header(value));
    if (!meta || meta->marked) return;
    meta->marked = 1;
    meta->mark_next = g_mark_stack;
    g_mark_stack = meta;
}

void npl_gc_mark_roots_internal(void)
{
    for (int i = 0; i < g_root_count; i++) {
        if (g_roots[i]) npl_gc_mark_candidate_internal(*g_roots[i]);
    }
    npl_gc_mark_candidate_internal(g_args_list);
}

void npl_gc_drain_internal(void)
{
    while (g_mark_stack) {
        NplAllocMeta *meta = g_mark_stack;
        g_mark_stack = meta->mark_next;
        NplHeader *header = (NplHeader *)meta_object(meta);
        switch (header->tag) {
        case BOX_PAIR: {
            NplPair *pair = (NplPair *)header;
            npl_gc_mark_candidate_internal(pair->car);
            npl_gc_mark_candidate_internal(pair->cdr);
            break;
        }
        case BOX_VECTOR: {
            NplVector *vector = (NplVector *)header;
            for (uint32_t i = 0; i < vector->length; i++)
                npl_gc_mark_candidate_internal(vector->data[i]);
            break;
        }
        case BOX_CLOSURE: {
            NplClosure *closure = (NplClosure *)header;
            npl_gc_mark_candidate_internal(closure->environment);
            break;
        }
        default:
            break;
        }
    }
}

void npl_gc_sweep_internal(void)
{
    NplAllocMeta **link = &g_alloc_head;
    while (*link) {
        NplAllocMeta *meta = *link;
        if (meta->marked) {
            meta->marked = 0;
            meta->mark_next = NULL;
            link = &meta->all_next;
            continue;
        }

        *link = meta->all_next;
        g_allocated -= meta->size;
        g_live_objects--;
        meta->magic = NPL_META_MAGIC_FREE;
        meta->all_next = NULL;
        meta->mark_next = NULL;
        meta->free_next = g_free_head;
        g_free_head = meta;
    }

    g_threshold = g_allocated * 2;
    if (g_threshold < 1024 * 1024) g_threshold = 1024 * 1024;
    g_collections++;
    g_gc_running = 0;
}

uintptr_t npl_gc_stack_bottom_internal(void)
{
    return g_stack_bottom;
}

size_t npl_gc_allocated(void) { return g_allocated; }
size_t npl_gc_threshold(void) { return g_threshold; }
size_t npl_gc_live_objects(void) { return g_live_objects; }
size_t npl_gc_collections(void) { return g_collections; }

/* ---- Proper tail-call trampoline ---- */

typedef struct NplTailContext {
    struct NplTailContext *previous;
    NplGenericFunction function;
    NplVal environment;
    int64_t argc;
    NplVal argv[NPL_MAX_CALL_ARGS];
    int pending;
} NplTailContext;

static NplTailContext *g_tail_context = NULL;

void npl_tail_request(NplGenericFunction function, NplVal environment,
                      int64_t argc, NplVal *argv)
{
    if (!g_tail_context) {
        fprintf(stderr, "npl_tail_request: no active trampoline\n");
        exit(1);
    }
    if (argc < 0 || argc > NPL_MAX_CALL_ARGS) {
        fprintf(stderr, "npl_tail_request: unsupported arity %lld\n",
                (long long)argc);
        exit(1);
    }

    g_tail_context->function = function;
    g_tail_context->environment = environment;
    g_tail_context->argc = argc;
    for (int64_t i = 0; i < argc; i++) g_tail_context->argv[i] = argv[i];
    g_tail_context->pending = 1;
}

void npl_tail_request_closure(NplVal value, int64_t argc, NplVal *argv)
{
    if (!npl_is_ptr(value) || npl_header(value)->tag != BOX_CLOSURE) {
        fprintf(stderr, "npl_tail_request_closure: not a closure\n");
        exit(1);
    }
    NplClosure *closure = (NplClosure *)npl_header(value);
    if (argc != closure->arity) {
        fprintf(stderr,
                "npl_tail_request_closure: expected %lld arguments, got %lld\n",
                (long long)closure->arity, (long long)argc);
        exit(1);
    }
    npl_tail_request(closure->function, closure->environment, argc, argv);
}

NplVal npl_trampoline(NplGenericFunction function, NplVal environment,
                      int64_t argc, NplVal *argv)
{
    NplTailContext context;
    context.previous = g_tail_context;
    g_tail_context = &context;

    NplVal result = npl_nil();
    for (;;) {
        context.pending = 0;
        result = function(environment, argc, argv);
        if (!context.pending) break;
        function = context.function;
        environment = context.environment;
        argc = context.argc;
        argv = context.argv;
    }

    g_tail_context = context.previous;
    return result;
}

NplVal npl_apply(NplVal value, int64_t argc, NplVal *argv)
{
    if (!npl_is_ptr(value) || npl_header(value)->tag != BOX_CLOSURE) {
        fprintf(stderr, "npl_apply: not a closure\n");
        exit(1);
    }
    NplClosure *closure = (NplClosure *)npl_header(value);
    if (argc != closure->arity) {
        fprintf(stderr, "npl_apply: expected %lld arguments, got %lld\n",
                (long long)closure->arity, (long long)argc);
        exit(1);
    }
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
static int g_escape_depth = 0;
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

/* ---- Display (for debugging) ---- */

void npl_display(NplVal v) {
    if (npl_is_fixnum(v)) {
        printf("%lld", (long long)npl_fixnum_value(v));
    } else if (npl_is_nil(v)) {
        printf("()");
    } else if (v == npl_true()) {
        printf("#t");
    } else if (v == npl_false()) {
        printf("#f");
    } else if (npl_is_ptr(v)) {
        NplHeader *h = npl_header(v);
        switch (h->tag) {
        case BOX_PAIR: {
            NplPair *p = (NplPair *)h;
            printf("(");
            npl_display(p->car);
            NplVal rest = p->cdr;
            while (!npl_is_nil(rest) && npl_is_ptr(rest) && npl_box_tag(rest) == BOX_PAIR) {
                printf(" ");
                npl_display(npl_car(rest));
                rest = npl_cdr(rest);
            }
            if (!npl_is_nil(rest)) {
                printf(" . ");
                npl_display(rest);
            }
            printf(")");
            break;
        }
        case BOX_STRING: {
            NplString *s = (NplString *)h;
            /* R7RS display prints strings without quotes */
            printf("%.*s", (int)s->length, s->data);
            break;
        }
        case BOX_SYMBOL: {
            NplSymbolLike *s = (NplSymbolLike *)h;
            printf("%.*s", (int)s->length, s->data);
            break;
        }
        case BOX_FLOAT: {
            NplFloat *f = (NplFloat *)h;
            printf("%g", f->value);
            break;
        }
        default:
            printf("<unknown>");
        }
    } else {
        printf("<unknown>");
    }
}

void npl_newline(void) {
    printf("\n");
}

/* ---- String & symbol builtins (increment-2) ---- */

static NplVal box_bytes(const char *bytes, uint32_t len, uint32_t box_tag) {
    size_t size = sizeof(NplString) + (size_t)len + 1;
    NplString *str = (NplString *)npl_alloc(size);
    str->header.tag = box_tag;
    str->header.flags = 0;
    str->length = len;
    str->capacity = len;
    if (len && bytes) memcpy(str->data, bytes, len);
    str->data[len] = '\0';
    return (NplVal)(uintptr_t)str | TAG_PTR;
}

static const NplString *require_bytes(NplVal v, const char *who) {
    if (!npl_is_ptr(v)) {
        fprintf(stderr, "%s: not a string/symbol\n", who);
        exit(1);
    }
    uint32_t tag = npl_header(v)->tag;
    if (tag != BOX_STRING && tag != BOX_SYMBOL) {
        fprintf(stderr, "%s: not a string/symbol\n", who);
        exit(1);
    }
    return (const NplString *)npl_header(v);
}

NplVal npl_string_append(NplVal a, NplVal b) {
#ifdef NPL_TRACE_APPEND
    fprintf(stderr, "[append a=%lu b=%lu]", (unsigned long)a, (unsigned long)b);
#endif
    const NplString *sa = require_bytes(a, "string-append");
    const NplString *sb = require_bytes(b, "string-append");
#ifdef NPL_TRACE_APPEND
    fprintf(stderr, "[alen=%u blen=%u]", sa->length, sb->length);
#endif
    uint32_t len = sa->length + sb->length;
    char *tmp = (char *)malloc((size_t)len + 1);
    if (!tmp) { fprintf(stderr, "string-append: oom\n"); exit(1); }
    memcpy(tmp, sa->data, sa->length);
    memcpy(tmp + sa->length, sb->data, sb->length);
    tmp[len] = '\0';
    NplVal out = npl_string(tmp, len);
    free(tmp);
    return out;
}

int64_t npl_string_length(NplVal s) {
    return (int64_t)require_bytes(s, "string-length")->length;
}

/* Returns the byte at index i as a fixnum (v1: single-byte strings). */
NplVal npl_string_ref(NplVal s, NplVal i) {
    const NplString *st = require_bytes(s, "string-ref");
    int64_t idx = (int64_t)(i >> 2);   /* expect tagged fixnum */
    if (idx < 0 || idx >= (int64_t)st->length) {
        fprintf(stderr, "string-ref: index out of range\n");
        exit(1);
    }
    return npl_fixnum((unsigned char)st->data[idx]);
}

int npl_string_equal(NplVal a, NplVal b) {
    const NplString *sa = require_bytes(a, "string=?");
    const NplString *sb = require_bytes(b, "string=?");
    if (sa->length != sb->length) return 0;
    return memcmp(sa->data, sb->data, sa->length) == 0;
}

NplVal npl_number_to_string(NplVal v) {
#ifdef NPL_TRACE_APPEND
    fprintf(stderr, "[num2str v=%lu]", (unsigned long)v);
#endif
    if (!npl_is_fixnum(v)) {
        fprintf(stderr, "number->string: not a fixnum (v=%lu)\n", (unsigned long)v);
        exit(1);
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "%lld", (long long)npl_fixnum_value(v));
    return npl_string(buf, (uint32_t)strlen(buf));
}

/* Copy bytes into a box with a different header tag (symbol<->string). */
static NplVal retag_box(NplVal v, uint32_t new_tag) {
    const NplString *src = (const NplString *)npl_header(v);
    return box_bytes(src->data, src->length, new_tag);
}

NplVal npl_symbol_to_string(NplVal s) {
    require_bytes(s, "symbol->string");
    return retag_box(s, BOX_STRING);
}

/* ---- bootstrap I/O & control (increment-4) ---- */

void npl_store_args(int argc, char **argv) {
#ifdef NPL_UNBUFFERED_STDOUT
    setvbuf(stdout, (char *)NULL, _IONBF, 0);
#endif
    g_argc_stored = argc;
    g_argv_stored = argv;
}

/* Build (once) the list of argument strings; argv[0] (program path) omitted. */
NplVal npl_program_arguments(void) {
    if (!npl_is_nil(g_args_list)) {
        return g_args_list;
    }
    NplVal acc = 2; /* nil */
    for (int i = g_argc_stored - 1; i >= 1; i--) {
        NplVal s = npl_string(g_argv_stored[i],
                              (uint32_t)strlen(g_argv_stored[i]));
        acc = npl_cons(s, acc);
    }
    g_args_list = acc;
    return acc;
}

NplVal npl_read_file(NplVal path) {
    const NplString *p = require_bytes(path, "read-file");
    char *buf = (char *)malloc((size_t)p->length + 1);
    if (!buf) { fprintf(stderr, "read-file: oom\n"); exit(1); }
    memcpy(buf, p->data, p->length);
    buf[p->length] = '\0';

#ifdef NPL_TRACE_APPEND
    fprintf(stderr, "[readfile '%s']", buf);
#endif
    FILE *fp = fopen(buf, "rb");
    free(buf);
    if (!fp) {
        fprintf(stderr, "read-file: cannot open '%.*s'\n",
                (int)p->length, p->data);
        exit(1);
    }
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (size < 0) { fclose(fp); exit(1); }
    char *data = (char *)malloc((size_t)size + 1);
    size_t got = fread(data, 1, (size_t)size, fp);
    fclose(fp);
    data[got] = '\0';
#ifdef NPL_TRACE_APPEND
    fprintf(stderr, "[size=%zu]", got);
#endif
    NplVal out = npl_string(data, (uint32_t)got);
    free(data);
    return out;
}

NplVal npl_substring(NplVal s, NplVal i, NplVal j) {
    const NplString *st = require_bytes(s, "substring");
    int64_t a = (int64_t)(i >> 2);
    int64_t b = (int64_t)(j >> 2);
    if (a < 0 || b < a || b > (int64_t)st->length) {
        fprintf(stderr, "substring: bad range\n");
        exit(1);
    }
    return box_bytes(st->data + a, (uint32_t)(b - a), st->header.tag);
}

NplVal npl_int_to_string_char(NplVal code) {
    int64_t v = (int64_t)(code >> 2);
    if (v < 1 || v > 255) {
        fprintf(stderr, "int->string: byte out of range\n");
        exit(1);
    }
    char one = (char)v;
    return box_bytes(&one, 1, BOX_STRING);
}

/* ---- bootstrap lexer hooks (increment-4) ----
 * These keep the self-hosted lexer out of the tagged/raw numeric-domain
 * swamp: character classification and decimal scanning happen here in C. */

static const NplString *lex_str(NplVal s) {
    return require_bytes(s, "lex");
}

/* Returns a PROPER tagged boolean (#f=10/#t=6) so the self-hosted caller
 * can feed it straight into `if`; a raw int return would bypass retagging. */
static inline NplVal npl_bool(int x) { return x ? (NplVal)6 : (NplVal)10; }

NplVal npl_lex_class_w(NplVal cT, NplVal wantT) {
    long c = (long)(cT >> 2);
    long want = (long)(wantT >> 2);
    int cls;
#ifdef NPL_TRACE_APPEND
    fprintf(stderr, "[class c=%ld want=%ld]", c, want);
#endif
    if (c == 32 || c == 9 || c == 10 || c == 13)      cls = 0; /* ws    */
    else if (c == ';')                                 cls = 1;
    else if (c == '(')                                 cls = 2;
    else if (c == ')')                                 cls = 3;
    else if (c == '\'')                                cls = 4;
    else if (c == '"')                                 cls = 5;
    else if (c >= '0' && c <= '9')                     cls = 6; /* digit */
    else if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
             (c >= '0' && c <= '9') ||
             c=='-' || c=='_' || c=='?' || c=='!' || c=='*' ||
             c=='/' || c=='+' || c=='=' || c=='<' || c=='>' ||
             c=='%' || c=='.' || c==':')                 cls = 7; /* symch */
    else if (c == '#')                                 cls = 9; /* hash */
    else if (c == '+')                                 cls = 10;
    else                                               cls = 8; /* bad  */
    return npl_bool(cls == want);
}

/* Print the TAGGED bits of a fixnum as decimal text ("copy <v>" literal). */
NplVal npl_tagged_num_to_string(NplVal v) {
    if (!npl_is_fixnum(v)) {
        fprintf(stderr, "tagged->string: not a fixnum (v=%lu)\n", (unsigned long)v);
        exit(1);
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "%lld", (long long)v);
    return box_bytes(buf, (uint32_t)strlen(buf), BOX_STRING);
}

NplVal npl_lex_scan_sym_end(NplVal sT, NplVal jT) {
    const NplString *s = lex_str(sT);
    uint32_t len = s->length;
    uint32_t j = (uint32_t)(jT >> 2);
    while (j < len) {
        unsigned char c = (unsigned char)s->data[j];
        int is_sym = ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                      (c >= '0' && c <= '9') ||
                      c=='-' || c=='_' || c=='?' || c=='!' || c=='*' ||
                      c=='/' || c=='+' || c=='=' || c=='<' || c=='>' ||
                      c=='%' || c=='.' || c==':');
        if (!is_sym) break;
        j++;
    }
    return npl_fixnum(j);
}

NplVal npl_lex_number(NplVal sT, NplVal iT, NplVal jT) {
    /* [i,j) may start with '-'; everything raw inside C */
    const NplString *s = lex_str(sT);
    uint32_t i = (uint32_t)(iT >> 2);
    uint32_t j = (uint32_t)(jT >> 2);
    long acc = 0;
    int neg = 0;
    if (i < j && s->data[i] == '-') { neg = 1; i++; }
    while (i < j) {
        if (s->data[i] >= '0' && s->data[i] <= '9')
            acc = acc * 10 + (s->data[i] - '0');
        i++;
    }
    return npl_fixnum(neg ? -acc : acc);
}

static int lex_symbol_char(unsigned char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '?' ||
           c == '!' || c == '*' || c == '/' || c == '+' || c == '=' ||
           c == '<' || c == '>' || c == '%' || c == '.' || c == ':';
}

static NplVal lex_token(int kind, NplVal payload)
{
    return npl_cons(npl_fixnum(kind), payload);
}

static NplVal reverse_token_stream(NplVal stream)
{
    NplVal result = npl_nil();
    while (!npl_is_nil(stream)) {
        NplVal next = npl_cdr(stream);
        npl_set_cdr(stream, result);
        result = stream;
        stream = next;
    }
    return result;
}

NplVal npl_lex_tokens(NplVal source)
{
    const NplString *string = lex_str(source);
    const unsigned char *data = (const unsigned char *)string->data;
    uint32_t length = string->length;
    uint32_t i = 0;
    NplVal tokens = npl_nil();

    while (i < length) {
        unsigned char c = data[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            i++;
            continue;
        }
        if (c == ';') {
            while (i < length && data[i] != '\n') i++;
            continue;
        }
        if (c == '(' || c == ')' || c == '\'') {
            int kind = c == '(' ? 1 : (c == ')' ? 2 : 3);
            tokens = npl_cons(lex_token(kind, npl_nil()), tokens);
            i++;
            continue;
        }
        if (c == '"') {
            uint32_t start = ++i;
            while (i < length && data[i] != '"') i++;
            if (i == length) {
                fprintf(stdout, "nplc1: unterminated string literal\n");
                fflush(stdout);
                _Exit(20);
            }
            NplVal text = box_bytes((const char *)data + start,
                                    i - start, BOX_STRING);
            tokens = npl_cons(lex_token(6, text), tokens);
            i++;
            continue;
        }
        if (c == '#') {
            if (i + 1 >= length || (data[i + 1] != 't' && data[i + 1] != 'f')) {
                fprintf(stdout, "nplc1: bad boolean literal\n");
                fflush(stdout);
                _Exit(20);
            }
            int kind = data[i + 1] == 't' ? 8 : 9;
            tokens = npl_cons(lex_token(kind, npl_nil()), tokens);
            i += 2;
            continue;
        }
        if ((c >= '0' && c <= '9') ||
            (c == '-' && i + 1 < length &&
             data[i + 1] >= '0' && data[i + 1] <= '9')) {
            int negative = c == '-';
            if (negative) i++;
            int64_t number = 0;
            while (i < length && data[i] >= '0' && data[i] <= '9') {
                number = number * 10 + (data[i] - '0');
                i++;
            }
            if (negative) number = -number;
            tokens = npl_cons(lex_token(5, npl_fixnum(number)), tokens);
            continue;
        }
        if (lex_symbol_char(c)) {
            uint32_t start = i++;
            while (i < length && lex_symbol_char(data[i])) i++;
            NplVal symbol = box_bytes((const char *)data + start,
                                      i - start, BOX_STRING);
            tokens = npl_cons(lex_token(4, symbol), tokens);
            continue;
        }

        fprintf(stdout, "nplc1: bad character byte %u\n", (unsigned)c);
        fflush(stdout);
        _Exit(20);
    }

    tokens = npl_cons(lex_token(7, npl_nil()), tokens);
    return reverse_token_stream(tokens);
}

void npl_exit(NplVal code) {
    int64_t v = (int64_t)(code >> 2);          /* untag fixnum */
    fflush(stdout);
    _Exit((int)(v & 255));
}


/* ---- Helper: fixnum to string ---- */

char *npl_fixnum_to_string(int64_t v) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%lld", (long long)v);
    return strdup(buf);
}

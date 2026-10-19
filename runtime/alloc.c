/*
 * alloc.c — NPL runtime allocation and boxed values.
 *
 * Managed objects live in mmap chunks. Each object has private allocator
 * metadata immediately before its public NplHeader; sweep recycles metadata
 * blocks through a free list while chunks remain mapped for the process life.
 */

#include "npl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>

#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif

#ifndef NPL_GC_MIN_THRESHOLD
#define NPL_GC_MIN_THRESHOLD (1024u * 1024u * 1024u)
#endif
#define NPL_CHUNK_SIZE (1024u * 1024u)
#define NPL_ALIGN      16u
#define NPL_META_MAGIC_LIVE UINT64_C(0x4e504c4d45544131)
#define NPL_META_MAGIC_FREE UINT64_C(0x4e504c4d45544130)
#define NPL_ROOTS_INITIAL 256
#define NPL_CHUNK_INDEX_SHIFT 20
#define NPL_CHUNK_INDEX_BUCKETS 65536
#define NPL_FREE_EXACT_BINS 128
#define NPL_FREE_BIN_COUNT (NPL_FREE_EXACT_BINS + 1)

typedef struct NplChunkIndexEntry NplChunkIndexEntry;

typedef struct NplChunk NplChunk;
typedef struct NplAllocMeta NplAllocMeta;

struct NplChunk {
    NplChunk *next;
    size_t map_size;
    size_t used;
    size_t live_blocks;
    unsigned char data[];
};

struct NplChunkIndexEntry {
    NplChunkIndexEntry *next;
    NplChunk *chunk;
};

struct NplAllocMeta {
    uint64_t magic;
    NplAllocMeta *all_next;
    NplAllocMeta *free_next;
    NplAllocMeta *mark_next;
    NplChunk *owner;
    size_t size;
    size_t capacity;
    uint32_t marked;
    uint32_t reserved;
};

#define NPL_META_SIZE ((sizeof(NplAllocMeta) + NPL_ALIGN - 1) & ~(NPL_ALIGN - 1))

static NplChunk *g_chunks = NULL;
static NplChunkIndexEntry *g_chunk_index[NPL_CHUNK_INDEX_BUCKETS];
static NplAllocMeta *g_alloc_head = NULL;
static NplAllocMeta *g_free_bins[NPL_FREE_BIN_COUNT];
/* Bit b is set when g_free_bins[b] is non-empty, so an allocation skips
 * empty bins instead of scanning all of them. */
#define NPL_FREE_MASK_WORDS ((NPL_FREE_BIN_COUNT + 63) / 64)
static uint64_t g_free_bin_mask[NPL_FREE_MASK_WORDS];
static NplAllocMeta *g_mark_stack = NULL;
static size_t g_allocated = 0;
static size_t g_threshold = NPL_GC_MIN_THRESHOLD;
static size_t g_min_threshold = NPL_GC_MIN_THRESHOLD;
static size_t g_live_objects = 0;
static size_t g_collections = 0;
static NplVal **g_roots = NULL;
static int g_root_count = 0;
static int g_root_capacity = 0;
static uintptr_t g_stack_bottom = 0;
static int g_runtime_initialized = 0;
static int g_gc_running = 0;

static size_t align_up(size_t n, size_t alignment)
{
    return (n + alignment - 1) & ~(alignment - 1);
}

void npl_runtime_oom_internal(void)
{
    static const char msg[] = "npl runtime: out of memory\n";
    (void)write(STDERR_FILENO, msg, sizeof(msg) - 1);
    _Exit(1);
}

static size_t chunk_index_bucket(uintptr_t address)
{
    return (size_t)((address >> NPL_CHUNK_INDEX_SHIFT) &
                     (NPL_CHUNK_INDEX_BUCKETS - 1));
}

static void index_chunk(NplChunk *chunk)
{
    uintptr_t begin = (uintptr_t)chunk;
    uintptr_t end = begin + chunk->map_size - 1;
    uintptr_t first = begin >> NPL_CHUNK_INDEX_SHIFT;
    uintptr_t last = end >> NPL_CHUNK_INDEX_SHIFT;
    for (uintptr_t page = first; page <= last; page++) {
        size_t bucket = chunk_index_bucket(page << NPL_CHUNK_INDEX_SHIFT);
        NplChunkIndexEntry *entry =
            (NplChunkIndexEntry *)malloc(sizeof(NplChunkIndexEntry));
        if (!entry) npl_runtime_oom_internal();
        entry->chunk = chunk;
        entry->next = g_chunk_index[bucket];
        g_chunk_index[bucket] = entry;
    }
}

static NplChunkIndexEntry *chunks_for_address(uintptr_t address)
{
    return g_chunk_index[chunk_index_bucket(address)];
}

static NplChunk *map_chunk(size_t need)
{
    size_t header = align_up(sizeof(NplChunk), NPL_ALIGN);
    size_t map_size = NPL_CHUNK_SIZE;
    while (map_size < header + need) map_size += NPL_CHUNK_SIZE;

    void *mem = mmap(NULL, map_size, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED) npl_runtime_oom_internal();

    NplChunk *chunk = (NplChunk *)mem;
    chunk->next = g_chunks;
    chunk->map_size = map_size;
    chunk->used = header;
    chunk->live_blocks = 0;
    g_chunks = chunk;
    index_chunk(chunk);
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
    for (NplChunkIndexEntry *entry = chunks_for_address(address);
         entry; entry = entry->next) {
        NplChunk *chunk = entry->chunk;
        uintptr_t begin = (uintptr_t)chunk;
        uintptr_t end = begin + chunk->used;
        if (address < begin + NPL_META_SIZE || address >= end) continue;
        NplAllocMeta *meta = (NplAllocMeta *)(address - NPL_META_SIZE);
        if (meta->magic == NPL_META_MAGIC_LIVE && meta_object(meta) == object)
            return meta;
    }
    return NULL;
}


static size_t free_bin_index(size_t capacity)
{
    size_t units = capacity / NPL_ALIGN;
    if (units == 0) units = 1;
    return units <= NPL_FREE_EXACT_BINS
        ? units - 1
        : NPL_FREE_EXACT_BINS;
}

static void add_free_block(NplAllocMeta *meta)
{
    size_t bin = free_bin_index(meta->capacity);
    meta->free_next = g_free_bins[bin];
    g_free_bins[bin] = meta;
    g_free_bin_mask[bin / 64] |= UINT64_C(1) << (bin % 64);
}

static unsigned lowest_set_bit(uint64_t word)
{
#if defined(__GNUC__) || defined(__clang__)
    return (unsigned)__builtin_ctzll(word);
#else
    unsigned bit = 0;
    while (!(word & 1)) {
        word >>= 1;
        bit++;
    }
    return bit;
#endif
}

/* The first non-empty bin at or after first, or NPL_FREE_BIN_COUNT. */
static size_t next_free_bin(size_t first)
{
    size_t word = first / 64;
    if (word >= NPL_FREE_MASK_WORDS) return NPL_FREE_BIN_COUNT;
    uint64_t bits = g_free_bin_mask[word] & (~UINT64_C(0) << (first % 64));
    for (;;) {
        if (bits) {
            size_t bin = word * 64 + lowest_set_bit(bits);
            return bin < NPL_FREE_BIN_COUNT ? bin : NPL_FREE_BIN_COUNT;
        }
        if (++word >= NPL_FREE_MASK_WORDS) return NPL_FREE_BIN_COUNT;
        bits = g_free_bin_mask[word];
    }
}

static NplAllocMeta *take_free_block(size_t size)
{
    for (size_t bin = next_free_bin(free_bin_index(size));
         bin < NPL_FREE_BIN_COUNT; bin = next_free_bin(bin + 1)) {
        NplAllocMeta **link = &g_free_bins[bin];
        while (*link && (*link)->capacity < size)
            link = &(*link)->free_next;
        if (!*link) continue;

        NplAllocMeta *meta = *link;
        *link = meta->free_next;
        if (!g_free_bins[bin])
            g_free_bin_mask[bin / 64] &= ~(UINT64_C(1) << (bin % 64));
        if (meta->capacity >= size + NPL_META_SIZE + NPL_ALIGN) {
            NplAllocMeta *remainder = (NplAllocMeta *)(
                (unsigned char *)meta + NPL_META_SIZE + size);
            memset(remainder, 0, NPL_META_SIZE);
            remainder->magic = NPL_META_MAGIC_FREE;
            remainder->owner = meta->owner;
            remainder->capacity = meta->capacity - size - NPL_META_SIZE;
            add_free_block(remainder);
            meta->capacity = size;
        }
        return meta;
    }
    return NULL;
}

void *npl_alloc_object(size_t size)
{
    if (!g_runtime_initialized)
        npl_runtime_init();
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
        meta->owner = chunk;
    }

    NplChunk *owner = meta->owner;
    if (!owner) npl_runtime_oom_internal();
    owner->live_blocks++;
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

/* NPL_GC_THRESHOLD_BYTES overrides the minimum collection threshold so tests
 * can force frequent collections; malformed or zero values are ignored. */
static size_t initial_threshold(void)
{
    const char *text = getenv("NPL_GC_THRESHOLD_BYTES");
    if (!text || !*text) return NPL_GC_MIN_THRESHOLD;
    char *end = NULL;
    unsigned long long value = strtoull(text, &end, 10);
    if (*end != '\0' || value == 0 || value > SIZE_MAX)
        return NPL_GC_MIN_THRESHOLD;
    return (size_t)value;
}

void npl_runtime_init(void)
{
    uintptr_t marker = 0;
    if (g_runtime_initialized) return;
    g_runtime_initialized = 1;
    g_stack_bottom = (uintptr_t)(void *)&marker + sizeof(marker);
    g_min_threshold = initial_threshold();
    g_threshold = g_min_threshold;
    g_root_count = 0;
    npl_control_init();
    npl_io_init();
}

/* ---- GC root management and collector internals ---- */

void npl_gc_root(NplVal *root)
{
    /* Compiled programs register one root per global slot, so grow. */
    if (g_root_count == g_root_capacity) {
        int capacity = g_root_capacity ? g_root_capacity * 2
                                       : NPL_ROOTS_INITIAL;
        NplVal **roots = (NplVal **)realloc(
            g_roots, (size_t)capacity * sizeof(NplVal *));
        if (!roots) npl_runtime_oom_internal();
        g_roots = roots;
        g_root_capacity = capacity;
    }
    g_roots[g_root_count++] = root;
}

/* Primitive-error unwinding (trampoline.c) drops roots registered by the C
 * frames it discards; roots are registered and released in LIFO order. */
int npl_gc_root_count_internal(void) { return g_root_count; }

void npl_gc_restore_root_count_internal(int count)
{
    if (count >= 0 && count <= g_root_count) g_root_count = count;
}

/* Search from the top (roots are normally released LIFO) and keep the
 * order of the rest, so a saved root count still marks the same prefix. */
void npl_gc_unroot(NplVal *root)
{
    for (int i = g_root_count - 1; i >= 0; i--) {
        if (g_roots[i] == root) {
            memmove(&g_roots[i], &g_roots[i + 1],
                    (size_t)(g_root_count - 1 - i) * sizeof(NplVal *));
            g_root_count--;
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
    npl_control_mark_roots();
    npl_symbol_mark_roots();
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
        case BOX_BIGNUM:
            break;
        case BOX_STRING:
        case BOX_SYMBOL:
            npl_string_mark_internal((const NplString *)header);
            break;
        case BOX_RATIONAL: {
            NplRational *rational = (NplRational *)header;
            npl_gc_mark_candidate_internal(rational->numerator);
            npl_gc_mark_candidate_internal(rational->denominator);
            break;
        }
        case BOX_COMPLEX: {
            NplComplex *complex = (NplComplex *)header;
            npl_gc_mark_candidate_internal(complex->real);
            npl_gc_mark_candidate_internal(complex->imag);
            break;
        }
        case BOX_RECORD:
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
        case BOX_VALUES: {
            NplValues *values = (NplValues *)header;
            for (uint32_t i = 0; i < values->count; i++)
                npl_gc_mark_candidate_internal(values->data[i]);
            break;
        }
        case BOX_WIND_FRAME: {
            NplWindFrame *frame = (NplWindFrame *)header;
            npl_gc_mark_candidate_internal(frame->parent);
            npl_gc_mark_candidate_internal(frame->before);
            npl_gc_mark_candidate_internal(frame->after);
            break;
        }
        case BOX_WIND_TRANSITION: {
            NplWindTransition *transition = (NplWindTransition *)header;
            npl_gc_mark_candidate_internal(transition->target);
            npl_gc_mark_candidate_internal(transition->target_handler);
            npl_gc_mark_candidate_internal(transition->continuation);
            npl_gc_mark_candidate_internal(transition->value);
            npl_gc_mark_candidate_internal(transition->entering);
            uint32_t count = transition->exit_count + transition->entry_count;
            for (uint32_t i = 0; i < count; i++)
                npl_gc_mark_candidate_internal(transition->frames[i]);
            break;
        }
        case BOX_HANDLER_FRAME: {
            NplHandlerFrame *frame = (NplHandlerFrame *)header;
            npl_gc_mark_candidate_internal(frame->parent);
            npl_gc_mark_candidate_internal(frame->procedure);
            break;
        }
        case BOX_RAISE_CONTEXT: {
            NplRaiseContext *context = (NplRaiseContext *)header;
            npl_gc_mark_candidate_internal(context->frame);
            npl_gc_mark_candidate_internal(context->continuation);
            npl_gc_mark_candidate_internal(context->value);
            break;
        }
        case BOX_PARAMETER: {
            NplParameter *parameter = (NplParameter *)header;
            npl_gc_mark_candidate_internal(parameter->converter);
            npl_gc_mark_candidate_internal(parameter->value);
            break;
        }
        case BOX_PARAMETER_BINDING: {
            NplParameterBinding *binding = (NplParameterBinding *)header;
            npl_gc_mark_candidate_internal(binding->parent);
            npl_gc_mark_candidate_internal(binding->parameter);
            npl_gc_mark_candidate_internal(binding->value);
            break;
        }
        case BOX_PARAMETER_CONTEXT: {
            NplParameterContext *context = (NplParameterContext *)header;
            npl_gc_mark_candidate_internal(context->parameter);
            npl_gc_mark_candidate_internal(context->continuation);
            break;
        }
        case BOX_PROMISE: {
            NplPromise *promise = (NplPromise *)header;
            npl_gc_mark_candidate_internal(promise->value);
            break;
        }
        case BOX_PROMISE_CONTEXT: {
            NplPromiseContext *context = (NplPromiseContext *)header;
            npl_gc_mark_candidate_internal(context->root);
            npl_gc_mark_candidate_internal(context->current);
            npl_gc_mark_candidate_internal(context->continuation);
            break;
        }
        case BOX_PORT: {
            NplPort *port = (NplPort *)header;
            npl_gc_mark_candidate_internal(port->buffer);
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
        NplChunk *owner = meta->owner;
        if (!owner || owner->live_blocks == 0) npl_runtime_oom_internal();
        owner->live_blocks--;
        g_allocated -= meta->size;
        g_live_objects--;
        meta->magic = NPL_META_MAGIC_FREE;
        meta->all_next = NULL;
        meta->mark_next = NULL;
        meta->free_next = NULL;
        add_free_block(meta);
    }

    g_threshold = g_allocated * 2;
    if (g_threshold < g_min_threshold)
        g_threshold = g_min_threshold;
    g_collections++;
    g_gc_running = 0;
}

uintptr_t npl_gc_swap_stack_bottom(uintptr_t stack_bottom)
{
    uintptr_t previous = g_stack_bottom;
    g_stack_bottom = stack_bottom;
    return previous;
}

uintptr_t npl_gc_stack_bottom_internal(void)
{
    return g_stack_bottom;
}

size_t npl_gc_allocated(void) { return g_allocated; }
size_t npl_gc_threshold(void) { return g_threshold; }
size_t npl_gc_live_objects(void) { return g_live_objects; }
size_t npl_gc_collections(void) { return g_collections; }

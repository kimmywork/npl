/*
 * alloc.c — NPL runtime allocation and boxed values.
 *
 * Managed objects live in mmap chunks. Each object has private allocator
 * metadata immediately before its public NplHeader. A chunk is a sequence of
 * blocks (metadata, then capacity bytes) from its header to chunk->used, so
 * sweep walks each chunk in address order, merges each run of dead and free
 * blocks into one free block, and rebuilds the free bins from those runs.
 * Chunks remain mapped for the process life.
 *
 * Generations: objects allocated since the last collection are young and
 * listed in the young log. A minor collection (the nursery is full) marks
 * from the roots and the remembered set, follows young objects only, and
 * sweeps the young log alone, so its cost follows the nursery, not the
 * heap. Young survivors are promoted in place (NPL_FLAG_OLD); objects never
 * move. A full collection, run when the whole heap reaches the threshold,
 * coalesces every chunk.
 *
 * Allocation bumps a cursor through one region at a time: a hole (a free
 * run of at least NPL_FREE_EXACT_BINS * NPL_ALIGN bytes, from the large bin)
 * or the unused tail of the newest chunk. Small free blocks are used only
 * when no region fits, before a new chunk is mapped. Bumping keeps new
 * objects contiguous: with first-fit reuse of scattered free blocks, the
 * compiler's own mutator frames took 2-3x the samples under a nursery
 * (profile, worklog 2026-10-20).
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
#define NPL_GC_MIN_THRESHOLD (128u * 1024u * 1024u)
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
#ifndef NPL_GC_NURSERY_DEFAULT
#define NPL_GC_NURSERY_DEFAULT (1024u * 1024u)
#endif

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
static NplAllocMeta **g_young = NULL;    /* allocated since the last collection */
static size_t g_young_count = 0;
static size_t g_young_capacity = 0;
static size_t g_young_bytes = 0;
static size_t g_nursery_bytes = NPL_GC_NURSERY_DEFAULT;
static int g_minor = 0;
/* Current bump region: [g_bump_cursor, g_bump_limit) in g_bump_chunk. A
 * hole region is free memory inside chunk->used; a tail region advances
 * chunk->used with the cursor. */
static unsigned char *g_bump_cursor = NULL;
static unsigned char *g_bump_limit = NULL;
static NplChunk *g_bump_chunk = NULL;
static int g_bump_hole = 0;
static size_t g_minor_collections = 0;
static NplHeader **g_remembered = NULL;
static size_t g_remembered_count = 0;
static size_t g_remembered_capacity = 0;

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

/* First block in bin with capacity >= size, unlinked; NULL when none. */
static NplAllocMeta *take_from_bin(size_t bin, size_t size)
{
    NplAllocMeta **link = &g_free_bins[bin];
    while (*link && (*link)->capacity < size)
        link = &(*link)->free_next;
    if (!*link) return NULL;
    NplAllocMeta *meta = *link;
    *link = meta->free_next;
    if (!g_free_bins[bin])
        g_free_bin_mask[bin / 64] &= ~(UINT64_C(1) << (bin % 64));
    return meta;
}

static NplAllocMeta *take_free_block(size_t size)
{
    for (size_t bin = next_free_bin(free_bin_index(size));
         bin < NPL_FREE_BIN_COUNT; bin = next_free_bin(bin + 1)) {
        NplAllocMeta *meta = take_from_bin(bin, size);
        if (!meta) continue;
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

/* Carve one block from the bump region. A remainder too small to hold a
 * free block's metadata is absorbed into this block, so the region always
 * ends on a block boundary. */
static NplAllocMeta *bump_take(size_t size)
{
    size_t total = NPL_META_SIZE + size;
    if (!g_bump_cursor || (size_t)(g_bump_limit - g_bump_cursor) < total)
        return NULL;
    NplAllocMeta *meta = (NplAllocMeta *)(void *)g_bump_cursor;
    size_t capacity = size;
    size_t left = (size_t)(g_bump_limit - g_bump_cursor) - total;
    if (left < NPL_META_SIZE) capacity += left;
    g_bump_cursor += NPL_META_SIZE + capacity;
    if (!g_bump_hole)
        g_bump_chunk->used =
            (size_t)(g_bump_cursor - (unsigned char *)g_bump_chunk);
    memset(meta, 0, NPL_META_SIZE);
    meta->capacity = capacity;
    meta->owner = g_bump_chunk;
    return meta;
}

/* End the bump region: the unused part of a hole becomes a free block, so
 * chunk walks (sweep, verification) see only whole blocks. */
static void retire_bump_region(void)
{
    if (g_bump_hole && g_bump_cursor < g_bump_limit) {
        NplAllocMeta *rest = (NplAllocMeta *)(void *)g_bump_cursor;
        memset(rest, 0, NPL_META_SIZE);
        rest->magic = NPL_META_MAGIC_FREE;
        rest->owner = g_bump_chunk;
        rest->capacity =
            (size_t)(g_bump_limit - g_bump_cursor) - NPL_META_SIZE;
        add_free_block(rest);
    }
    g_bump_cursor = g_bump_limit = NULL;
    g_bump_chunk = NULL;
    g_bump_hole = 0;
}

static void start_region(NplChunk *chunk, unsigned char *begin,
                         unsigned char *end, int hole)
{
    g_bump_chunk = chunk;
    g_bump_cursor = begin;
    g_bump_limit = end;
    g_bump_hole = hole;
}

static NplAllocMeta *refill_and_take(size_t size)
{
    size_t total = NPL_META_SIZE + size;
    retire_bump_region();
    NplAllocMeta *hole = take_from_bin(NPL_FREE_EXACT_BINS, size);
    if (hole) {
        unsigned char *begin = (unsigned char *)hole;
        start_region(hole->owner, begin,
                     begin + NPL_META_SIZE + hole->capacity, 1);
        return bump_take(size);
    }
    NplChunk *chunk = g_chunks;
    if (!chunk || chunk->used + total > chunk->map_size) {
        NplAllocMeta *meta = take_free_block(size);
        if (meta) return meta;
        chunk = map_chunk(total);
    }
    start_region(chunk, (unsigned char *)chunk + chunk->used,
                 (unsigned char *)chunk + chunk->map_size, 0);
    return bump_take(size);
}

void *npl_alloc_object(size_t size)
{
    if (!g_runtime_initialized)
        npl_runtime_init();
    size = align_up(size, NPL_ALIGN);
    if (g_runtime_initialized && !g_gc_running) {
        if (g_allocated + size >= g_threshold)
            npl_gc_collect();
        else if (g_nursery_bytes != 0 &&
                 g_young_bytes + size >= g_nursery_bytes)
            npl_gc_collect_minor_internal();
    }

    NplAllocMeta *meta = bump_take(size);
    if (!meta) meta = refill_and_take(size);

    NplChunk *owner = meta->owner;
    if (!owner) npl_runtime_oom_internal();
    owner->live_blocks++;
    /* Without a nursery every collection is full: no young log. */
    if (g_nursery_bytes != 0) {
        if (g_young_count == g_young_capacity) {
            size_t capacity = g_young_capacity ? g_young_capacity * 2 : 65536;
            NplAllocMeta **grown = (NplAllocMeta **)realloc(
                g_young, capacity * sizeof(NplAllocMeta *));
            if (!grown) npl_runtime_oom_internal();
            g_young = grown;
            g_young_capacity = capacity;
        }
        g_young[g_young_count++] = meta;
    }
    g_young_bytes += size;
    meta->magic = NPL_META_MAGIC_LIVE;
    meta->size = size;
    meta->marked = 0;
    meta->mark_next = NULL;
    meta->free_next = NULL;
    memset(meta_object(meta), 0, meta->capacity);
    g_allocated += size;
    g_live_objects++;
    return meta_object(meta);
}

/* A byte count from the environment, or fallback when unset or malformed;
 * zero is accepted only when allow_zero. */
static size_t env_bytes(const char *name, size_t fallback, int allow_zero)
{
    const char *text = getenv(name);
    if (!text || !*text) return fallback;
    char *end = NULL;
    unsigned long long value = strtoull(text, &end, 10);
    if (*end != '\0' || (value == 0 && !allow_zero) || value > SIZE_MAX)
        return fallback;
    return (size_t)value;
}

void npl_runtime_init(void)
{
    uintptr_t marker = 0;
    if (g_runtime_initialized) return;
    g_runtime_initialized = 1;
    g_stack_bottom = (uintptr_t)(void *)&marker + sizeof(marker);
    npl_stack_init_internal(g_stack_bottom);
    /* NPL_GC_THRESHOLD_BYTES: minimum heap size for a full collection;
     * NPL_GC_NURSERY_BYTES: young bytes per minor collection, 0 = none. */
    g_min_threshold = env_bytes("NPL_GC_THRESHOLD_BYTES",
                                NPL_GC_MIN_THRESHOLD, 0);
    g_nursery_bytes = env_bytes("NPL_GC_NURSERY_BYTES",
                                NPL_GC_NURSERY_DEFAULT, 1);
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

void npl_gc_begin_internal(int minor)
{
    retire_bump_region();
    g_gc_running = 1;
    g_minor = minor;
    g_mark_stack = NULL;
}

void npl_gc_mark_candidate_internal(NplVal value)
{
    if (!npl_is_ptr(value)) return;
    NplAllocMeta *meta = find_managed_meta(npl_header(value));
    if (!meta || meta->marked) return;
    /* A minor collection neither marks nor traces old objects: their young
     * children are reached through the remembered set. */
    if (g_minor && (((NplHeader *)meta_object(meta))->flags & NPL_FLAG_OLD))
        return;
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
    if (g_minor)
        for (size_t i = 0; i < g_remembered_count; i++)
            npl_gc_visit_children_internal(g_remembered[i],
                                           npl_gc_mark_candidate_internal);
}

void npl_gc_drain_internal(void)
{
    while (g_mark_stack) {
        NplAllocMeta *meta = g_mark_stack;
        g_mark_stack = meta->mark_next;
        npl_gc_visit_children_internal((NplHeader *)meta_object(meta),
                       npl_gc_mark_candidate_internal);
    }
}

/* ---- Generations: remembered set and barrier verification ----
 *
 * An object that survives a collection is old (NPL_FLAG_OLD, set by sweep).
 * npl_gc_write_barrier (npl.h) records an old object that receives a
 * pointer to a young one; the set is empty again after each collection. */


/* The write barrier for compiled code, which stores into a field inline
 * (compiler backend/inline.npl): holder is a tagged heap value. */
void npl_gc_write_barrier_value(NplVal holder, NplVal value)
{
    npl_gc_write_barrier(npl_header(holder), value);
}

void npl_gc_remember_internal(NplHeader *object)
{
    if (g_remembered_count == g_remembered_capacity) {
        size_t capacity = g_remembered_capacity ? g_remembered_capacity * 2
                                                : 1024;
        NplHeader **grown = (NplHeader **)realloc(
            g_remembered, capacity * sizeof(NplHeader *));
        if (!grown) npl_runtime_oom_internal();
        g_remembered = grown;
        g_remembered_capacity = capacity;
    }
    object->flags |= NPL_FLAG_REMEMBERED;
    g_remembered[g_remembered_count++] = object;
}

static NplHeader *g_verify_holder = NULL;
static size_t g_verify_missed = 0;
static int g_verify_report = 0;

static void verify_child(NplVal value)
{
    if (!npl_is_ptr(value)) return;
    NplHeader *child = npl_header(value);
    if (!find_managed_meta(child) || (child->flags & NPL_FLAG_OLD)) return;
    if (g_verify_report && g_verify_missed < 8)
        fprintf(stderr, "npl gc verify: old tag %u holds young tag %u "
                "without a barrier\n", g_verify_holder->tag, child->tag);
    g_verify_missed++;
}

/* Count old, unremembered objects holding a pointer to a young object: each
 * is a store that skipped npl_gc_write_barrier. */
size_t npl_gc_verify_barriers(int report)
{
    retire_bump_region();  /* chunk walks need whole blocks */
    g_verify_missed = 0;
    g_verify_report = report;
    for (NplChunk *chunk = g_chunks; chunk; chunk = chunk->next) {
        unsigned char *cursor =
            (unsigned char *)chunk + align_up(sizeof(NplChunk), NPL_ALIGN);
        unsigned char *end = (unsigned char *)chunk + chunk->used;
        while (cursor < end) {
            NplAllocMeta *meta = (NplAllocMeta *)cursor;
            cursor += NPL_META_SIZE + meta->capacity;
            if (meta->magic != NPL_META_MAGIC_LIVE) continue;
            NplHeader *header = (NplHeader *)meta_object(meta);
            if ((header->flags & (NPL_FLAG_OLD | NPL_FLAG_REMEMBERED)) !=
                NPL_FLAG_OLD)
                continue;
            g_verify_holder = header;
            npl_gc_visit_children_internal(header, verify_child);
        }
    }
    return g_verify_missed;
}

/* Walk one chunk in address order. Unmarked live objects become free; each
 * run of adjacent free blocks merges into its first block, which is binned
 * once the run ends. */
static void sweep_chunk(NplChunk *chunk)
{
    unsigned char *cursor =
        (unsigned char *)chunk + align_up(sizeof(NplChunk), NPL_ALIGN);
    unsigned char *end = (unsigned char *)chunk + chunk->used;
    NplAllocMeta *run = NULL;
    while (cursor < end) {
        NplAllocMeta *meta = (NplAllocMeta *)cursor;
        cursor += NPL_META_SIZE + meta->capacity;
        if (meta->magic == NPL_META_MAGIC_LIVE) {
            if (meta->marked) {
                NplHeader *header = (NplHeader *)meta_object(meta);
                header->flags = (header->flags | NPL_FLAG_OLD) &
                                ~(uint32_t)NPL_FLAG_REMEMBERED;
                meta->marked = 0;
                meta->mark_next = NULL;
                if (run) {
                    add_free_block(run);
                    run = NULL;
                }
                continue;
            }
            if (chunk->live_blocks == 0) npl_runtime_oom_internal();
            chunk->live_blocks--;
            g_allocated -= meta->size;
            g_live_objects--;
        }
        meta->magic = NPL_META_MAGIC_FREE;
        if (run) {
            run->capacity += NPL_META_SIZE + meta->capacity;
        } else {
            run = meta;
            run->mark_next = NULL;
        }
    }
    if (run) add_free_block(run);
}

/* Every survivor is now old: no remembered entries and no young objects. */
static void end_young_generation(void)
{
    for (size_t i = 0; i < g_remembered_count; i++)
        g_remembered[i]->flags &= ~(uint32_t)NPL_FLAG_REMEMBERED;
    g_remembered_count = 0;
    g_young_count = 0;
    g_young_bytes = 0;
}

static int g_poison_freed = 0;

/* Minor sweep over the young log: marked young objects are promoted,
 * unmarked ones are freed. Blocks split from one free run are logged in
 * ascending address order, so consecutive dead entries that touch in memory
 * merge into one free block; later allocations then split that run instead
 * of scattering over single-object blocks. */
void npl_gc_sweep_minor_internal(void)
{
    NplAllocMeta *run = NULL;
    for (size_t i = 0; i < g_young_count; i++) {
        NplAllocMeta *meta = g_young[i];
        NplHeader *header = (NplHeader *)meta_object(meta);
        if (meta->marked) {
            header->flags |= NPL_FLAG_OLD;
            meta->marked = 0;
            meta->mark_next = NULL;
            if (run) {
                add_free_block(run);
                run = NULL;
            }
            continue;
        }
        NplChunk *owner = meta->owner;
        if (!owner || owner->live_blocks == 0) npl_runtime_oom_internal();
        owner->live_blocks--;
        g_allocated -= meta->size;
        g_live_objects--;
        if (g_poison_freed) memset(header, 0xdb, meta->capacity);
        meta->magic = NPL_META_MAGIC_FREE;
        meta->mark_next = NULL;
        if (run && (unsigned char *)run + NPL_META_SIZE + run->capacity ==
                       (unsigned char *)meta) {
            run->capacity += NPL_META_SIZE + meta->capacity;
            continue;
        }
        if (run) add_free_block(run);
        run = meta;
    }
    if (run) add_free_block(run);
    end_young_generation();
    g_minor_collections++;
    g_minor = 0;
    g_gc_running = 0;
}

void npl_gc_set_poison_internal(int enabled) { g_poison_freed = enabled; }

static int address_in_chunk(uintptr_t address)
{
    for (NplChunkIndexEntry *entry = chunks_for_address(address);
         entry; entry = entry->next) {
        uintptr_t begin = (uintptr_t)entry->chunk;
        if (address >= begin && address < begin + entry->chunk->used)
            return 1;
    }
    return 0;
}

static size_t g_dangling = 0;

static void verify_live_child(NplVal value)
{
    if (!npl_is_ptr(value)) return;
    NplHeader *child = npl_header(value);
    if (find_managed_meta(child) || !address_in_chunk((uintptr_t)child))
        return;
    if (g_dangling < 8)
        fprintf(stderr, "npl gc verify: tag %u holds a freed object\n",
                g_verify_holder->tag);
    g_dangling++;
}

/* After a collection every field of a live object must reach a live object
 * (or memory outside the heap, such as a static literal). */
size_t npl_gc_verify_heap_internal(void)
{
    g_dangling = 0;
    for (NplChunk *chunk = g_chunks; chunk; chunk = chunk->next) {
        unsigned char *cursor =
            (unsigned char *)chunk + align_up(sizeof(NplChunk), NPL_ALIGN);
        unsigned char *end = (unsigned char *)chunk + chunk->used;
        while (cursor < end) {
            NplAllocMeta *meta = (NplAllocMeta *)cursor;
            cursor += NPL_META_SIZE + meta->capacity;
            if (meta->magic != NPL_META_MAGIC_LIVE) continue;
            g_verify_holder = (NplHeader *)meta_object(meta);
            npl_gc_visit_children_internal(g_verify_holder,
                                           verify_live_child);
        }
    }
    return g_dangling;
}

void npl_gc_sweep_internal(void)
{
    memset(g_free_bins, 0, sizeof(g_free_bins));
    memset(g_free_bin_mask, 0, sizeof(g_free_bin_mask));
    for (NplChunk *chunk = g_chunks; chunk; chunk = chunk->next)
        sweep_chunk(chunk);
    end_young_generation();

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
size_t npl_gc_minor_collections(void) { return g_minor_collections; }

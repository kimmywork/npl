/*
 * symbol.c — symbol interning.
 *
 * R7RS symbols with the same name are eq?. Every symbol, whether a static box
 * emitted by the compiler (registered at program start) or one created at run
 * time by read or string->symbol, is looked up in one table keyed by its
 * bytes. Interned symbols are never collected; the table marks its entries.
 */

#include "npl.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NPL_SYMBOL_TABLE_INITIAL 512u

static NplVal *g_symbols = NULL;   /* open addressing; 0 = empty slot */
static size_t g_symbol_capacity = 0;
static size_t g_symbol_count = 0;

static uint64_t symbol_hash(const char *bytes, uint32_t length)
{
    uint64_t hash = UINT64_C(1469598103934665603);  /* FNV-1a */
    for (uint32_t i = 0; i < length; i++) {
        hash ^= (unsigned char)bytes[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static const NplString *symbol_box(NplVal symbol)
{
    return (const NplString *)npl_header(symbol);
}

static size_t symbol_slot(NplVal *table, size_t capacity, const char *bytes,
                          uint32_t length)
{
    size_t mask = capacity - 1;
    size_t slot = (size_t)symbol_hash(bytes, length) & mask;
    while (table[slot] != 0) {
        const NplString *entry = symbol_box(table[slot]);
        if (entry->length == length &&
            memcmp(entry->data, bytes, length) == 0)
            return slot;
        slot = (slot + 1) & mask;
    }
    return slot;
}

static void symbol_table_grow(void)
{
    size_t capacity = g_symbol_capacity ? g_symbol_capacity * 2
                                        : NPL_SYMBOL_TABLE_INITIAL;
    NplVal *table = (NplVal *)calloc(capacity, sizeof(NplVal));
    if (!table) npl_runtime_oom_internal();
    for (size_t i = 0; i < g_symbol_capacity; i++) {
        if (g_symbols[i] == 0) continue;
        const NplString *entry = symbol_box(g_symbols[i]);
        table[symbol_slot(table, capacity, entry->data, entry->length)] =
            g_symbols[i];
    }
    free(g_symbols);
    g_symbols = table;
    g_symbol_capacity = capacity;
}

static NplVal *symbol_find_slot(const char *bytes, uint32_t length)
{
    if (g_symbol_count * 2 >= g_symbol_capacity) symbol_table_grow();
    return &g_symbols[symbol_slot(g_symbols, g_symbol_capacity, bytes, length)];
}

NplVal npl_symbol(const char *bytes, uint32_t length)
{
    NplVal *slot = symbol_find_slot(bytes, length);
    if (*slot != 0) return *slot;
    /* Allocation may collect, but the table is only a root set: the slot
     * pointer stays valid because nothing else resizes the table. */
    NplVal symbol = npl_box_bytes_internal(bytes, length, BOX_SYMBOL);
    *slot = symbol;
    g_symbol_count++;
    return symbol;
}

/* Register a compiler-emitted static symbol. If a symbol with the same name
 * is already interned (another static box), keep the first so identity is
 * stable; the caller's slot is redirected to the canonical symbol. */
NplVal npl_intern_static_symbol(NplVal symbol)
{
    const NplString *box = symbol_box(symbol);
    NplVal *slot = symbol_find_slot(box->data, box->length);
    if (*slot != 0) return *slot;
    *slot = symbol;
    g_symbol_count++;
    return symbol;
}

NplVal npl_string_to_symbol(NplVal string)
{
    if (!npl_is_ptr(string) || npl_header(string)->tag != BOX_STRING) {
        fprintf(stderr, "string->symbol: expected a string\n");
        exit(1);
    }
    const NplString *box = (const NplString *)npl_header(string);
    return npl_symbol(box->data, box->length);
}

void npl_symbol_mark_roots(void)
{
    for (size_t i = 0; i < g_symbol_capacity; i++)
        if (g_symbols[i] != 0) npl_gc_mark_candidate_internal(g_symbols[i]);
}

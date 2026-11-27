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

/* Symbols are keyed by their characters (string.c hashes and compares
 * independently of narrow/wide storage). A lookup key is a symbol or string
 * box; the table holds symbols only. */
static const NplString *symbol_box(NplVal symbol)
{
    return (const NplString *)npl_header(symbol);
}

static size_t symbol_slot(NplVal *table, size_t capacity, const NplString *key)
{
    size_t mask = capacity - 1;
    size_t slot = (size_t)npl_string_hash_internal(key) & mask;
    while (table[slot] != 0) {
        if (npl_string_same_chars_internal(symbol_box(table[slot]), key))
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
        table[symbol_slot(table, capacity, symbol_box(g_symbols[i]))] =
            g_symbols[i];
    }
    free(g_symbols);
    g_symbols = table;
    g_symbol_capacity = capacity;
}

static NplVal *symbol_find_slot(const NplString *key)
{
    if (g_symbol_count * 2 >= g_symbol_capacity) symbol_table_grow();
    return &g_symbols[symbol_slot(g_symbols, g_symbol_capacity, key)];
}

/* Intern the characters of key (a string or symbol box): the canonical
 * symbol, created from a copy of key's characters when absent. */
static NplVal intern_characters(NplVal key)
{
    NplVal *slot = symbol_find_slot(symbol_box(key));
    if (*slot != 0) return *slot;
    npl_gc_root(&key);
    uint32_t count = npl_string_char_count_internal(symbol_box(key));
    uint32_t *chars = (uint32_t *)malloc(((size_t)count + 1) * sizeof(uint32_t));
    if (!chars) npl_runtime_oom_internal();
    for (uint32_t i = 0; i < count; i++)
        chars[i] = npl_string_char_at_internal(symbol_box(key), i);
    NplVal symbol = npl_string_from_chars_internal(chars, count, BOX_SYMBOL);
    free(chars);
    npl_gc_unroot(&key);
    /* Allocation may collect, but the table is only a root set: the slot
     * pointer stays valid because nothing else resizes the table. */
    *slot = symbol;
    g_symbol_count++;
    return symbol;
}

/* bytes are UTF-8 (ASCII for runtime-internal names). An ASCII name is
 * looked up through an unmanaged stack key, so finding an existing symbol
 * allocates nothing. */
NplVal npl_symbol(const char *bytes, uint32_t length)
{
    npl_shared_lock();
    NplVal result = npl_nil();
    int ascii = 1;
    for (uint32_t i = 0; i < length; i++)
        if ((unsigned char)bytes[i] >= 0x80) { ascii = 0; break; }
    if (ascii && length <= 256) {
        /* Aligned storage for a narrow NplString header plus its bytes. */
        uint64_t storage[(sizeof(NplString) + 257 + 7) / 8];
        NplString *probe = (NplString *)(void *)storage;
        probe->header.tag = BOX_STRING;
        probe->header.flags = 0;
        probe->length = length;
        probe->capacity = length;
        memcpy(probe->data, bytes, length);
        NplVal *slot = symbol_find_slot(probe);
        if (*slot != 0) result = *slot;
    }
    if (npl_is_nil(result)) {
        NplVal key = npl_string_from_utf8((const uint8_t *)bytes, length,
                                          BOX_STRING);
        result = intern_characters(key);
    }
    npl_shared_unlock();
    return result;
}

/* Register a compiler-emitted static symbol. If a symbol with the same name
 * is already interned (another static box), keep the first so identity is
 * stable; the caller's slot is redirected to the canonical symbol. */
NplVal npl_intern_static_symbol(NplVal symbol)
{
    npl_shared_lock();
    NplVal *slot = symbol_find_slot(symbol_box(symbol));
    NplVal result = *slot;
    if (result == 0) {   /* empty slot */
        *slot = symbol;
        g_symbol_count++;
        result = symbol;
    }
    npl_shared_unlock();
    return result;
}

NplVal npl_string_to_symbol(NplVal string)
{
    if (!npl_is_ptr(string) || npl_header(string)->tag != BOX_STRING)
        npl_primitive_error("string->symbol", "expected a string", string);
    npl_shared_lock();
    NplVal result = intern_characters(string);
    npl_shared_unlock();
    return result;
}

void npl_symbol_mark_roots(void)
{
    for (size_t i = 0; i < g_symbol_capacity; i++)
        if (g_symbols[i] != 0) npl_gc_mark_candidate_internal(g_symbols[i]);
}

/* ---- SRFI 88 keyword objects ---- */

static NplVal *g_keywords = NULL;
static size_t g_keyword_capacity = 0;
static size_t g_keyword_count = 0;

static size_t keyword_slot(NplVal *table, size_t capacity,
                           const NplString *key)
{
    size_t mask = capacity - 1;
    size_t slot = (size_t)npl_string_hash_internal(key) & mask;
    while (table[slot] != 0) {
        if (npl_string_same_chars_internal(symbol_box(table[slot]), key))
            return slot;
        slot = (slot + 1) & mask;
    }
    return slot;
}

static void keyword_table_grow(void)
{
    size_t capacity = g_keyword_capacity ? g_keyword_capacity * 2
                                         : NPL_SYMBOL_TABLE_INITIAL;
    NplVal *table = (NplVal *)calloc(capacity, sizeof(NplVal));
    if (!table) npl_runtime_oom_internal();
    for (size_t i = 0; i < g_keyword_capacity; i++) {
        if (g_keywords[i] == 0) continue;
        table[keyword_slot(table, capacity, symbol_box(g_keywords[i]))] =
            g_keywords[i];
    }
    free(g_keywords);
    g_keywords = table;
    g_keyword_capacity = capacity;
}

static NplVal *keyword_find_slot(const NplString *key)
{
    if (g_keyword_count * 2 >= g_keyword_capacity) keyword_table_grow();
    return &g_keywords[keyword_slot(g_keywords, g_keyword_capacity, key)];
}

static NplVal intern_keyword_characters(NplVal key)
{
    NplVal *slot = keyword_find_slot(symbol_box(key));
    if (*slot != 0) return *slot;
    npl_gc_root(&key);
    uint32_t count = npl_string_char_count_internal(symbol_box(key));
    uint32_t *chars = (uint32_t *)malloc(((size_t)count + 1) * sizeof(uint32_t));
    if (!chars) npl_runtime_oom_internal();
    for (uint32_t i = 0; i < count; i++)
        chars[i] = npl_string_char_at_internal(symbol_box(key), i);
    NplVal keyword = npl_string_from_chars_internal(chars, count, BOX_KEYWORD);
    free(chars);
    npl_gc_unroot(&key);
    *slot = keyword;
    g_keyword_count++;
    return keyword;
}

NplVal npl_keyword(const char *bytes, uint32_t length)
{
    npl_shared_lock();
    NplVal result = npl_nil();
    int ascii = 1;
    for (uint32_t i = 0; i < length; i++)
        if ((unsigned char)bytes[i] >= 0x80) { ascii = 0; break; }
    if (ascii && length <= 256) {
        uint64_t storage[(sizeof(NplString) + 257 + 7) / 8];
        NplString *probe = (NplString *)(void *)storage;
        probe->header.tag = BOX_STRING;
        probe->header.flags = 0;
        probe->length = length;
        probe->capacity = length;
        memcpy(probe->data, bytes, length);
        NplVal *slot = keyword_find_slot(probe);
        if (*slot != 0) result = *slot;
    }
    if (npl_is_nil(result)) {
        NplVal key = npl_string_from_utf8((const uint8_t *)bytes, length,
                                          BOX_STRING);
        result = intern_keyword_characters(key);
    }
    npl_shared_unlock();
    return result;
}

NplVal npl_string_to_keyword(NplVal string)
{
    if (!npl_is_ptr(string) || npl_header(string)->tag != BOX_STRING)
        npl_primitive_error("string->keyword", "expected a string", string);
    npl_shared_lock();
    NplVal result = intern_keyword_characters(string);
    npl_shared_unlock();
    return result;
}

NplVal npl_intern_static_keyword(NplVal keyword)
{
    npl_shared_lock();
    NplVal *slot = keyword_find_slot(symbol_box(keyword));
    NplVal result = *slot;
    if (result == 0) {
        *slot = keyword;
        g_keyword_count++;
        result = keyword;
    }
    npl_shared_unlock();
    return result;
}

void npl_keyword_mark_roots(void)
{
    for (size_t i = 0; i < g_keyword_capacity; i++)
        if (g_keywords[i] != 0) npl_gc_mark_candidate_internal(g_keywords[i]);
}

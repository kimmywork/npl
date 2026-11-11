/*
 * hashtable.c — SRFI 125 hash tables (.agents/archive/design/design-data.md section 3).
 *
 * Open addressing with linear probing; at most a quarter of the slots are
 * live after a resize and at most half are used (live or deleted) before
 * one. The collector does not move objects, so eq? and eqv? tables hash
 * addresses and never rehash after a collection.
 *
 * Kinds: eq, eqv, string (string=? over strings), equal (compared here
 * by a bounded walk; the library passes its equal? for keys too deep or
 * cyclic for it), and general (a user equivalence and hash procedure,
 * called through npl_apply_sync).
 */

#include "npl.h"
#include <stdint.h>
#include <string.h>

/* ---- hashing ---- */

static uint64_t mix(uint64_t x)
{
    x ^= x >> 33;
    x *= UINT64_C(0xff51afd7ed558ccd);
    x ^= x >> 33;
    x *= UINT64_C(0xc4ceb9fe1a85ec53);
    x ^= x >> 33;
    return x;
}

static uint64_t combine(uint64_t seed, uint64_t value)
{
    return mix(seed ^ (value + UINT64_C(0x9e3779b97f4a7c15) + (seed << 6)));
}

static uint64_t bytes_hash(const uint8_t *bytes, uint32_t length)
{
    uint64_t hash = UINT64_C(1469598103934665603);
    for (uint32_t i = 0; i < length; i++) {
        hash ^= bytes[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

/* Agrees with eqv? (runtime/numeric.c npl_eqv): numbers by value (exact
 * integers and fractions are canonical, flonums by their bits), pointer
 * boxes by address, everything else by identity. */
static uint64_t eqv_hash(NplVal value)
{
    if (!npl_is_ptr(value)) return mix(value);
    NplHeader *header = npl_header(value);
    switch (header->tag) {
    case BOX_FLOAT: {
        uint64_t bits;
        memcpy(&bits, &((NplFloat *)header)->value, sizeof bits);
        return mix(bits ^ UINT64_C(0x5bd1e995));
    }
    case BOX_BIGNUM: {
        NplBigint *big = (NplBigint *)header;
        uint64_t hash = mix((uint64_t)(int64_t)big->sign);
        for (uint32_t i = 0; i < big->count; i++)
            hash = combine(hash, big->limbs[i]);
        return hash;
    }
    case BOX_RATIONAL:
        return combine(eqv_hash(((NplRational *)header)->numerator),
                       eqv_hash(((NplRational *)header)->denominator));
    case BOX_COMPLEX:
        return combine(eqv_hash(((NplComplex *)header)->real),
                       eqv_hash(((NplComplex *)header)->imag) + 1);
    case BOX_POINTER:
        return mix((uint64_t)(uintptr_t)((NplPointer *)header)->address);
    default:
        return mix(value);
    }
}

static uint64_t string_hash(NplVal value)
{
    return npl_string_hash_internal((const NplString *)npl_header(value));
}

/* Agrees with equal? (lib/scheme/list.scm): structure over pairs, vectors,
 * and define-record-type instances, characters of strings, bytes of
 * bytevectors, eqv? elsewhere. Every structurally equal value visits the
 * same nodes in the same order, so a node budget keeps the result equal
 * while bounding the work and ending on cyclic data. */
static uint64_t equal_hash(NplVal value, int *budget)
{
    if (--*budget < 0) return 0;
    if (!npl_is_ptr(value)) return mix(value);
    NplHeader *header = npl_header(value);
    switch (header->tag) {
    case BOX_PAIR:
        return combine(equal_hash(npl_pair_ptr(value)->car, budget),
                       equal_hash(npl_pair_ptr(value)->cdr, budget));
    case BOX_STRING:
        return string_hash(value);
    case BOX_BYTEVECTOR:
        return bytes_hash(((NplBytevector *)header)->data,
                          ((NplBytevector *)header)->length);
    case BOX_VECTOR: {
        NplVector *vector = (NplVector *)header;
        uint64_t hash = mix(vector->length + 1);
        for (uint32_t i = 0; i < vector->length && *budget > 0; i++)
            hash = combine(hash, equal_hash(vector->data[i], budget));
        return hash;
    }
    case BOX_RECORD: {
        if (npl_record_instance_type(value) == npl_false())
            return eqv_hash(value);
        NplVector *record = (NplVector *)header;
        uint64_t hash = mix(record->data[0]);
        for (uint32_t i = 1; i < record->length && *budget > 0; i++)
            hash = combine(hash, equal_hash(record->data[i], budget));
        return hash;
    }
    default:
        return eqv_hash(value);
    }
}

#define EQUAL_HASH_BUDGET 64

static NplVal hash_fixnum(uint64_t hash)
{
    return npl_fixnum((int64_t)(hash >> 3));
}

/* (npl_hash_value kind value): 0 eq?, 1 eqv?, 2 equal?, 3 string; a
 * non-negative fixnum (SRFI 125 hash, string-hash, hash-by-identity). */
NplVal npl_hash_value(NplVal kind, NplVal value)
{
    int budget = EQUAL_HASH_BUDGET;
    switch (npl_is_fixnum(kind) ? npl_fixnum_value(kind) : -1) {
    case NPL_HASH_EQ: return hash_fixnum(mix(value));
    case NPL_HASH_EQV: return hash_fixnum(eqv_hash(value));
    case NPL_HASH_EQUAL: return hash_fixnum(equal_hash(value, &budget));
    case NPL_HASH_STRING:
        if (!npl_is_ptr(value) || npl_header(value)->tag != BOX_STRING)
            npl_primitive_error("string-hash", "expected a string", value);
        return hash_fixnum(string_hash(value));
    default:
        npl_primitive_error("hash", "unknown hash kind", kind);
        return npl_false();
    }
}
/* ---- tables ---- */

#define EMPTY_SLOT npl_false()
#define DELETED_SLOT npl_true()

static NplHashTable *require_table(NplVal value, const char *who)
{
    if (!npl_is_ptr(value) || npl_header(value)->tag != BOX_HASHTABLE)
        npl_primitive_error(who, "expected a hash table", value);
    return (NplHashTable *)npl_header(value);
}

static NplHashTable *require_mutable(NplVal value, const char *who)
{
    NplHashTable *table = require_table(value, who);
    if (table->header.flags & NPL_FLAG_LITERAL)
        npl_primitive_error(who, "cannot mutate an immutable hash table",
                            value);
    return table;
}

static NplVal *slots(NplHashTable *table)
{
    return ((NplVector *)npl_header(table->entries))->data;
}

/* Entries for capacity slots, all empty. */
static NplVal new_entries(uint32_t capacity)
{
    size_t length = (size_t)capacity * 3;
    NplVector *vector = (NplVector *)npl_alloc_object(
        sizeof(NplVector) + length * sizeof(NplVal));
    vector->header.tag = BOX_VECTOR;
    vector->header.flags = 0;
    vector->length = (uint32_t)length;
    vector->capacity = (uint32_t)length;
    for (size_t i = 0; i < length; i++) vector->data[i] = EMPTY_SLOT;
    return (NplVal)(uintptr_t)vector | TAG_PTR;
}

static int is_string(NplVal value)
{
    return npl_is_ptr(value) && npl_header(value)->tag == BOX_STRING;
}

/* A user hash procedure's result as a slot hash. */
static uint64_t user_hash(NplVal result)
{
    return npl_is_fixnum(result) ? mix((uint64_t)result) : eqv_hash(result);
}

static uint64_t key_hash(NplHashTable *table, NplVal key, const char *who)
{
    int budget = EQUAL_HASH_BUDGET;
    switch (table->kind) {
    case NPL_HASH_EQ: return mix(key);
    case NPL_HASH_EQV: return eqv_hash(key);
    case NPL_HASH_STRING:
        if (!is_string(key))
            npl_primitive_error(who, "expected a string key", key);
        return string_hash(key);
    case NPL_HASH_EQUAL: return equal_hash(key, &budget);
    default:
        if (npl_is_false(table->hash)) return equal_hash(key, &budget);
        return user_hash(npl_apply_sync(table->hash, 1, &key));
    }
}

/* equal? (lib/scheme/list.scm) over at most *budget nodes: 1 equal, 0 not,
 * -1 undecided (the budget ran out: deep or cyclic data). */
static int equal_bounded(NplVal a, NplVal b, int *budget)
{
    for (;;) {
        if (a == b) return 1;
        if (--*budget < 0) return -1;
        if (!npl_is_ptr(a) || !npl_is_ptr(b)) return npl_eqv(a, b) == npl_true();
        uint32_t tag = npl_header(a)->tag;
        if (tag != npl_header(b)->tag) return npl_eqv(a, b) == npl_true();
        switch (tag) {
        case BOX_PAIR: {
            int head = equal_bounded(npl_pair_ptr(a)->car,
                                     npl_pair_ptr(b)->car, budget);
            if (head != 1) return head;
            a = npl_pair_ptr(a)->cdr;
            b = npl_pair_ptr(b)->cdr;
            continue;
        }
        case BOX_STRING:
            return npl_string_same_chars_internal(
                (const NplString *)npl_header(a),
                (const NplString *)npl_header(b));
        case BOX_BYTEVECTOR: {
            NplBytevector *x = (NplBytevector *)npl_header(a);
            NplBytevector *y = (NplBytevector *)npl_header(b);
            return x->length == y->length &&
                   memcmp(x->data, y->data, x->length) == 0;
        }
        case BOX_RECORD:
            if (npl_record_instance_type(a) == npl_false() ||
                npl_record_instance_type(a) != npl_record_instance_type(b))
                return 0;
            /* fall through: fields compare as vector elements */
        case BOX_VECTOR: {
            NplVector *x = (NplVector *)npl_header(a);
            NplVector *y = (NplVector *)npl_header(b);
            if (x->length != y->length) return 0;
            for (uint32_t i = tag == BOX_RECORD ? 1 : 0; i < x->length; i++) {
                int element = equal_bounded(x->data[i], y->data[i], budget);
                if (element != 1) return element;
            }
            return 1;
        }
        default:
            return npl_eqv(a, b) == npl_true();
        }
    }
}

#define EQUAL_COMPARE_BUDGET 10000

/* Keys with equal hashes; an equivalence is reflexive, so eq? keys match.
 * Only general tables, and equal tables on data too deep or cyclic for
 * the bounded walk, call back into NPL. */
static int same_key(NplHashTable *table, NplVal stored, NplVal key)
{
    if (stored == key) return 1;
    switch (table->kind) {
    case NPL_HASH_EQ: return 0;
    case NPL_HASH_EQV: return npl_eqv(stored, key) == npl_true();
    case NPL_HASH_STRING:
        return npl_string_same_chars_internal(
            (const NplString *)npl_header(stored),
            (const NplString *)npl_header(key));
    case NPL_HASH_EQUAL: {
        int budget = EQUAL_COMPARE_BUDGET;
        int same = equal_bounded(stored, key, &budget);
        if (same >= 0) return same;
        if (npl_is_false(table->equivalence))
            npl_primitive_error("hash-table", "key too deep to compare", key);
    }
        /* fall through: the library's equal? handles cycles */
    default: {
        NplVal arguments[2] = { stored, key };
        return npl_apply_sync(table->equivalence, 2, arguments) !=
               npl_false();
    }
    }
}

/* The slot holding key, or -1. When insert is non-NULL it receives the slot
 * a new association for key goes to (the first deleted slot on the probe
 * path, else the empty slot ending it). An equivalence procedure that
 * mutates the table restarts the probe on the new entries. */
static int64_t find_slot(NplHashTable *table, NplVal key, uint64_t hash,
                         int64_t *insert)
{
    NplVal stored_hash = hash_fixnum(hash);
    uint32_t start = (uint32_t)npl_fixnum_value(stored_hash);
    for (;;) {
        NplVal entries = table->entries;
        uint32_t mask = table->capacity - 1;
        uint32_t index = start & mask;
        int64_t first_deleted = -1;
        int restarted = 0;
        for (;;) {
            NplVal *slot = slots(table) + (size_t)index * 3;
            if (slot[0] == EMPTY_SLOT) {
                if (insert) *insert = first_deleted >= 0 ? first_deleted : index;
                return -1;
            }
            if (slot[0] == DELETED_SLOT) {
                if (first_deleted < 0) first_deleted = index;
            } else if (slot[0] == stored_hash) {
                NplVal stored = slot[1];
                int same = same_key(table, stored, key);
                if (table->entries != entries) { restarted = 1; break; }
                if (same) return index;
            }
            index = (index + 1) & mask;
        }
        if (!restarted) return -1;
    }
}

/* Rehash into capacity slots, dropping deleted ones. */
static void resize(NplHashTable *table, uint32_t capacity)
{
    NplVal entries = new_entries(capacity);
    npl_gc_root(&entries);
    NplVal *from = slots(table);
    NplVal *to = ((NplVector *)npl_header(entries))->data;
    uint32_t mask = capacity - 1;
    for (uint32_t i = 0; i < table->capacity; i++) {
        NplVal *slot = from + (size_t)i * 3;
        if (slot[0] == EMPTY_SLOT || slot[0] == DELETED_SLOT) continue;
        uint32_t index = (uint32_t)npl_fixnum_value(slot[0]) & mask;
        while (to[(size_t)index * 3] != EMPTY_SLOT)
            index = (index + 1) & mask;
        NplVal *target = to + (size_t)index * 3;
        target[0] = slot[0];
        target[1] = slot[1];
        target[2] = slot[2];
    }
    table->entries = entries;
    table->capacity = capacity;
    table->used = table->count;
    table->cursor = 0;
    npl_gc_write_barrier(table, entries);
    npl_gc_unroot(&entries);
}

static uint32_t capacity_for(uint64_t count)
{
    uint32_t capacity = 8;
    while ((uint64_t)capacity < count * 4) capacity *= 2;
    return capacity;
}
/* ---- operations ---- */

/* (npl_make_hash_table kind equivalence hash capacity) */
NplVal npl_make_hash_table(NplVal kind, NplVal equivalence, NplVal hash,
                           NplVal capacity)
{
    if (!npl_is_fixnum(kind) || npl_fixnum_value(kind) < NPL_HASH_EQ ||
        npl_fixnum_value(kind) > NPL_HASH_GENERAL)
        npl_primitive_error("make-hash-table", "unknown table kind", kind);
    if (!npl_is_fixnum(capacity) || npl_fixnum_value(capacity) < 0 ||
        npl_fixnum_value(capacity) > (INT64_C(1) << 28))
        npl_primitive_error("make-hash-table", "invalid capacity", capacity);
    NplVal entries =
        new_entries(capacity_for((uint64_t)npl_fixnum_value(capacity)));
    npl_gc_root(&entries);
    NplHashTable *table =
        (NplHashTable *)npl_alloc_object(sizeof(NplHashTable));
    npl_gc_unroot(&entries);
    table->header.tag = BOX_HASHTABLE;
    table->header.flags = 0;
    table->kind = (uint32_t)npl_fixnum_value(kind);
    table->count = 0;
    table->used = 0;
    table->capacity = ((NplVector *)npl_header(entries))->length / 3;
    table->cursor = 0;
    table->reserved = 0;
    table->entries = entries;
    table->equivalence = equivalence;
    table->hash = hash;
    return (NplVal)(uintptr_t)table | TAG_PTR;
}

NplVal npl_hash_table_p(NplVal value)
{
    return npl_is_ptr(value) && npl_header(value)->tag == BOX_HASHTABLE
        ? npl_true() : npl_false();
}

/* The value for key, or missing when key has no association. */
NplVal npl_hash_table_ref(NplVal value, NplVal key, NplVal missing)
{
    NplHashTable *table = require_table(value, "hash-table-ref");
    int64_t slot = find_slot(table, key,
                             key_hash(table, key, "hash-table-ref"), NULL);
    return slot < 0 ? missing : slots(table)[(size_t)slot * 3 + 2];
}

NplVal npl_hash_table_set(NplVal value, NplVal key, NplVal element)
{
    NplHashTable *table = require_mutable(value, "hash-table-set!");
    uint64_t hash = key_hash(table, key, "hash-table-set!");
    int64_t insert = -1;
    int64_t slot = find_slot(table, key, hash, &insert);
    if (slot >= 0) {
        NplVal *entry = slots(table) + (size_t)slot * 3;
        entry[2] = element;
        npl_gc_write_barrier(npl_header(table->entries), element);
        return npl_unspecified();
    }
    if ((uint64_t)(table->used + 1) * 2 > table->capacity) {
        resize(table, capacity_for((uint64_t)table->count + 1));
        find_slot(table, key, hash, &insert);
    }
    NplVal *entry = slots(table) + (size_t)insert * 3;
    if (entry[0] == EMPTY_SLOT) table->used++;
    entry[0] = hash_fixnum(hash);
    entry[1] = key;
    entry[2] = element;
    NplHeader *holder = npl_header(table->entries);
    npl_gc_write_barrier(holder, key);
    npl_gc_write_barrier(holder, element);
    table->count++;
    return npl_unspecified();
}

static void delete_slot(NplHashTable *table, int64_t slot)
{
    NplVal *entry = slots(table) + (size_t)slot * 3;
    entry[0] = DELETED_SLOT;
    entry[1] = npl_false();
    entry[2] = npl_false();
    table->count--;
}

/* 1 when key had an association, else 0. */
NplVal npl_hash_table_delete(NplVal value, NplVal key)
{
    NplHashTable *table = require_mutable(value, "hash-table-delete!");
    int64_t slot = find_slot(table, key,
                             key_hash(table, key, "hash-table-delete!"), NULL);
    if (slot < 0) return npl_fixnum(0);
    delete_slot(table, slot);
    return npl_fixnum(1);
}

NplVal npl_hash_table_contains_p(NplVal value, NplVal key)
{
    NplHashTable *table = require_table(value, "hash-table-contains?");
    return find_slot(table, key,
                     key_hash(table, key, "hash-table-contains?"), NULL) >= 0
        ? npl_true() : npl_false();
}

NplVal npl_hash_table_size(NplVal value)
{
    return npl_fixnum(require_table(value, "hash-table-size")->count);
}

/* Keys (field 1) or values (field 2) of the live slots, in slot order. */
static NplVal collect(NplVal value, int field, const char *who)
{
    NplHashTable *table = require_table(value, who);
    NplVal result = npl_nil();
    npl_gc_root(&value);
    npl_gc_root(&result);
    for (uint32_t i = table->capacity; i-- > 0;) {
        NplVal *entry = slots(table) + (size_t)i * 3;
        if (entry[0] == EMPTY_SLOT || entry[0] == DELETED_SLOT) continue;
        result = npl_cons(entry[field], result);
    }
    npl_gc_unroot(&result);
    npl_gc_unroot(&value);
    return result;
}

NplVal npl_hash_table_keys(NplVal value)
{
    return collect(value, 1, "hash-table-keys");
}

NplVal npl_hash_table_values(NplVal value)
{
    return collect(value, 2, "hash-table-values");
}

/* #(key value key value ...) in slot order: a snapshot for the library's
 * walking procedures. */
NplVal npl_hash_table_items(NplVal value)
{
    NplHashTable *table = require_table(value, "hash-table-entries");
    npl_gc_root(&value);
    size_t length = (size_t)table->count * 2;
    NplVector *items = (NplVector *)npl_alloc_object(
        sizeof(NplVector) + length * sizeof(NplVal));
    npl_gc_unroot(&value);
    items->header.tag = BOX_VECTOR;
    items->header.flags = 0;
    items->length = (uint32_t)length;
    items->capacity = (uint32_t)length;
    size_t next = 0;
    for (uint32_t i = 0; i < table->capacity; i++) {
        NplVal *entry = slots(table) + (size_t)i * 3;
        if (entry[0] == EMPTY_SLOT || entry[0] == DELETED_SLOT) continue;
        items->data[next++] = entry[1];
        items->data[next++] = entry[2];
    }
    return (NplVal)(uintptr_t)items | TAG_PTR;
}

NplVal npl_hash_table_clear(NplVal value)
{
    NplHashTable *table = require_mutable(value, "hash-table-clear!");
    npl_gc_root(&value);
    NplVal entries = new_entries(8);
    npl_gc_unroot(&value);
    table->entries = entries;
    npl_gc_write_barrier(table, entries);
    table->capacity = 8;
    table->count = 0;
    table->used = 0;
    table->cursor = 0;
    return npl_unspecified();
}

/* A copy with the same kind and procedures; immutable unless mutable_p. */
NplVal npl_hash_table_copy(NplVal value, NplVal mutable_p)
{
    NplHashTable *source = require_table(value, "hash-table-copy");
    npl_gc_root(&value);
    NplVal entries = new_entries(source->capacity);
    npl_gc_root(&entries);
    NplHashTable *table =
        (NplHashTable *)npl_alloc_object(sizeof(NplHashTable));
    npl_gc_unroot(&entries);
    npl_gc_unroot(&value);
    memcpy(((NplVector *)npl_header(entries))->data, slots(source),
           (size_t)source->capacity * 3 * sizeof(NplVal));
    *table = *source;
    table->header.flags = npl_is_false(mutable_p) ? NPL_FLAG_LITERAL : 0;
    table->entries = entries;
    return (NplVal)(uintptr_t)table | TAG_PTR;
}

/* (key . value) of an association, which is removed. The scan resumes
 * where the previous pop stopped, so emptying a table is linear. */
NplVal npl_hash_table_pop(NplVal value)
{
    NplHashTable *table = require_mutable(value, "hash-table-pop!");
    if (table->count == 0)
        npl_primitive_error("hash-table-pop!", "empty hash table", value);
    uint32_t index = table->cursor & (table->capacity - 1);
    for (;; index = (index + 1) & (table->capacity - 1)) {
        NplVal *entry = slots(table) + (size_t)index * 3;
        if (entry[0] != EMPTY_SLOT && entry[0] != DELETED_SLOT) break;
    }
    NplVal *entry = slots(table) + (size_t)index * 3;
    NplVal key = entry[1];
    NplVal element = entry[2];
    delete_slot(table, index);
    table->cursor = index + 1;
    return npl_cons(key, element);
}

/* (npl_hash_table_info table which): 0 kind, 1 equivalence procedure, 2 hash
 * procedure, 3 mutable?. */
NplVal npl_hash_table_info(NplVal value, NplVal which)
{
    NplHashTable *table = require_table(value, "hash-table-info");
    switch (npl_is_fixnum(which) ? npl_fixnum_value(which) : -1) {
    case 0: return npl_fixnum(table->kind);
    case 1: return table->equivalence;
    case 2: return table->hash;
    case 3:
        return table->header.flags & NPL_FLAG_LITERAL ? npl_false()
                                                       : npl_true();
    default:
        npl_primitive_error("hash-table-info", "unknown field", which);
        return npl_false();
    }
}

/* (npl_same_procedure_p a b): eq?, or closures over the same code that
 * capture nothing. A primitive used as a value is a fresh closure over one
 * wrapper at each use, so make-hash-table recognizes eq?, equal?, string=?,
 * and the other standard predicates this way. */
NplVal npl_same_procedure_p(NplVal a, NplVal b)
{
    if (a == b) return npl_true();
    if (!npl_is_ptr(a) || !npl_is_ptr(b) ||
        npl_header(a)->tag != BOX_CLOSURE || npl_header(b)->tag != BOX_CLOSURE)
        return npl_false();
    NplClosure *left = (NplClosure *)npl_header(a);
    NplClosure *right = (NplClosure *)npl_header(b);
    return left->function == right->function && left->capture_count == 0 &&
           right->capture_count == 0 && left->environment == right->environment
        ? npl_true() : npl_false();
}

#ifndef NPL_RUNTIME_H
#define NPL_RUNTIME_H

#include <stdint.h>
#include <stddef.h>

/*
 * NPL Runtime — Value representation and core types.
 *
 * Tagged pointer scheme (64-bit):
 *   - Fixnum: value << 2 | 0  (inline, no allocation)
 *   - Pointer: ptr | 1        (pointer to boxed NplVal)
 *   - Immediate: enc | 2      (nil, bool, char)
 *
 * Boxed values (NplVal) have an 8-byte header:
 *   - tag:    value type (TAG_PAIR, TAG_STRING, TAG_VECTOR, etc.)
 *   - flags:  GC mark bit, etc.
 *   - payload follows inline
 *
 * Memory layout:
 *   [tag:32][flags:32][payload...]
 */

/* ---- Tagged pointer type ---- */
typedef uintptr_t NplVal;

/* ---- Tag bits (low 2 bits) ---- */
#define TAG_FIXNUM  0   /* fixnum: value in upper 62 bits */
#define TAG_PTR     1   /* pointer to boxed NplVal */
#define TAG_IMM     2   /* immediate (nil, bool, char) */

/* ---- Boxed value tags ---- */
#define BOX_PAIR    0
#define BOX_STRING  1
#define BOX_VECTOR  2
#define BOX_SYMBOL  3
#define BOX_CLOSURE 4
#define BOX_FLOAT   5

/* ---- Immediate values ---- */
#define IMM_NIL     0
#define IMM_TRUE    1
#define IMM_FALSE   2

/* ---- Boxed value header ---- */
typedef struct {
    uint32_t tag;      /* BOX_PAIR, BOX_STRING, etc. */
    uint32_t flags;    /* bit 0 = GC mark */
} NplHeader;

/* ---- Pair ---- */
typedef struct {
    NplHeader header;
    NplVal car;
    NplVal cdr;
} NplPair;

/* ---- String ---- */
typedef struct {
    NplHeader header;
    uint32_t length;
    uint32_t capacity;
    char data[];  /* flexible array member */
} NplString;

/* ---- Symbol — same layout as string (tag BOX_SYMBOL) ---- */
typedef NplString NplSymbolLike;

/* ---- Vector ---- */
typedef struct {
    NplHeader header;
    uint32_t length;
    uint32_t capacity;
    NplVal data[];  /* flexible array member */
} NplVector;

/* ---- Float ---- */
typedef struct {
    NplHeader header;
    double value;
} NplFloat;

/* ---- Constructor/destructor macros ---- */

static inline NplVal npl_fixnum(int64_t v) {
    return (NplVal)((uint64_t)v << 2) | TAG_FIXNUM;
}

static inline int64_t npl_fixnum_value(NplVal v) {
    return (int64_t)((uint64_t)v >> 2);
}

static inline NplVal npl_nil(void) {
    return (NplVal)(IMM_NIL << 2) | TAG_IMM;
}

static inline NplVal npl_true(void) {
    return (NplVal)(IMM_TRUE << 2) | TAG_IMM;
}

static inline NplVal npl_false(void) {
    return (NplVal)(IMM_FALSE << 2) | TAG_IMM;
}

static inline int npl_is_fixnum(NplVal v) {
    return (v & 3) == TAG_FIXNUM;
}

static inline int npl_is_ptr(NplVal v) {
    return (v & 3) == TAG_PTR;
}

static inline int npl_is_nil(NplVal v) {
    return v == npl_nil();
}

static inline int npl_is_true(NplVal v) {
    return v == npl_true();
}

static inline int npl_is_false(NplVal v) {
    return v == npl_false();
}

static inline NplHeader *npl_header(NplVal v) {
    return (NplHeader *)(v & ~(NplVal)3);
}

static inline NplPair *npl_pair_ptr(NplVal v) {
    return (NplPair *)(v & ~(NplVal)3);
}

static inline uint32_t npl_box_tag(NplVal v) {
    return npl_header(v)->tag;
}

/* ---- Runtime API (implemented in alloc.c, gc.c) ---- */

/* Initialize the runtime (call once at startup) */
void npl_runtime_init(void);

/* Allocate a pair (cons cell) */
NplVal npl_cons(NplVal car, NplVal cdr);

/* Allocate a string */
NplVal npl_string(const char *s, uint32_t len);

/* Allocate a float */
NplVal npl_float(double v);

/* Get car/cdr of a pair */
NplVal npl_car(NplVal v);
NplVal npl_cdr(NplVal v);

/* Set car/cdr of a pair */
void npl_set_car(NplVal pair, NplVal val);
void npl_set_cdr(NplVal pair, NplVal val);

/* Garbage collection */
void npl_gc_collect(void);

/* Register a GC root (global variable, stack slot, etc.) */
void npl_gc_root(NplVal *root);

/* Unregister a GC root */
void npl_gc_unroot(NplVal *root);

/* Get memory stats */
size_t npl_gc_allocated(void);
size_t npl_gc_threshold(void);

/* Display a value (for debugging) */
void npl_display(NplVal v);

/* Print a single newline */
void npl_newline(void);

/* Convert fixnum to string (for printing) */
char *npl_fixnum_to_string(int64_t v);

#endif /* NPL_RUNTIME_H */

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
typedef NplVal (*NplGenericFunction)(NplVal, int64_t, NplVal *);

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
#define BOX_BYTEVECTOR 6
#define BOX_VALUES  7
#define BOX_WIND_FRAME 8
#define BOX_WIND_TRANSITION 9

#define NPL_FLAG_LITERAL 1
#define IMM_CHAR_TAG 14

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

typedef struct {
    NplHeader header;
    uint32_t length;
    uint32_t capacity;
    uint8_t data[];
} NplBytevector;

typedef struct {
    NplHeader header;
    uint32_t count;
    uint32_t capacity;
    NplVal data[];
} NplValues;

typedef struct {
    NplHeader header;
    NplVal parent;
    NplVal before;
    NplVal after;
    uint64_t id;
} NplWindFrame;

typedef struct {
    NplHeader header;
    uint32_t exit_count;
    uint32_t exit_index;
    uint32_t entry_count;
    uint32_t entry_index;
    NplVal target;
    NplVal continuation;
    NplVal value;
    NplVal entering;
    NplVal frames[];
} NplWindTransition;

/* ---- Closure ---- */
typedef struct {
    NplHeader header;
    NplGenericFunction function;
    NplVal environment;
    int64_t arity;
} NplClosure;

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
    return ((int64_t)v) >> 2;
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

static inline int npl_is_char(NplVal v) {
    return (v & 0xff) == IMM_CHAR_TAG;
}

static inline NplVal npl_char(uint32_t codepoint) {
    return ((NplVal)codepoint << 8) | IMM_CHAR_TAG;
}

static inline uint32_t npl_char_value(NplVal v) {
    return (uint32_t)(v >> 8);
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

NplVal npl_make_vector(int64_t argc, NplVal *argv);
NplVal npl_vector_length(NplVal vector);
NplVal npl_vector_ref(NplVal vector, NplVal index);
NplVal npl_vector_set(NplVal vector, NplVal index, NplVal value);
NplVal npl_vector_p(NplVal value);
NplVal npl_list_to_vector(NplVal list);
NplVal npl_make_bytevector(int64_t argc, NplVal *argv);
NplVal npl_bytevector_length(NplVal vector);
NplVal npl_bytevector_ref(NplVal vector, NplVal index);
NplVal npl_bytevector_set(NplVal vector, NplVal index, NplVal value);
NplVal npl_bytevector_p(NplVal value);
NplVal npl_char_p(NplVal value);
NplVal npl_char_to_integer(NplVal value);
NplVal npl_integer_to_char(NplVal value);
NplVal npl_append(NplVal left, NplVal right);
NplVal npl_make_values(int64_t argc, NplVal *argv);
NplVal npl_list_to_values(NplVal list);
NplVal npl_expect_one(NplVal value);
void npl_values_dispatch(NplVal consumer, NplVal continuation, NplVal value);
NplVal npl_current_wind(void);
NplVal npl_wind_push(NplVal before, NplVal after);
NplVal npl_wind_pop(NplVal frame);
void npl_wind_transfer(NplVal target, NplVal continuation, NplVal value);

/* Get car/cdr of a pair */
NplVal npl_car(NplVal v);
NplVal npl_cdr(NplVal v);

/* Set car/cdr of a pair */
void npl_set_car(NplVal pair, NplVal val);
void npl_set_cdr(NplVal pair, NplVal val);

/* Generic NPL call ABI used by closures and the tail trampoline. */
NplVal npl_make_environment(int64_t argc, NplVal *argv);
NplVal npl_environment_ref(NplVal environment, int64_t index);
NplVal npl_rest_arguments(int64_t argc, NplVal *argv, int64_t fixed,
                          int64_t trailing);
NplVal npl_make_closure(NplGenericFunction function, NplVal environment,
                        int64_t arity);
int32_t npl_guard_integer(NplVal value);
int32_t npl_guard_bool(NplVal value);
double npl_guard_float(NplVal value);
NplVal npl_guard_pair(NplVal value);
NplVal npl_guard_closure(NplVal value);
NplVal npl_box_integer(int32_t value);
NplVal npl_box_bool(int32_t value);
NplVal npl_box_float(double value);
NplVal npl_box_identity(NplVal value);
NplVal npl_apply(NplVal closure, int64_t argc, NplVal *argv);
void npl_tail_request_closure(NplVal closure, int64_t argc, NplVal *argv);
NplVal npl_call_cc(NplVal procedure);
NplVal npl_trampoline(NplGenericFunction function, NplVal environment,
                      int64_t argc, NplVal *argv);
void npl_tail_request(NplGenericFunction function, NplVal environment,
                      int64_t argc, NplVal *argv);

/* Garbage collection */
void npl_gc_collect(void);
void npl_gc_maybe_collect(void);

/* Register a GC root (global variable, stack slot, etc.) */
void npl_gc_root(NplVal *root);

/* Unregister a GC root */
void npl_gc_unroot(NplVal *root);

/* Get memory stats */
size_t npl_gc_allocated(void);
size_t npl_gc_threshold(void);
size_t npl_gc_live_objects(void);
size_t npl_gc_collections(void);

/* Display a value and return the unspecified value. */
NplVal npl_display(NplVal v);

/* Print a single newline and return the unspecified value. */
NplVal npl_newline(void);

/* ---- String & symbol builtins ---- */
NplVal npl_string_append(NplVal a, NplVal b);
int64_t npl_string_length(NplVal s);
NplVal npl_string_ref(NplVal s, NplVal i);   /* returns fixnum byte value */
int npl_string_equal(NplVal a, NplVal b);    /* raw 0/1 */
NplVal npl_number_to_string(NplVal v);
NplVal npl_symbol_to_string(NplVal s);
/* NOTE: no string->symbol in stage0 — v1 does not maintain a global
 * symbol intern table spanning static quotes and runtime boxes. */
NplVal npl_substring(NplVal s, NplVal i, NplVal j); /* bytes [i,j) as new box */
NplVal npl_int_to_string_char(NplVal code);         /* 1-byte string */

/* ---- bootstrap I/O & control ---- */
void npl_store_args(int argc, char **argv);   /* called from emitted $main */
NplVal npl_program_arguments(void);           /* list of arg strings, argv[1..] */
NplVal npl_read_file(NplVal path);            /* whole file as string; aborts on error */
void npl_exit(NplVal code);                   /* untagged fixnum, low byte */

/* bootstrap lexer hooks — all take TAGGED values */
NplVal npl_lex_class_w(NplVal cT, NplVal wantT);     /* PROPER bool */
NplVal npl_lex_scan_sym_end(NplVal sT, NplVal jT);   /* tagged index */
NplVal npl_tagged_num_to_string(NplVal v);           /* decimal of tagged bits */
NplVal npl_lex_number(NplVal sT, NplVal iT, NplVal jT);
NplVal npl_lex_tokens(NplVal source);

/* Convert fixnum to string (for printing) */
char *npl_fixnum_to_string(int64_t v);

#endif /* NPL_RUNTIME_H */

/* debug hooks (bootstrap diagnosis) */
void npl_tracev(unsigned long v);
void npl_trace2(unsigned long tag, unsigned long v);

#ifndef NPL_RUNTIME_H
#define NPL_RUNTIME_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

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
#define BOX_HANDLER_FRAME 10
#define BOX_RAISE_CONTEXT 11
#define BOX_PARAMETER 12
#define BOX_PARAMETER_BINDING 13
#define BOX_PARAMETER_CONTEXT 14
#define BOX_PROMISE 15
#define BOX_PROMISE_CONTEXT 16
#define BOX_PORT 17
#define BOX_EOF 18
#define BOX_BIGNUM 19
#define BOX_RATIONAL 20
#define BOX_COMPLEX 21
#define BOX_RECORD 22   /* NplVector layout: data[0] = type, data[1..] = fields */
#define BOX_STRING_DATA 23  /* out-of-line wide string characters (string.c) */
#define BOX_POINTER 24  /* C address (ffi.c); holds no references */

#define NPL_FLAG_LITERAL 1
/* Strings (string.c): 32-bit characters; characters in an out-of-line wide
 * buffer whose pointer is the first data word. */
#define NPL_FLAG_WIDE 2
#define NPL_FLAG_OUT_OF_LINE 4
/* Generations (alloc.c): an object that survived a collection is old; an
 * old object in the remembered set may point to young objects. Static
 * (data-segment) objects never carry these bits. */
#define NPL_FLAG_OLD 8
#define NPL_FLAG_REMEMBERED 16
#define IMM_CHAR_TAG 14

/* ---- Immediate values ---- */
#define IMM_NIL     0
#define IMM_TRUE    1
#define IMM_FALSE   2
/* Result of procedures whose value R7RS leaves unspecified (vector-set!,
 * display, one-armed if, set!, ...). 3 is unusable: (3 << 2) | TAG_IMM
 * equals IMM_CHAR_TAG, the encoding of #\x0. */
#define IMM_UNSPECIFIED 4

/* ---- Boxed value header ---- */
typedef struct {
    uint32_t tag;      /* BOX_PAIR, BOX_STRING, etc. */
    uint32_t flags;    /* NPL_FLAG_* bits */
} NplHeader;

/* ---- Pair ---- */
typedef struct {
    NplHeader header;
    NplVal car;
    NplVal cdr;
} NplPair;

/* ---- String ----
 * Layout is private to string.c (narrow bytes or wide code points); length
 * counts characters of the inline data. Use the string.c accessors. */
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
    NplVal target_handler;
    NplVal continuation;
    NplVal value;
    NplVal entering;
    NplVal frames[];
} NplWindTransition;

typedef struct {
    NplHeader header;
    NplVal parent;
    NplVal procedure;
} NplHandlerFrame;

typedef struct {
    NplHeader header;
    NplVal frame;
    NplVal continuation;
    NplVal value;
    uint32_t continuable;
    uint32_t reserved;
} NplRaiseContext;

typedef struct {
    NplHeader header;
    NplVal converter;
    NplVal value;
} NplParameter;

typedef struct {
    NplHeader header;
    NplVal parent;
    NplVal parameter;
    NplVal value;
} NplParameterBinding;

typedef struct {
    NplHeader header;
    NplVal parameter;
    NplVal continuation;
    uint32_t update;
    uint32_t reserved;
} NplParameterContext;

typedef struct {
    NplHeader header;
    uint32_t state;
    uint32_t reserved;
    NplVal value;
} NplPromise;

typedef struct {
    NplHeader header;
    NplVal root;
    NplVal current;
    NplVal continuation;
} NplPromiseContext;

typedef struct {
    NplHeader header;
    FILE *stream;
    NplVal buffer;
    uint32_t mode;
    uint32_t position;
    uint32_t length;
    uint32_t pushed;        /* bytes in pushback (stream ports) */
    uint8_t pushback[8];    /* read-ahead bytes, consumed first (io.c): at
                               most one character of lookahead plus the
                               tail of one invalid UTF-8 sequence */
} NplPort;

typedef struct {
    NplHeader header;
} NplEof;

/* A compiled lambda's captured values are stored in the closure itself
 * (npl_make_flat_closure); its environment is then the closure, so compiled
 * code reads capture i at a fixed offset (byte 48 + 8i). Runtime and FFI
 * closures have no captures and an arbitrary environment value. direct:
 * the lambda's direct entry (docs/design-calls.md C3), which takes the
 * closure's arguments without the continuation and returns the value;
 * NULL for every other closure. The collector does not visit it. */
typedef struct {
    NplHeader header;
    NplGenericFunction function;
    NplVal environment;
    int64_t arity;
    int64_t capture_count;
    NplGenericFunction direct;
    NplVal captures[];
} NplClosure;

/* ---- Float ---- */
typedef struct {
    NplHeader header;
    double value;
} NplFloat;

typedef struct {
    NplHeader header;
    NplVal real;
    NplVal imag;
} NplComplex;

typedef struct {
    NplHeader header;
    int32_t sign;
    uint32_t count;
    uint32_t capacity;
    uint32_t limbs[];
} NplBigint;

/* A C address from a foreign call. The collector never follows or frees
 * it; whatever it points to is released by the C library's own calls. */
typedef struct {
    NplHeader header;
    void *address;
} NplPointer;

/* Reduced exact fraction; denominator > 1 and numerator is nonzero. */
typedef struct {
    NplHeader header;
    NplVal numerator;
    NplVal denominator;
} NplRational;

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

static inline NplVal npl_unspecified(void) {
    return (NplVal)(IMM_UNSPECIFIED << 2) | TAG_IMM;
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

void npl_gc_remember_internal(NplHeader *object);
void npl_gc_write_barrier_value(NplVal holder, NplVal value);
/* Every NplVal field of a heap object, for marking and verification. */
typedef void (*NplChildVisitor)(NplVal value);
void npl_gc_visit_children_internal(NplHeader *header, NplChildVisitor visit);

/* Write barrier: call after storing value into a field of an existing heap
 * object. Stores into an object the same function just allocated, before
 * any further allocation, need none. */
static inline void npl_gc_write_barrier(void *object, NplVal value) {
    NplHeader *holder = (NplHeader *)object;
    if ((holder->flags & (NPL_FLAG_OLD | NPL_FLAG_REMEMBERED)) == NPL_FLAG_OLD &&
        npl_is_ptr(value) && !(npl_header(value)->flags & NPL_FLAG_OLD))
        npl_gc_remember_internal(holder);
}

static inline NplPair *npl_pair_ptr(NplVal v) {
    return (NplPair *)(v & ~(NplVal)3);
}

static inline uint32_t npl_box_tag(NplVal v) {
    return npl_header(v)->tag;
}

NplVal npl_current_input_port(void);
NplVal npl_current_output_port(void);
NplVal npl_current_error_port(void);
NplVal npl_open_input_file(NplVal path);
NplVal npl_open_output_file(NplVal path);
NplVal npl_open_binary_input_file(NplVal path);
NplVal npl_open_binary_output_file(NplVal path);
NplVal npl_open_input_bytevector(NplVal bytevector);
NplVal npl_open_output_bytevector(void);
NplVal npl_get_output_bytevector(NplVal port);
NplVal npl_open_input_string(NplVal string);
NplVal npl_open_output_string(void);
NplVal npl_get_output_string(NplVal port);
NplVal npl_close_port(NplVal port);
NplVal npl_close_input_port(NplVal port);
NplVal npl_close_output_port(NplVal port);
NplVal npl_flush_output_port(NplVal port);
NplVal npl_read_char(NplVal port);
NplVal npl_peek_char(NplVal port);
NplVal npl_write_char(NplVal character, NplVal port);
NplVal npl_read_byte(NplVal port);
NplVal npl_peek_byte(NplVal port);
NplVal npl_write_byte(NplVal byte, NplVal port);
NplVal npl_read_bytevector(int64_t argc, NplVal *argv);
NplVal npl_read_bytevector_into(int64_t argc, NplVal *argv);
NplVal npl_write_bytevector(int64_t argc, NplVal *argv);
NplVal npl_eof_object(void);
NplVal npl_eof_object_p(NplVal value);
NplVal npl_input_port_p(NplVal value);
NplVal npl_output_port_p(NplVal value);
NplVal npl_port_p(NplVal value);
NplVal npl_input_port_open_p(NplVal value);
NplVal npl_output_port_open_p(NplVal value);
NplVal npl_textual_port_p(NplVal value);
NplVal npl_binary_port_p(NplVal value);
NplVal npl_display_port(NplVal value, NplVal port);
NplVal npl_write_port(NplVal value, NplVal port);
NplVal npl_write_shared_port(NplVal value, NplVal port);
NplVal npl_write_simple_port(NplVal value, NplVal port);
NplVal npl_newline_port(NplVal port);
NplVal npl_read_datum(NplVal port);
NplVal npl_read_result(NplVal port);


/* Initialize the runtime (call once at startup) */
void npl_runtime_init(void);

/* Allocate a pair (cons cell) */
NplVal npl_cons(NplVal car, NplVal cdr);

/* Allocate a string */
NplVal npl_string(const char *s, uint32_t len);

/* Allocate a symbol. */
NplVal npl_symbol(const char *s, uint32_t len);

/* Allocate a float. */
NplVal npl_float(double v);
NplVal npl_inexact_p(NplVal value);
/* IEEE binary64 payload as an exact unsigned decimal integer for static data. */
NplVal npl_inexact_bits(NplVal value);
NplVal npl_inexact_parse(NplVal value);
NplVal npl_inexact_string(NplVal value);
NplVal npl_inexact_op(NplVal operation, NplVal value);
NplVal npl_inexact_binary(NplVal operation, NplVal left, NplVal right);
NplVal npl_finite_p(NplVal value);
NplVal npl_nan_p(NplVal value);
NplVal npl_infinite_p(NplVal value);
NplVal npl_inexact_eqv(NplVal left, NplVal right);
int npl_is_inexact(NplVal value);
double npl_real_double(NplVal value);
NplVal npl_real_exact(NplVal value);
NplVal npl_inexact_arithmetic(NplVal left, NplVal right, char operation);
int32_t npl_inexact_compare(NplVal left, NplVal right);
int npl_is_complex(NplVal value);
NplVal npl_complex_arithmetic(NplVal left, NplVal right, char operation);


NplVal npl_make_vector(int64_t argc, NplVal *argv);
NplVal npl_make_record(int64_t argc, NplVal *argv);
NplVal npl_record_p(NplVal value, NplVal type);
NplVal npl_record_ref(NplVal value, NplVal type, NplVal index);
NplVal npl_record_set(NplVal value, NplVal type, NplVal index, NplVal field);
NplVal npl_record_type(NplVal name, NplVal fields);
int npl_record_type_p_internal(NplVal value);
NplVal npl_record_instance_type(NplVal value);
NplVal npl_record_length(NplVal value);
NplVal npl_record_update(int64_t argc, NplVal *argv);
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
NplVal npl_wind_exit_step(void);
NplVal npl_current_handler(void);
NplVal npl_handler_push(NplVal procedure);
NplVal npl_handler_pop(NplVal frame);
NplVal npl_make_parameter(NplVal converter, NplVal value);
NplVal npl_parameter_ref(NplVal parameter);
NplVal npl_parameter_set(NplVal parameter, NplVal value);
NplVal npl_parameter_converter(NplVal parameter);
NplVal npl_parameters_enter(NplVal parameters, NplVal values, NplVal frames);
NplVal npl_parameters_leave(NplVal frames);
NplVal npl_promise_op(int64_t argc, NplVal *argv);
void npl_force(NplVal value, NplVal continuation);
void npl_raise(NplVal value, NplVal continuation, int64_t continuable);
void npl_wind_transfer(NplVal target, NplVal target_handler,
                       NplVal continuation, NplVal value);

/* Get car/cdr of a pair */
NplVal npl_car(NplVal v);
NplVal npl_cdr(NplVal v);

/* Set car/cdr of a pair */
void npl_set_car(NplVal pair, NplVal val);
void npl_set_cdr(NplVal pair, NplVal val);

/* Generic NPL call ABI used by closures and the tail trampoline. */
NplVal npl_rest_arguments(int64_t argc, NplVal *argv, int64_t fixed,
                          int64_t trailing);
NplVal npl_make_closure(NplGenericFunction function, NplVal environment,
                        int64_t arity);
NplVal npl_make_flat_closure(NplGenericFunction function, int64_t arity,
                             int64_t count, NplVal *captures);
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
void npl_wrong_argument_count(int64_t argc);
void npl_apply_tail(int64_t argc, NplVal *argv);
void npl_tail_request_closure(NplVal closure, int64_t argc, NplVal *argv);
NplVal npl_call_cc(NplVal procedure);
NplVal npl_trampoline(NplGenericFunction function, NplVal environment,
                      int64_t argc, NplVal *argv);
void npl_tail_request(NplGenericFunction function, NplVal environment,
                      int64_t argc, NplVal *argv);
/* Value-position call of a registered function; argc excludes the
 * continuation, which the call supplies (trampoline.c). */
NplVal npl_call_sync(NplGenericFunction function, NplVal environment,
                     int64_t argc, NplVal *argv);
/* Direct entries of compiled functions run their CPS entry through
 * npl_direct_fallback below this stack address (stack.c). */
extern uintptr_t npl_stack_limit;
void npl_stack_init_internal(uintptr_t base);
/* An exact integer from base-10^9 limbs, least significant first; a
 * fixnum when it fits (numeric.c; number_read.c builds integers with it). */
NplVal npl_box_integer_limbs_internal(int32_t sign, const uint32_t *limbs,
                                      uint32_t count);
/* The direct entry of a closure called with argc arguments (the
 * continuation included), or 0 (value.c). */
NplVal npl_closure_direct(NplVal value, int64_t argc);
/* Mark an evaluator constant immutable, deeply (value.c). */
NplVal npl_freeze_literal(NplVal value);
NplVal npl_direct_fallback(NplGenericFunction function, NplVal environment,
                           int64_t argc, NplVal *argv);
/* The same for a variadic function: argv holds the fixed arguments and then
 * the rest list, which is spread for the CPS entry. */
NplVal npl_direct_fallback_rest(NplGenericFunction function,
                                NplVal environment, int64_t fixed,
                                NplVal *argv);
NplVal npl_raise_direct(NplVal object);
/* Always 0. Compiled direct entries with a self jump combine each parameter
 * with it, so QBE does not keep the loop variables in the argument
 * registers (backend/program.npl emit-direct-function). */
extern uintptr_t npl_opaque_zero;

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
size_t npl_gc_minor_collections(void);
/* Old objects holding a young pointer outside the remembered set (a missed
 * write barrier); NPL_GC_VERIFY=1 checks this before every collection. */
size_t npl_gc_verify_barriers(int report);

/* Display a value and return the unspecified value. */
NplVal npl_display(NplVal v);

/* Print a single newline and return the unspecified value. */
NplVal npl_newline(void);

/* ---- Exact integer substrate (decimal conversion; no public tower policy) ---- */
NplVal npl_integer_from_string(NplVal v);
NplVal npl_integer_string_p(NplVal v);
/* Exact integer/rational conversion; invalid syntax returns #f. */
NplVal npl_number_from_string(NplVal v);
NplVal npl_number_from_string_radix(NplVal v, NplVal radix);
/* Canonical rectangular constructor: exact-zero imaginary parts demote. */
NplVal npl_complex_make(NplVal real, NplVal imag);
NplVal npl_complex_p(NplVal value);
NplVal npl_complex_real(NplVal value);
NplVal npl_complex_imag(NplVal value);
NplVal npl_exact_to_inexact(NplVal v);
NplVal npl_inexact_to_exact(NplVal v);
NplVal npl_integer_p(NplVal v);
NplVal npl_string_p(NplVal v);
NplVal npl_number_to_string(NplVal v);
NplVal npl_number_to_string_radix(NplVal v, NplVal radix);
NplVal npl_number_add(NplVal left, NplVal right);
NplVal npl_number_sub(NplVal left, NplVal right);
NplVal npl_number_mul(NplVal left, NplVal right);
NplVal npl_number_div(NplVal left, NplVal right);
NplVal npl_rational_make(NplVal numerator, NplVal denominator);
NplVal npl_rational_numerator(NplVal value);
NplVal npl_rational_denominator(NplVal value);
NplVal npl_number_quotient(NplVal left, NplVal right);
NplVal npl_number_remainder(NplVal left, NplVal right);
NplVal npl_number_gcd(NplVal left, NplVal right);
int32_t npl_number_compare(NplVal left, NplVal right);
NplVal npl_number_zero_p(NplVal value);
NplVal npl_number_p(NplVal v);
NplVal npl_eqv(NplVal left, NplVal right);

/* ---- String & symbol builtins ---- */
NplVal npl_string_append(NplVal a, NplVal b);
NplVal npl_string_append_n(int64_t argc, NplVal *argv);
int64_t npl_string_length(NplVal s);
NplVal npl_string_ref(NplVal s, NplVal i);   /* fixnum byte (stage0, string-byte-ref) */
NplVal npl_string_char_ref(NplVal s, NplVal i); /* R7RS string-ref: a char */
NplVal npl_make_string(NplVal count, NplVal fill);
NplVal npl_string_set(NplVal s, NplVal index, NplVal character);
NplVal npl_string_fill_range(NplVal s, NplVal character, NplVal start,
                             NplVal end);
int npl_string_equal(NplVal a, NplVal b);    /* raw 0/1 */

NplVal npl_symbol_to_string(NplVal s);
NplVal npl_symbol_predicate(NplVal s);
NplVal npl_procedure_p(NplVal value);
/* Symbols are interned (runtime/symbol.c): equal names are eq?. */
NplVal npl_string_to_symbol(NplVal string);
NplVal npl_intern_static_symbol(NplVal symbol);
NplVal npl_substring(NplVal s, NplVal i, NplVal j); /* bytes [i,j) as new box */
NplVal npl_int_to_string_char(NplVal code);         /* 1-byte string */

/* ---- bootstrap I/O & control ---- */
void npl_store_args(int argc, char **argv);   /* called from emitted $main */
NplVal npl_program_arguments(void);           /* list of arg strings, argv[1..] */
NplVal npl_command_line(void);                /* R7RS: command name + arguments */
NplVal npl_set_command_line(NplVal arguments);
NplVal npl_read_file(NplVal path);            /* whole file as string; aborts on error */
NplVal npl_process_run(NplVal arguments, NplVal output_path);
NplVal npl_get_environment_variable(NplVal name);
NplVal npl_get_environment_variables(void);
NplVal npl_file_exists(NplVal path);
NplVal npl_delete_file(NplVal path);
NplVal npl_make_temporary_directory(void);
NplVal npl_delete_directory(NplVal path);
NplVal npl_executable_path(void);
NplVal npl_try_open_file(NplVal path, NplVal kind); /* port or #f */
NplVal npl_port_parameter(NplVal kind); /* current-*-port parameter */
NplVal npl_port_ready_p(NplVal port);     /* char-ready? (a whole character) */
NplVal npl_byte_ready_p(NplVal port);     /* u8-ready? */
NplVal npl_jiffies_per_second(void);
NplVal npl_current_jiffy(void);
NplVal npl_current_second(void);            /* absolute path or #f */
void npl_exit(NplVal code);                   /* R7RS exit status mapping */

/* bootstrap lexer hooks — all take TAGGED values */
NplVal npl_lex_class_w(NplVal cT, NplVal wantT);     /* PROPER bool */
NplVal npl_lex_scan_sym_end(NplVal sT, NplVal jT);   /* tagged index */
NplVal npl_tagged_num_to_string(NplVal v);           /* decimal of tagged bits */
NplVal npl_lex_number(NplVal sT, NplVal iT, NplVal jT);

/* Convert fixnum to string (for printing) */
char *npl_fixnum_to_string(int64_t v);

/* Internal module interfaces. */
void *npl_alloc_object(size_t size);
void npl_gc_collect_minor_internal(void);
void npl_runtime_oom_internal(void);
void npl_gc_mark_candidate_internal(NplVal value);
uintptr_t npl_gc_swap_stack_bottom(uintptr_t stack_bottom);
uintptr_t npl_gc_stack_bottom_internal(void);
NplVal npl_box_bytes_internal(const char *bytes, uint32_t len,
                              uint32_t box_tag);
const NplString *npl_require_bytes_internal(NplVal value, const char *who);
/* Strings (string.c). */
NplVal npl_string_from_chars_internal(const uint32_t *chars, uint32_t length,
                                      uint32_t tag);
NplVal npl_string_from_utf8(const uint8_t *bytes, size_t length, uint32_t tag);
const char *npl_string_utf8(const NplString *string, size_t *length,
                            int *owned);
uint32_t npl_string_char_count_internal(const NplString *string);
uint32_t npl_string_char_at_internal(const NplString *string, uint32_t index);
void npl_string_store_internal(NplString *string, uint32_t index,
                               uint32_t code);
uint64_t npl_string_hash_internal(const NplString *string);
int npl_string_same_chars_internal(const NplString *a, const NplString *b);
NplVal npl_string_owned_buffer_internal(const NplString *string);
int npl_string_scalar_value_internal(uint32_t code);
uint32_t npl_utf8_decode_internal(const uint8_t *bytes, size_t length,
                                  size_t *index);
int npl_utf8_sequence_length_internal(uint8_t lead);
size_t npl_utf8_encode_internal(uint32_t code, uint8_t *out);
NplVal npl_string_to_utf8(NplVal string, NplVal start, NplVal end);
/* (scheme char) table lookups (unicode.c). */
NplVal npl_char_property(NplVal character, NplVal kind);
NplVal npl_char_digit_value(NplVal character);
NplVal npl_char_case(NplVal character, NplVal kind);
NplVal npl_char_full_case(NplVal character, NplVal kind);
NplVal npl_string_full_case(NplVal string, NplVal kind);
NplVal npl_symbol_fold_internal(NplVal symbol);
NplVal npl_utf8_to_string(NplVal bytevector, NplVal start, NplVal end);
void npl_control_init(void);
/* Parameter object application in CPS (control.c, trampoline.c). */
void npl_parameter_dispatch(NplVal parameter, int64_t argc, NplVal *argv);
void npl_control_mark_roots(void);
int npl_gc_root_count_internal(void);
void npl_gc_restore_root_count_internal(int count);
/* Catchable primitive errors (error.c, trampoline.c). */
int npl_raise_from_primitive(NplVal object);
/* C pointers (ffi.c, docs/ffi.md): boxed addresses compared by address. */
NplVal npl_pointer(void *address);
int npl_is_pointer(NplVal value);
void *npl_pointer_address(NplVal value, const char *who);
NplVal npl_pointer_p(NplVal value);
NplVal npl_pointer_null_p(NplVal value);
NplVal npl_null_pointer(void);
NplVal npl_pointer_to_integer(NplVal value);
NplVal npl_ffi_load_library(NplVal path);
NplVal npl_ffi_procedure(NplVal symbol, NplVal count);

/* C type conversions for stubs generated by nplc --emit=c-stubs
 * (define-c-function; src/compiler/backend/c-stubs.npl writes the calls).
 * who names the NPL procedure in error objects; ctype names the C type. */
int64_t npl_ffi_to_int64(NplVal value, int64_t minimum, int64_t maximum,
                         const char *ctype, const char *who);
uint64_t npl_ffi_to_uint64(NplVal value, uint64_t maximum, const char *ctype,
                           const char *who);
double npl_ffi_to_double(NplVal value, const char *who);
int npl_ffi_to_bool(NplVal value);
void *npl_ffi_to_pointer(NplVal value, const char *who);
void npl_ffi_check_string(NplVal value, const char *who);
char *npl_ffi_copy_string(NplVal value);
void npl_ffi_free(void *memory);
NplVal npl_ffi_from_int64(int64_t value);
NplVal npl_ffi_from_uint64(uint64_t value);
NplVal npl_ffi_from_double(double value);
NplVal npl_ffi_from_bool(int value);
NplVal npl_ffi_from_string(const char *text);
NplVal npl_ffi_from_pointer(void *address);
NplVal npl_ffi_unspecified(void);
NplVal npl_ffi_values(int64_t count, NplVal *values);

void npl_primitive_error(const char *who, const char *message,
                         NplVal irritant);
void npl_report_error_object(NplVal object);
void npl_primitive_file_error(const char *who, const char *message,
                              NplVal irritant);
void npl_primitive_read_error(const char *message);
void npl_symbol_mark_roots(void);
void npl_io_init(void);

#endif /* NPL_RUNTIME_H */

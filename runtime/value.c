/* Runtime value representation and collection operations. */

#include "npl.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- Value constructors ---- */

NplVal npl_cons(NplVal car, NplVal cdr) {
    NplPair *p = (NplPair *)npl_alloc_object(sizeof(NplPair));
    p->header.flags = 0;
    p->car = car;
    p->cdr = cdr;
    return (NplVal)(uintptr_t)p | TAG_PTR;
}

NplVal npl_string_p(NplVal value)
{
    return npl_is_ptr(value) && npl_header(value)->tag == BOX_STRING
        ? npl_true() : npl_false();
}


NplVal npl_float(double v) {
    NplFloat *f = (NplFloat *)npl_alloc_object(sizeof(NplFloat));
    f->header.tag = BOX_FLOAT;
    f->header.flags = 0;
    f->value = v;
    return (NplVal)(uintptr_t)f | TAG_PTR;
}

static NplVector *require_vector(NplVal value, const char *who)
{
    if (!npl_is_ptr(value) || npl_header(value)->tag != BOX_VECTOR)
        npl_primitive_error(who, "expected a vector", value);
    return (NplVector *)npl_header(value);
}

/* (record type field ...): a record instance; data[0] holds the type. */
NplVal npl_make_record(int64_t argc, NplVal *argv)
{
    if (argc < 1) {
        fprintf(stderr, "record: needs a type\n");
        exit(1);
    }
    NplVal record = npl_make_vector(argc, argv);
    npl_header(record)->tag = BOX_RECORD;
    return record;
}

static NplVector *require_record(NplVal value, NplVal type, const char *who)
{
    if (!npl_is_ptr(value) || npl_header(value)->tag != BOX_RECORD ||
        ((NplVector *)npl_header(value))->data[0] != type) {
        npl_primitive_error(who, "wrong record type", value);
    }
    return (NplVector *)npl_header(value);
}

NplVal npl_record_p(NplVal value, NplVal type)
{
    return npl_is_ptr(value) && npl_header(value)->tag == BOX_RECORD &&
           ((NplVector *)npl_header(value))->data[0] == type
        ? npl_true() : npl_false();
}

/* Field indexes are 1-based (slot 0 is the type) and validated by the
 * define-record-type expansion, so only the type is checked here. */
NplVal npl_record_ref(NplVal value, NplVal type, NplVal index)
{
    NplVector *record = require_record(value, type, "record accessor");
    return record->data[npl_fixnum_value(index)];
}

NplVal npl_record_set(NplVal value, NplVal type, NplVal index, NplVal field)
{
    NplVector *record = require_record(value, type, "record modifier");
    record->data[npl_fixnum_value(index)] = field;
    return npl_nil();
}

NplVal npl_make_vector(int64_t argc, NplVal *argv)
{
    if (argc < 0 || (uint64_t)argc > UINT32_MAX) {
        fprintf(stderr, "vector: unsupported length\n");
        exit(1);
    }
    NplVector *vector = (NplVector *)npl_alloc_object(
        sizeof(NplVector) + (size_t)argc * sizeof(NplVal));
    vector->header.tag = BOX_VECTOR;
    vector->header.flags = 0;
    vector->length = (uint32_t)argc;
    vector->capacity = (uint32_t)argc;
    for (int64_t i = 0; i < argc; i++) vector->data[i] = argv[i];
    return (NplVal)(uintptr_t)vector | TAG_PTR;
}

NplVal npl_vector_length(NplVal value)
{
    return npl_fixnum(require_vector(value, "vector-length")->length);
}

NplVal npl_vector_ref(NplVal value, NplVal index)
{
    NplVector *vector = require_vector(value, "vector-ref");
    if (!npl_is_fixnum(index))
        npl_primitive_error("vector-ref", "expected an exact integer index",
                            index);
    int64_t i = npl_fixnum_value(index);
    if (i < 0 || (uint64_t)i >= vector->length)
        npl_primitive_error("vector-ref", "index out of range", index);
    return vector->data[i];
}

NplVal npl_vector_set(NplVal value, NplVal index, NplVal element)
{
    NplVector *vector = require_vector(value, "vector-set!");
    if (vector->header.flags & NPL_FLAG_LITERAL)
        npl_primitive_error("vector-set!", "cannot mutate a literal", value);
    if (!npl_is_fixnum(index))
        npl_primitive_error("vector-set!", "expected an exact integer index",
                            index);
    int64_t i = npl_fixnum_value(index);
    if (i < 0 || (uint64_t)i >= vector->length)
        npl_primitive_error("vector-set!", "index out of range", index);
    vector->data[i] = element;
    return npl_nil();
}

NplVal npl_vector_p(NplVal value)
{
    return (npl_is_ptr(value) && npl_header(value)->tag == BOX_VECTOR)
        ? npl_true() : npl_false();
}

NplVal npl_list_to_vector(NplVal list)
{
    int64_t length = 0;
    for (NplVal rest = list; !npl_is_nil(rest); rest = npl_cdr(rest)) {
        if (!npl_is_ptr(rest) || npl_header(rest)->tag != BOX_PAIR)
            npl_primitive_error("list->vector", "expected a proper list",
                                list);
        if ((uint64_t)length == UINT32_MAX) {
            fprintf(stderr, "list->vector: vector is too long\n");
            exit(1);
        }
        length++;
    }

    npl_gc_root(&list);
    NplVector *vector = (NplVector *)npl_alloc_object(
        sizeof(NplVector) + (size_t)length * sizeof(NplVal));
    vector->header.tag = BOX_VECTOR;
    vector->header.flags = 0;
    vector->length = (uint32_t)length;
    vector->capacity = (uint32_t)length;
    NplVal rest = list;
    for (int64_t i = 0; i < length; i++) {
        vector->data[i] = npl_car(rest);
        rest = npl_cdr(rest);
    }
    npl_gc_unroot(&list);
    return (NplVal)(uintptr_t)vector | TAG_PTR;
}

static NplBytevector *require_bytevector(NplVal value, const char *who)
{
    if (!npl_is_ptr(value) || npl_header(value)->tag != BOX_BYTEVECTOR)
        npl_primitive_error(who, "expected a bytevector", value);
    return (NplBytevector *)npl_header(value);
}

NplVal npl_make_bytevector(int64_t argc, NplVal *argv)
{
    if (argc < 0 || (uint64_t)argc > UINT32_MAX) {
        fprintf(stderr, "bytevector: unsupported length\n");
        exit(1);
    }
    NplBytevector *vector = (NplBytevector *)npl_alloc_object(
        sizeof(NplBytevector) + (size_t)argc);
    vector->header.tag = BOX_BYTEVECTOR;
    vector->header.flags = 0;
    vector->length = (uint32_t)argc;
    vector->capacity = (uint32_t)argc;
    /* On a bad byte the partly filled bytevector is unreachable garbage. */
    for (int64_t i = 0; i < argc; i++) {
        int64_t byte = npl_fixnum_value(argv[i]);
        if (!npl_is_fixnum(argv[i]) || byte < 0 || byte > 255)
            npl_primitive_error("bytevector", "expected a byte", argv[i]);
        vector->data[i] = (uint8_t)byte;
    }
    return (NplVal)(uintptr_t)vector | TAG_PTR;
}

NplVal npl_bytevector_length(NplVal value)
{
    return npl_fixnum(require_bytevector(value, "bytevector-length")->length);
}

NplVal npl_bytevector_ref(NplVal value, NplVal index)
{
    NplBytevector *vector = require_bytevector(value, "bytevector-u8-ref");
    if (!npl_is_fixnum(index))
        npl_primitive_error("bytevector-u8-ref",
                            "expected an exact integer index", index);
    int64_t i = npl_fixnum_value(index);
    if (i < 0 || (uint64_t)i >= vector->length)
        npl_primitive_error("bytevector-u8-ref", "index out of range", index);
    return npl_fixnum(vector->data[i]);
}

NplVal npl_bytevector_set(NplVal value, NplVal index, NplVal element)
{
    NplBytevector *vector = require_bytevector(value, "bytevector-u8-set!");
    if (vector->header.flags & NPL_FLAG_LITERAL)
        npl_primitive_error("bytevector-u8-set!", "cannot mutate a literal",
                            value);
    if (!npl_is_fixnum(index))
        npl_primitive_error("bytevector-u8-set!",
                            "expected an exact integer index", index);
    int64_t i = npl_fixnum_value(index);
    if (i < 0 || (uint64_t)i >= vector->length)
        npl_primitive_error("bytevector-u8-set!", "index out of range", index);
    int64_t byte = npl_fixnum_value(element);
    if (!npl_is_fixnum(element) || byte < 0 || byte > 255)
        npl_primitive_error("bytevector-u8-set!", "expected a byte", element);
    vector->data[i] = (uint8_t)byte;
    return npl_nil();
}

NplVal npl_bytevector_p(NplVal value)
{
    return (npl_is_ptr(value) && npl_header(value)->tag == BOX_BYTEVECTOR)
        ? npl_true() : npl_false();
}

NplVal npl_char_p(NplVal value)
{
    return npl_is_char(value) ? npl_true() : npl_false();
}

NplVal npl_char_to_integer(NplVal value)
{
    if (!npl_is_char(value))
        npl_primitive_error("char->integer", "expected a character", value);
    return npl_fixnum(npl_char_value(value));
}

NplVal npl_integer_to_char(NplVal value)
{
    if (!npl_is_fixnum(value))
        npl_primitive_error("integer->char", "expected an exact integer",
                            value);
    int64_t code = npl_fixnum_value(value);
    if (code < 0 || code > 0x10FFFF ||
        !npl_string_scalar_value_internal((uint32_t)code))
        npl_primitive_error("integer->char", "expected a Unicode scalar value",
                            value);
    return npl_char((uint32_t)code);
}

NplVal npl_append(NplVal left, NplVal right)
{
    if (npl_is_nil(left)) return right;
    if (!npl_is_ptr(left) || npl_header(left)->tag != BOX_PAIR)
        npl_primitive_error("append", "expected a list", left);

    NplVal head = npl_nil();
    NplVal tail = npl_nil();
    NplVal rest = left;
    npl_gc_root(&head);
    npl_gc_root(&tail);
    npl_gc_root(&rest);
    npl_gc_root(&right);
    while (!npl_is_nil(rest)) {
        if (!npl_is_ptr(rest) || npl_header(rest)->tag != BOX_PAIR)
            npl_primitive_error("append", "expected a proper list", left);
        NplVal item = npl_cons(npl_car(rest), npl_nil());
        if (npl_is_nil(head)) head = item;
        else npl_pair_ptr(tail)->cdr = item;
        tail = item;
        rest = npl_cdr(rest);
    }
    npl_pair_ptr(tail)->cdr = right;
    npl_gc_unroot(&right);
    npl_gc_unroot(&rest);
    npl_gc_unroot(&tail);
    npl_gc_unroot(&head);
    return head;
}

NplVal npl_make_environment(int64_t argc, NplVal *argv)
{
    if (argc < 0 || (uint64_t)argc > UINT32_MAX) {
        fprintf(stderr, "npl_make_environment: unsupported size %lld\n",
                (long long)argc);
        exit(1);
    }
    NplVector *environment = (NplVector *)npl_alloc_object(
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

NplVal npl_rest_arguments(int64_t argc, NplVal *argv, int64_t fixed,
                          int64_t trailing)
{
    if (argc < 0 || fixed < 0 || trailing < 0 || fixed + trailing > argc) {
        fprintf(stderr, "procedure: invalid rest argument range\n");
        exit(1);
    }
    NplVal rest = npl_nil();
    npl_gc_root(&rest);
    for (int64_t i = argc - trailing; i > fixed; i--)
        rest = npl_cons(argv[i - 1], rest);
    npl_gc_unroot(&rest);
    return rest;
}

NplVal npl_make_closure(NplGenericFunction function, NplVal environment,
                        int64_t arity)
{
    NplClosure *closure = (NplClosure *)npl_alloc_object(sizeof(NplClosure));
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

double npl_guard_float(NplVal value)
{
    if (!npl_is_ptr(value) || npl_header(value)->tag != BOX_FLOAT) {
        fprintf(stderr, "type guard: expected Float\n");
        exit(1);
    }
    return ((NplFloat *)npl_header(value))->value;
}

NplVal npl_guard_pair(NplVal value)
{
    if (!npl_is_ptr(value) || npl_header(value)->tag != BOX_PAIR) {
        fprintf(stderr, "type guard: expected Pair\n");
        exit(1);
    }
    return value;
}

NplVal npl_guard_closure(NplVal value)
{
    if (!npl_is_ptr(value) || npl_header(value)->tag != BOX_CLOSURE) {
        fprintf(stderr, "type guard: expected function\n");
        exit(1);
    }
    return value;
}

NplVal npl_box_integer(int32_t value) { return npl_fixnum(value); }
NplVal npl_box_bool(int32_t value) { return value ? npl_true() : npl_false(); }
NplVal npl_box_float(double value) { return npl_float(value); }
NplVal npl_box_identity(NplVal value) { return value; }

/* ---- Value accessors ---- */

NplVal npl_car(NplVal v) {
    if (!npl_is_ptr(v) || npl_pair_ptr(v)->header.tag != BOX_PAIR)
        npl_primitive_error("car", "expected a pair", v);
    return npl_pair_ptr(v)->car;
}

NplVal npl_cdr(NplVal v) {
    if (!npl_is_ptr(v) || npl_pair_ptr(v)->header.tag != BOX_PAIR)
        npl_primitive_error("cdr", "expected a pair", v);
    return npl_pair_ptr(v)->cdr;
}

void npl_set_car(NplVal pair, NplVal val) {
    if (!npl_is_ptr(pair)) return;
    NplPair *p = npl_pair_ptr(pair);
    if (p->header.tag != BOX_PAIR) return;
    if (p->header.flags & NPL_FLAG_LITERAL)
        npl_primitive_error("set-car!", "cannot mutate a literal", pair);
    p->car = val;
}

void npl_set_cdr(NplVal pair, NplVal val) {
    if (!npl_is_ptr(pair)) return;
    NplPair *p = npl_pair_ptr(pair);
    if (p->header.tag != BOX_PAIR) return;
    if (p->header.flags & NPL_FLAG_LITERAL)
        npl_primitive_error("set-cdr!", "cannot mutate a literal", pair);
    p->cdr = val;
}

/* ---- String & symbol builtins (increment-2) ---- */

/* Characters of a string as a malloc'd array (strings may be narrow or
 * wide; see string.c). */
static uint32_t *string_chars(const NplString *string, uint32_t *count)
{
    uint32_t n = npl_string_char_count_internal(string);
    uint32_t *chars = (uint32_t *)malloc(((size_t)n + 1) * sizeof(uint32_t));
    if (!chars) npl_runtime_oom_internal();
    for (uint32_t i = 0; i < n; i++)
        chars[i] = npl_string_char_at_internal(string, i);
    *count = n;
    return chars;
}

NplVal npl_string_append(NplVal a, NplVal b)
{
    NplVal parts[2] = { a, b };
    return npl_string_append_n(2, parts);
}

/* R7RS string-append over any number of strings, including none. */
NplVal npl_string_append_n(int64_t argc, NplVal *argv)
{
    size_t total = 0;
    for (int64_t i = 0; i < argc; i++)
        total += npl_string_char_count_internal(
            npl_require_bytes_internal(argv[i], "string-append"));
    if (total > UINT32_MAX) npl_runtime_oom_internal();
    uint32_t *chars = (uint32_t *)malloc((total + 1) * sizeof(uint32_t));
    if (!chars) npl_runtime_oom_internal();
    size_t offset = 0;
    for (int64_t i = 0; i < argc; i++) {
        const NplString *part = (const NplString *)npl_header(argv[i]);
        uint32_t n = npl_string_char_count_internal(part);
        for (uint32_t k = 0; k < n; k++)
            chars[offset++] = npl_string_char_at_internal(part, k);
    }
    NplVal out = npl_string_from_chars_internal(chars, (uint32_t)total,
                                                BOX_STRING);
    free(chars);
    return out;
}

/* Character count; the compiler's string-length (seed byte uses are on
 * ASCII text, where characters and bytes agree). */
int64_t npl_string_length(NplVal s)
{
    return (int64_t)npl_string_char_count_internal(
        npl_require_bytes_internal(s, "string-length"));
}

/* Compiler-internal string-byte-ref: the character code at index i as a
 * fixnum (the compiler only indexes ASCII source text). */
NplVal npl_string_ref(NplVal s, NplVal i)
{
    const NplString *string = npl_require_bytes_internal(s, "string-ref");
    if (!npl_is_fixnum(i) || npl_fixnum_value(i) < 0 ||
        npl_fixnum_value(i) >= (int64_t)npl_string_char_count_internal(string))
        npl_primitive_error("string-ref", "index out of range", i);
    return npl_fixnum(npl_string_char_at_internal(
        string, (uint32_t)npl_fixnum_value(i)));
}

static NplString *mutable_string(NplVal value, const char *who)
{
    if (!npl_is_ptr(value) || npl_header(value)->tag != BOX_STRING)
        npl_primitive_error(who, "expected a string", value);
    NplString *string = (NplString *)npl_header(value);
    if (string->header.flags & NPL_FLAG_LITERAL)
        npl_primitive_error(who, "cannot mutate a literal", value);
    return string;
}

static uint32_t string_character(NplVal character, const char *who)
{
    if (!npl_is_char(character))
        npl_primitive_error(who, "expected a character", character);
    return npl_char_value(character);
}

/* make-string k [char]: a fresh mutable string; the default fill is a space. */
NplVal npl_make_string(NplVal count, NplVal fill)
{
    if (!npl_is_fixnum(count) || npl_fixnum_value(count) < 0 ||
        npl_fixnum_value(count) > (int64_t)UINT32_MAX)
        npl_primitive_error("make-string", "expected a non-negative count",
                            count);
    uint32_t code = string_character(fill, "make-string");
    uint32_t length = (uint32_t)npl_fixnum_value(count);
    if (code <= 0xFF) {
        NplVal out = npl_box_bytes_internal(NULL, length, BOX_STRING);
        memset(((NplString *)npl_header(out))->data, (int)code, length);
        return out;
    }
    uint32_t *chars = (uint32_t *)malloc(((size_t)length + 1) *
                                         sizeof(uint32_t));
    if (!chars) npl_runtime_oom_internal();
    for (uint32_t i = 0; i < length; i++) chars[i] = code;
    NplVal out = npl_string_from_chars_internal(chars, length, BOX_STRING);
    free(chars);
    return out;
}

/* Fill [start, end) of a mutable string; string-set! is the one-index case. */
NplVal npl_string_fill_range(NplVal value, NplVal character, NplVal start,
                             NplVal end)
{
    NplString *string = mutable_string(value, "string-fill!");
    uint32_t code = string_character(character, "string-fill!");
    uint32_t length = npl_string_char_count_internal(string);
    if (!npl_is_fixnum(start) || !npl_is_fixnum(end) ||
        npl_fixnum_value(start) < 0 ||
        npl_fixnum_value(start) > npl_fixnum_value(end) ||
        npl_fixnum_value(end) > (int64_t)length)
        npl_primitive_error("string-fill!", "index out of range",
                            npl_cons(start, npl_cons(end, npl_nil())));
    for (int64_t i = npl_fixnum_value(start); i < npl_fixnum_value(end); i++)
        npl_string_store_internal(string, (uint32_t)i, code);
    return npl_nil();
}

NplVal npl_string_set(NplVal value, NplVal index, NplVal character)
{
    NplString *string = mutable_string(value, "string-set!");
    uint32_t code = string_character(character, "string-set!");
    if (!npl_is_fixnum(index) || npl_fixnum_value(index) < 0 ||
        npl_fixnum_value(index) >=
            (int64_t)npl_string_char_count_internal(string))
        npl_primitive_error("string-set!", "index out of range", index);
    npl_string_store_internal(string, (uint32_t)npl_fixnum_value(index), code);
    return npl_nil();
}

/* R7RS string-ref: the character at index k. */
NplVal npl_string_char_ref(NplVal s, NplVal i)
{
    const NplString *string = npl_require_bytes_internal(s, "string-ref");
    if (!npl_is_fixnum(i) || npl_fixnum_value(i) < 0 ||
        npl_fixnum_value(i) >= (int64_t)npl_string_char_count_internal(string))
        npl_primitive_error("string-ref", "index out of range", i);
    return npl_char(npl_string_char_at_internal(
        string, (uint32_t)npl_fixnum_value(i)));
}

int npl_string_equal(NplVal a, NplVal b)
{
    return npl_string_same_chars_internal(
        npl_require_bytes_internal(a, "string=?"),
        npl_require_bytes_internal(b, "string=?"));
}

/* Copy characters into a box with a different header tag (symbol<->string). */
static NplVal retag_box(NplVal v, uint32_t new_tag)
{
    uint32_t count;
    uint32_t *chars = string_chars((const NplString *)npl_header(v), &count);
    NplVal out = npl_string_from_chars_internal(chars, count, new_tag);
    free(chars);
    return out;
}

NplVal npl_symbol_to_string(NplVal s) {
    npl_require_bytes_internal(s, "symbol->string");
    return retag_box(s, BOX_STRING);
}

/* Closures (including continuations) and parameter objects are procedures;
 * promises are callable internally by force but are not procedures. */
NplVal npl_procedure_p(NplVal value)
{
    if (!npl_is_ptr(value)) return npl_false();
    uint32_t tag = npl_header(value)->tag;
    return tag == BOX_CLOSURE || tag == BOX_PARAMETER
        ? npl_true() : npl_false();
}

NplVal npl_symbol_predicate(NplVal value)
{
    return npl_is_ptr(value) && npl_header(value)->tag == BOX_SYMBOL
        ? npl_true() : npl_false();
}

NplVal npl_substring(NplVal s, NplVal i, NplVal j)
{
    const NplString *string = npl_require_bytes_internal(s, "substring");
    int64_t a = npl_fixnum_value(i);
    int64_t b = npl_fixnum_value(j);
    if (!npl_is_fixnum(i) || !npl_is_fixnum(j) || a < 0 || b < a ||
        b > (int64_t)npl_string_char_count_internal(string))
        npl_primitive_error("substring", "index out of range",
                            npl_cons(i, npl_cons(j, npl_nil())));
    uint32_t length = (uint32_t)(b - a);
    uint32_t *chars = (uint32_t *)malloc(((size_t)length + 1) *
                                         sizeof(uint32_t));
    if (!chars) npl_runtime_oom_internal();
    for (uint32_t k = 0; k < length; k++)
        chars[k] = npl_string_char_at_internal(string, (uint32_t)a + k);
    NplVal out = npl_string_from_chars_internal(chars, length,
                                                string->header.tag);
    free(chars);
    return out;
}

/* Compiler-internal int->string: a one-character string of code point v. */
NplVal npl_int_to_string_char(NplVal code)
{
    int64_t v = npl_fixnum_value(code);
    if (!npl_is_fixnum(code) || v < 0 || v > 0x10FFFF ||
        (v >= 0xD800 && v <= 0xDFFF))
        npl_primitive_error("int->string", "expected a scalar value", code);
    uint32_t one = (uint32_t)v;
    return npl_string_from_chars_internal(&one, 1, BOX_STRING);
}

/* ---- Helper: fixnum to string ---- */

char *npl_fixnum_to_string(int64_t v) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%lld", (long long)v);
    return strdup(buf);
}

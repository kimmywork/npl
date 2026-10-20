/* Strings and symbols (docs/design-l6.md, Part U).
 *
 * A string holds characters (Unicode scalar values), not encoded bytes. Its
 * storage has one of two widths:
 *   narrow: one byte per character, for code points 0..255 (Latin-1, which
 *           includes ASCII); data[] holds the bytes plus a trailing NUL.
 *   wide:   one 32-bit code point per character (NPL_FLAG_WIDE).
 * Indexing is O(1) at either width. Storing a character above 255 into a
 * narrow mutable string promotes it: the characters move to a separately
 * allocated wide buffer (NPL_FLAG_OUT_OF_LINE, pointer in the first data
 * word) so that the string keeps its identity. Literals and symbols are
 * immutable and never promoted.
 *
 * C boundaries (paths, environment, ports, the compiler's lexer) see text as
 * UTF-8: npl_string_utf8 encodes, npl_string_from_utf8 decodes. Narrow ASCII
 * strings are their own UTF-8, so the common case needs no copy. This module
 * owns the layout; other modules use the accessors in npl.h. */

#include "npl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A managed object (tag BOX_STRING_DATA, holds no references). */
typedef struct {
    NplHeader header;
    uint32_t length;     /* characters */
    uint32_t capacity;   /* characters */
    uint32_t data[];
} NplWideBuffer;

static int is_wide(const NplString *string)
{
    return (string->header.flags & NPL_FLAG_WIDE) != 0;
}

static NplWideBuffer *out_of_line(const NplString *string)
{
    NplWideBuffer *buffer;
    memcpy(&buffer, string->data, sizeof(buffer));
    return buffer;
}

static uint32_t *wide_data(const NplString *string)
{
    if (string->header.flags & NPL_FLAG_OUT_OF_LINE)
        return out_of_line(string)->data;
    return (uint32_t *)(void *)string->data;
}

static int scalar_value(uint32_t code)
{
    return code <= 0x10FFFF && (code < 0xD800 || code > 0xDFFF);
}

static NplString *allocate(uint32_t length, int wide, uint32_t tag)
{
    size_t unit = wide ? sizeof(uint32_t) : 1;
    size_t size = sizeof(NplString) + (size_t)length * unit + unit;
    NplString *string = (NplString *)npl_alloc_object(size);
    string->header.tag = tag;
    string->header.flags = wide ? NPL_FLAG_WIDE : 0;
    string->length = length;
    string->capacity = length;
    if (wide) ((uint32_t *)(void *)string->data)[length] = 0;
    else string->data[length] = '\0';
    return string;
}

static NplVal boxed(NplString *string)
{
    return (NplVal)(uintptr_t)string | TAG_PTR;
}

/* ---- construction ---- */

NplVal npl_box_bytes_internal(const char *bytes, uint32_t len,
                              uint32_t box_tag)
{
    NplString *string = allocate(len, 0, box_tag);
    if (len && bytes) memcpy(string->data, bytes, len);
    return boxed(string);
}

NplVal npl_string(const char *s, uint32_t len)
{
    return npl_box_bytes_internal(s, len, BOX_STRING);
}

/* A fresh string (or symbol) of the given characters; narrow when every
 * character fits in a byte. */
NplVal npl_string_from_chars_internal(const uint32_t *chars, uint32_t length,
                                      uint32_t tag)
{
    int wide = 0;
    for (uint32_t i = 0; i < length; i++)
        if (chars[i] > 0xFF) { wide = 1; break; }
    NplString *string = allocate(length, wide, tag);
    if (wide) {
        memcpy(string->data, chars, (size_t)length * sizeof(uint32_t));
    } else {
        for (uint32_t i = 0; i < length; i++)
            string->data[i] = (char)(unsigned char)chars[i];
    }
    return boxed(string);
}

/* ---- UTF-8 ---- */

/* Decode one UTF-8 sequence at bytes[*index]; malformed input yields U+FFFD
 * and consumes one byte (as Guile and Racket do). */
uint32_t npl_utf8_decode_internal(const uint8_t *bytes, size_t length,
                                  size_t *index)
{
    size_t i = *index;
    uint8_t lead = bytes[i];
    uint32_t code;
    size_t count;
    if (lead < 0x80) { *index = i + 1; return lead; }
    if (lead >= 0xC2 && lead <= 0xDF) { code = lead & 0x1F; count = 1; }
    else if (lead >= 0xE0 && lead <= 0xEF) { code = lead & 0x0F; count = 2; }
    else if (lead >= 0xF0 && lead <= 0xF4) { code = lead & 0x07; count = 3; }
    else { *index = i + 1; return 0xFFFD; }
    for (size_t k = 1; k <= count; k++) {
        if (i + k >= length || (bytes[i + k] & 0xC0) != 0x80) {
            *index = i + 1;
            return 0xFFFD;
        }
        code = (code << 6) | (bytes[i + k] & 0x3F);
    }
    /* Reject overlong forms, surrogates, and values above U+10FFFF. */
    if ((count == 2 && code < 0x800) || (count == 3 && code < 0x10000) ||
        !scalar_value(code)) {
        *index = i + 1;
        return 0xFFFD;
    }
    *index = i + count + 1;
    return code;
}

/* Number of bytes the UTF-8 encoding of a sequence starting with lead needs
 * (1 for invalid leads, which decode to U+FFFD). */
int npl_utf8_sequence_length_internal(uint8_t lead)
{
    if (lead >= 0xC2 && lead <= 0xDF) return 2;
    if (lead >= 0xE0 && lead <= 0xEF) return 3;
    if (lead >= 0xF0 && lead <= 0xF4) return 4;
    return 1;
}

size_t npl_utf8_encode_internal(uint32_t code, uint8_t *out)
{
    if (code < 0x80) { out[0] = (uint8_t)code; return 1; }
    if (code < 0x800) {
        out[0] = (uint8_t)(0xC0 | (code >> 6));
        out[1] = (uint8_t)(0x80 | (code & 0x3F));
        return 2;
    }
    if (code < 0x10000) {
        out[0] = (uint8_t)(0xE0 | (code >> 12));
        out[1] = (uint8_t)(0x80 | ((code >> 6) & 0x3F));
        out[2] = (uint8_t)(0x80 | (code & 0x3F));
        return 3;
    }
    out[0] = (uint8_t)(0xF0 | (code >> 18));
    out[1] = (uint8_t)(0x80 | ((code >> 12) & 0x3F));
    out[2] = (uint8_t)(0x80 | ((code >> 6) & 0x3F));
    out[3] = (uint8_t)(0x80 | (code & 0x3F));
    return 4;
}

NplVal npl_string_from_utf8(const uint8_t *bytes, size_t length, uint32_t tag)
{
    int ascii = 1;
    for (size_t i = 0; i < length; i++)
        if (bytes[i] >= 0x80) { ascii = 0; break; }
    if (length > UINT32_MAX) npl_runtime_oom_internal();
    if (ascii)
        return npl_box_bytes_internal((const char *)bytes, (uint32_t)length,
                                      tag);
    uint32_t *chars = (uint32_t *)malloc(length * sizeof(uint32_t));
    if (!chars) npl_runtime_oom_internal();
    uint32_t count = 0;
    size_t index = 0;
    while (index < length)
        chars[count++] = npl_utf8_decode_internal(bytes, length, &index);
    NplVal value = npl_string_from_chars_internal(chars, count, tag);
    free(chars);
    return value;
}

/* ---- access ---- */

const NplString *npl_require_bytes_internal(NplVal v, const char *who)
{
    if (!npl_is_ptr(v) || (npl_header(v)->tag != BOX_STRING &&
                           npl_header(v)->tag != BOX_SYMBOL))
        npl_primitive_error(who, "expected a string", v);
    return (const NplString *)npl_header(v);
}

uint32_t npl_string_char_count_internal(const NplString *string)
{
    if (string->header.flags & NPL_FLAG_OUT_OF_LINE)
        return out_of_line(string)->length;
    return string->length;
}

uint32_t npl_string_char_at_internal(const NplString *string, uint32_t index)
{
    if (is_wide(string)) return wide_data(string)[index];
    return (unsigned char)string->data[index];
}

/* A NUL-terminated UTF-8 view. Narrow ASCII strings return their own bytes
 * (*owned = 0); anything else is encoded into a malloc'd buffer the caller
 * frees (*owned = 1). */
const char *npl_string_utf8(const NplString *string, size_t *length,
                            int *owned)
{
    uint32_t count = npl_string_char_count_internal(string);
    if (!is_wide(string)) {
        int ascii = 1;
        for (uint32_t i = 0; i < count; i++)
            if ((unsigned char)string->data[i] >= 0x80) { ascii = 0; break; }
        if (ascii) {
            *length = count;
            *owned = 0;
            return string->data;
        }
    }
    char *buffer = (char *)malloc((size_t)count * 4 + 1);
    if (!buffer) npl_runtime_oom_internal();
    size_t used = 0;
    for (uint32_t i = 0; i < count; i++)
        used += npl_utf8_encode_internal(npl_string_char_at_internal(string, i),
                                         (uint8_t *)buffer + used);
    buffer[used] = '\0';
    *length = used;
    *owned = 1;
    return buffer;
}

/* Hash and compare by characters, independent of width (symbol interning). */
uint64_t npl_string_hash_internal(const NplString *string)
{
    uint64_t hash = UINT64_C(1469598103934665603);  /* FNV-1a over code points */
    uint32_t count = npl_string_char_count_internal(string);
    for (uint32_t i = 0; i < count; i++) {
        uint32_t code = npl_string_char_at_internal(string, i);
        for (int b = 0; b < 4 && (b == 0 || (code >> (8 * b)) != 0); b++) {
            hash ^= (code >> (8 * b)) & 0xFF;
            hash *= UINT64_C(1099511628211);
        }
    }
    return hash;
}

int npl_string_same_chars_internal(const NplString *a, const NplString *b)
{
    uint32_t count = npl_string_char_count_internal(a);
    if (count != npl_string_char_count_internal(b)) return 0;
    if (!is_wide(a) && !is_wide(b))
        return memcmp(a->data, b->data, count) == 0;
    for (uint32_t i = 0; i < count; i++)
        if (npl_string_char_at_internal(a, i) !=
            npl_string_char_at_internal(b, i))
            return 0;
    return 1;
}

/* ---- mutation ---- */

/* Make room for a character above 255 in a narrow mutable string. */
static void promote(NplString *string)
{
    uint32_t count = string->length;
    NplWideBuffer *buffer = (NplWideBuffer *)npl_alloc_object(
        sizeof(NplWideBuffer) + ((size_t)count + 1) * sizeof(uint32_t));
    buffer->header.tag = BOX_STRING_DATA;
    buffer->header.flags = 0;
    buffer->length = count;
    buffer->capacity = count;
    for (uint32_t i = 0; i < count; i++)
        buffer->data[i] = (unsigned char)string->data[i];
    buffer->data[count] = 0;
    memcpy(string->data, &buffer, sizeof(buffer));
    string->header.flags |= NPL_FLAG_WIDE | NPL_FLAG_OUT_OF_LINE;
    npl_gc_write_barrier(string, (NplVal)(uintptr_t)buffer | TAG_PTR);
}

/* May allocate (promotion): the caller keeps the string's value reachable
 * (an argument or a rooted local); the object does not move. */
void npl_string_store_internal(NplString *string, uint32_t index,
                               uint32_t code)
{
    if (!is_wide(string)) {
        if (code <= 0xFF) {
            string->data[index] = (char)(unsigned char)code;
            return;
        }
        promote(string);
    }
    wide_data(string)[index] = code;
}

/* GC: an out-of-line buffer is a managed object the string owns. */
NplVal npl_string_owned_buffer_internal(const NplString *string)
{
    if (string->header.flags & NPL_FLAG_OUT_OF_LINE)
        return (NplVal)(uintptr_t)out_of_line(string) | TAG_PTR;
    return npl_nil();
}

int npl_string_scalar_value_internal(uint32_t code)
{
    return scalar_value(code);
}

/* ---- (scheme base) UTF-8 conversion (R7RS 6.7) ---- */

/* string->utf8 over characters [start, end) of a string. */
NplVal npl_string_to_utf8(NplVal value, NplVal start, NplVal end)
{
    const NplString *string = npl_require_bytes_internal(value, "string->utf8");
    uint32_t count = npl_string_char_count_internal(string);
    if (!npl_is_fixnum(start) || !npl_is_fixnum(end) ||
        npl_fixnum_value(start) < 0 ||
        npl_fixnum_value(start) > npl_fixnum_value(end) ||
        npl_fixnum_value(end) > (int64_t)count)
        npl_primitive_error("string->utf8", "index out of range",
                            npl_cons(start, npl_cons(end, npl_nil())));
    uint32_t from = (uint32_t)npl_fixnum_value(start);
    uint32_t to = (uint32_t)npl_fixnum_value(end);
    size_t length = 0;
    uint8_t scratch[4];
    for (uint32_t i = from; i < to; i++)
        length += npl_utf8_encode_internal(
            npl_string_char_at_internal(string, i), scratch);
    if (length > UINT32_MAX) npl_runtime_oom_internal();
    NplBytevector *bytes = (NplBytevector *)npl_alloc_object(
        sizeof(NplBytevector) + length);
    bytes->header.tag = BOX_BYTEVECTOR;
    bytes->header.flags = 0;
    bytes->length = (uint32_t)length;
    bytes->capacity = (uint32_t)length;
    /* The allocation may collect; string is reachable through value. */
    string = (const NplString *)npl_header(value);
    size_t used = 0;
    for (uint32_t i = from; i < to; i++)
        used += npl_utf8_encode_internal(npl_string_char_at_internal(string, i),
                                         bytes->data + used);
    return (NplVal)(uintptr_t)bytes | TAG_PTR;
}

/* utf8->string over bytes [start, end); invalid UTF-8 raises (as Guile). */
NplVal npl_utf8_to_string(NplVal value, NplVal start, NplVal end)
{
    if (!npl_is_ptr(value) || npl_header(value)->tag != BOX_BYTEVECTOR)
        npl_primitive_error("utf8->string", "expected a bytevector", value);
    const NplBytevector *bytes = (const NplBytevector *)npl_header(value);
    if (!npl_is_fixnum(start) || !npl_is_fixnum(end) ||
        npl_fixnum_value(start) < 0 ||
        npl_fixnum_value(start) > npl_fixnum_value(end) ||
        npl_fixnum_value(end) > (int64_t)bytes->length)
        npl_primitive_error("utf8->string", "index out of range",
                            npl_cons(start, npl_cons(end, npl_nil())));
    size_t from = (size_t)npl_fixnum_value(start);
    size_t to = (size_t)npl_fixnum_value(end);
    uint32_t *chars = (uint32_t *)malloc((to - from + 1) * sizeof(uint32_t));
    if (!chars) npl_runtime_oom_internal();
    uint32_t count = 0;
    size_t index = 0;
    const uint8_t *data = bytes->data + from;
    while (index < to - from) {
        size_t before = index;
        uint32_t code = npl_utf8_decode_internal(data, to - from, &index);
        if (code == 0xFFFD &&
            !(index - before == 3 && data[before] == 0xEF &&
              data[before + 1] == 0xBF && data[before + 2] == 0xBD)) {
            free(chars);
            npl_primitive_error("utf8->string", "invalid UTF-8 sequence",
                                npl_fixnum((int64_t)(from + before)));
        }
        chars[count++] = code;
    }
    NplVal out = npl_string_from_chars_internal(chars, count, BOX_STRING);
    free(chars);
    return out;
}

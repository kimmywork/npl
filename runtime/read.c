/* Datum input: read (R7RS 6.13.2), decoding UTF-8. See port.h for the
 * module split. */

#include "port.h"
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Datum labels (R7RS 2.4): labels is a list of (n placeholder . value)
 * entries for this read (rooted by npl_read_datum); value is the
 * placeholder until the labelled datum is complete. */
typedef struct {
    NplPort *port;
    int lookahead;
    int has_lookahead;
    NplVal labels;
    int fold_case;      /* #!fold-case in effect (R7RS 2.1) */
} NplDatumReader;

static jmp_buf *g_read_recovery = NULL;
static char g_read_error_message[128];

static void read_error(const char *message)
{
    if (g_read_recovery) {
        (void)snprintf(g_read_error_message, sizeof(g_read_error_message),
                       "%s", message);
        longjmp(*g_read_recovery, 1);
    }
    /* Outside read-result (for example the raw read primitive), a syntax
     * error is the same read error object (scheme read) raises. */
    npl_primitive_read_error(message);
}

static void read_buffer_error(char *buffer, const char *message)
{
    free(buffer);
    read_error(message);
}

/* The datum reader works on characters (UTF-8 decoded). Its own lookahead
 * is returned to the port when read finishes (npl_read_datum). */
static int reader_get(NplDatumReader *reader)
{
    if (reader->has_lookahead) {
        reader->has_lookahead = 0;
        return reader->lookahead;
    }
    return npl_port_read_char_internal(reader->port);
}

static int reader_peek(NplDatumReader *reader)
{
    if (!reader->has_lookahead) {
        reader->lookahead = npl_port_read_char_internal(reader->port);
        reader->has_lookahead = 1;
    }
    return reader->lookahead;
}

static int datum_delimiter(int code)
{
    return code == EOF || code == '(' || code == ')' || code == '"' ||
           code == ';' || code == '\'' || code == '`' || code == ',' ||
           code == '|' || code == ' ' || code == '\t' || code == '\n' ||
           code == '\r' || code == '\f';
}

static NplVal read_datum_inner(NplDatumReader *reader, int allow_eof);

/* #| ... |# comments nest (R7RS 2.2). */
static void skip_block_comment(NplDatumReader *reader)
{
    int depth = 1;
    int previous = 0;
    while (depth > 0) {
        int code = reader_get(reader);
        if (code == EOF) read_error("unterminated block comment");
        if (previous == '|' && code == '#') { depth--; code = 0; }
        else if (previous == '#' && code == '|') { depth++; code = 0; }
        previous = code;
    }
}

/* #!fold-case / #!no-fold-case directives (R7RS 2.1). */
static void read_directive(NplDatumReader *reader)
{
    char name[16];
    size_t length = 0;
    while (!datum_delimiter(reader_peek(reader))) {
        int code = reader_get(reader);
        if (length + 1 >= sizeof(name)) read_error("unknown directive");
        name[length++] = (char)code;
    }
    name[length] = '\0';
    if (strcmp(name, "fold-case") == 0) reader->fold_case = 1;
    else if (strcmp(name, "no-fold-case") == 0) reader->fold_case = 0;
    else read_error("unknown directive");
}

/* Skips whitespace and comments: line (;), block (#|...|#), datum (#;),
 * and #! directives. A '#' that starts none of these is pushed back as the
 * reader's lookahead pair: reader->pending_hash. */
static int symbol_named(NplVal value, const char *name)
{
    if (!npl_is_ptr(value) || npl_header(value)->tag != BOX_SYMBOL) return 0;
    const NplString *symbol = (const NplString *)npl_header(value);
    size_t length = strlen(name);
    if (npl_string_char_count_internal(symbol) != length) return 0;
    for (size_t i = 0; i < length; i++)
        if (npl_string_char_at_internal(symbol, (uint32_t)i) !=
            (unsigned char)name[i])
            return 0;
    return 1;
}

static void reader_skip_space(NplDatumReader *reader)
{
    for (;;) {
        int code = reader_peek(reader);
        if (code == ' ' || code == '\t' || code == '\n' || code == '\r' ||
            code == '\f') {
            (void)reader_get(reader);
        } else if (code == ';') {
            while ((code = reader_get(reader)) != EOF && code != '\n') {}
        } else if (code == '#') {
            (void)reader_get(reader);
            int next = reader_peek(reader);
            if (next == '|') {
                (void)reader_get(reader);
                skip_block_comment(reader);
            } else if (next == ';') {
                (void)reader_get(reader);
                /* The commented-out thing must be a datum: a lone '.' is
                 * list syntax, not a datum (chibi r7rs-tests). */
                reader_skip_space(reader);
                int lead = reader_peek(reader);
                NplVal skipped = read_datum_inner(reader, 0);
                if (lead != '|' && symbol_named(skipped, "."))
                    read_error("datum comment needs a datum");
            } else if (next == '!') {
                (void)reader_get(reader);
                read_directive(reader);
            } else {
                /* Not a comment: give the '#' back. The next character is
                 * already in the lookahead, so keep both through the port. */
                uint8_t bytes[8];
                size_t count = next == EOF ? 0
                    : npl_utf8_encode_internal((uint32_t)next, bytes + 1);
                reader->has_lookahead = 0;
                bytes[0] = '#';
                npl_port_unread_bytes_internal(reader->port, bytes, count + 1);
                return;
            }
        } else {
            return;
        }
    }
}

/* Append a character to a malloc'd UTF-8 buffer. */
static void buffer_push(char **buffer, size_t *length, size_t *capacity,
                        int code)
{
    if (*length + 5 >= *capacity) {
        size_t next = *capacity ? *capacity * 2 : 32;
        char *grown = (char *)realloc(*buffer, next);
        if (!grown) npl_runtime_oom_internal();
        *buffer = grown;
        *capacity = next;
    }
    if (code == 0) {
        (*buffer)[(*length)++] = '\0';
        return;
    }
    *length += npl_utf8_encode_internal((uint32_t)code,
                                        (uint8_t *)*buffer + *length);
}

/* \x<hex>; in strings (R7RS 6.7) and #\x<hex> characters. */
static int hex_scalar(const char *text, size_t length, uint32_t *code)
{
    if (length == 0 || length > 6) return 0;
    uint32_t value = 0;
    for (size_t i = 0; i < length; i++) {
        char c = text[i];
        int digit = (c >= '0' && c <= '9') ? c - '0'
            : (c >= 'a' && c <= 'f') ? c - 'a' + 10
            : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
        if (digit < 0) return 0;
        value = value * 16 + (uint32_t)digit;
    }
    if (!npl_string_scalar_value_internal(value)) return 0;
    *code = value;
    return 1;
}

static NplVal read_datum_inner(NplDatumReader *reader, int allow_eof);

static NplVal read_string_datum(NplDatumReader *reader)
{
    char *buffer = NULL;
    size_t length = 0;
    size_t capacity = 0;
    for (;;) {
        int code = reader_get(reader);
        if (code == EOF)
            read_buffer_error(buffer, "unexpected EOF in string");
        if (code == '"') break;
        if (code == '\\') {
            code = reader_get(reader);
            if (code == EOF)
                read_buffer_error(buffer, "unexpected EOF in string escape");
            if (code == 'n') code = '\n';
            else if (code == 'r') code = '\r';
            else if (code == 't') code = '\t';
            else if (code == 'a') code = 7;
            else if (code == 'b') code = 8;
            else if (code == 'x' || code == 'X') {
                char hex[8];
                size_t used = 0;
                while ((code = reader_get(reader)) != ';') {
                    if (code == EOF || used == sizeof(hex))
                        read_buffer_error(buffer, "malformed \\x escape");
                    hex[used++] = (char)code;
                }
                uint32_t scalar;
                if (!hex_scalar(hex, used, &scalar))
                    read_buffer_error(buffer, "malformed \\x escape");
                code = (int)scalar;
            } else if (code == ' ' || code == '\t' || code == '\n' ||
                       code == '\r') {
                /* \<intraline whitespace>*<line ending><intraline
                 * whitespace>* is a line continuation (R7RS 6.7). */
                while (code == ' ' || code == '\t') code = reader_get(reader);
                if (code == '\r') {
                    if (reader_peek(reader) == '\n') (void)reader_get(reader);
                } else if (code != '\n') {
                    read_buffer_error(buffer, "unknown string escape");
                }
                while (reader_peek(reader) == ' ' || reader_peek(reader) == '\t')
                    (void)reader_get(reader);
                continue;
            } else if (code != '"' && code != '\\' && code != '|')
                read_buffer_error(buffer, "unknown string escape");
        }
        buffer_push(&buffer, &length, &capacity, code);
    }
    NplVal value = npl_string_from_utf8((const uint8_t *)buffer, length,
                                        BOX_STRING);
    free(buffer);
    return value;
}

/* Characters of a |...| symbol (R7RS 2.1), with string-like escapes. */
static void read_bar_symbol_part(NplDatumReader *reader, char **buffer,
                                 size_t *length, size_t *capacity)
{
    for (;;) {
        int code = reader_get(reader);
        if (code == EOF) read_buffer_error(*buffer, "unterminated |symbol|");
        if (code == '|') return;
        if (code == '\\') {
            code = reader_get(reader);
            if (code == 'x' || code == 'X') {
                char hex[8];
                size_t used = 0;
                while ((code = reader_get(reader)) != ';') {
                    if (code == EOF || used == sizeof(hex))
                        read_buffer_error(*buffer, "malformed \\x escape");
                    hex[used++] = (char)code;
                }
                uint32_t scalar;
                if (!hex_scalar(hex, used, &scalar))
                    read_buffer_error(*buffer, "malformed \\x escape");
                code = (int)scalar;
            } else if (code == 'n') code = '\n';
            else if (code == 't') code = '\t';
            else if (code == 'r') code = '\r';
            else if (code == 'a') code = 7;
            else if (code == 'b') code = 8;
            else if (code != '|' && code != '\\' && code != '"')
                read_buffer_error(*buffer, "unknown escape in |symbol|");
        }
        buffer_push(buffer, length, capacity, code);
    }
}

static NplVal read_token_datum(NplDatumReader *reader, int first)
{
    char *buffer = NULL;
    size_t length = 0;
    size_t capacity = 0;
    int barred = 0;
    if (first == '|') {
        barred = 1;
        read_bar_symbol_part(reader, &buffer, &length, &capacity);
    } else {
        buffer_push(&buffer, &length, &capacity, first);
    }
    /* A |...| part may continue the token, and vice versa (abc|d e|f). */
    for (;;) {
        int code = reader_peek(reader);
        if (code == '|') {
            (void)reader_get(reader);
            barred = 1;
            read_bar_symbol_part(reader, &buffer, &length, &capacity);
        } else if (datum_delimiter(code)) {
            break;
        } else {
            buffer_push(&buffer, &length, &capacity, reader_get(reader));
        }
    }
    buffer_push(&buffer, &length, &capacity, 0);
    if (barred) {
        NplVal symbol = npl_symbol(buffer, (uint32_t)(length - 1));
        free(buffer);
        return symbol;
    }

    NplVal token = npl_string_from_utf8((const uint8_t *)buffer, length - 1,
                                        BOX_STRING);
    NplVal value = npl_number_from_string(token);
    if (value == npl_false()) {
        int numeric_start = buffer[0] >= '0' && buffer[0] <= '9';
        if ((buffer[0] == '+' || buffer[0] == '-') && length > 2)
            numeric_start = buffer[1] >= '0' && buffer[1] <= '9';
        if (numeric_start) read_buffer_error(buffer, "invalid exact number");
        value = npl_symbol(buffer, (uint32_t)(length - 1));
        if (reader->fold_case) value = npl_symbol_fold_internal(value);
    }
    free(buffer);
    return value;
}


static NplVal reverse_onto(NplVal values, NplVal tail)
{
    while (!npl_is_nil(values)) {
        tail = npl_cons(npl_car(values), tail);
        values = npl_cdr(values);
    }
    return tail;
}

static NplVal read_list_datum(NplDatumReader *reader)
{
    NplVal reversed = npl_nil();
    for (;;) {
        reader_skip_space(reader);
        int code = reader_peek(reader);
        if (code == EOF) read_error("unexpected EOF in list");
        if (code == ')') {
            (void)reader_get(reader);
            return reverse_onto(reversed, npl_nil());
        }
        int bar = code == '|';
        NplVal value = read_datum_inner(reader, 0);
        if (!bar && symbol_named(value, ".")) {
            if (npl_is_nil(reversed)) read_error("misplaced dot");
            reader_skip_space(reader);
            if (reader_peek(reader) == ')') read_error("missing datum after dot");
            NplVal tail = read_datum_inner(reader, 0);
            reader_skip_space(reader);
            if (reader_get(reader) != ')')
                read_error("expected ) after dotted datum");
            return reverse_onto(reversed, tail);
        }
        reversed = npl_cons(value, reversed);
    }
}

/* 'x `x ,x ,@x (R7RS 4.2.8). */
static NplVal abbreviated_datum(const char *name, NplVal value)
{
    npl_gc_root(&value);
    NplVal symbol = npl_symbol(name, (uint32_t)strlen(name));
    NplVal result = npl_cons(symbol, npl_cons(value, npl_nil()));
    npl_gc_unroot(&value);
    return result;
}

static NplVal quoted_datum(NplVal value)
{
    return abbreviated_datum("quote", value);
}

/* #\<char>, #\<name> (R7RS 6.6), #\x<hex>. The first character after #\
 * is always part of the literal, even a delimiter such as ( or space. */
static NplVal read_character_datum(NplDatumReader *reader)
{
    static const struct { const char *name; uint32_t code; } names[] = {
        { "alarm", 7 }, { "backspace", 8 }, { "delete", 0x7F },
        { "escape", 0x1B }, { "newline", 10 }, { "null", 0 }, { "nul", 0 },
        { "return", 13 }, { "space", 32 }, { "tab", 9 },
    };
    int first = reader_get(reader);
    if (first == EOF) read_error("empty character literal");
    if (datum_delimiter(reader_peek(reader))) return npl_char((uint32_t)first);
    /* Names are compared after ASCII case folding under #!fold-case. */
    char buffer[32];
    size_t length = 0;
    buffer[length++] = (char)first;
    while (!datum_delimiter(reader_peek(reader))) {
        int code = reader_get(reader);
        if (length + 1 >= sizeof(buffer) || code >= 0x80)
            read_error("unknown character name");
        buffer[length++] = (char)code;
    }
    buffer[length] = '\0';
    if (first >= 0x80) read_error("unknown character name");
    if (reader->fold_case)
        for (size_t i = 0; i < length; i++)
            if (buffer[i] >= 'A' && buffer[i] <= 'Z') buffer[i] += 'a' - 'A';
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++)
        if (strcmp(buffer, names[i].name) == 0) return npl_char(names[i].code);
    uint32_t code;
    if ((buffer[0] == 'x' || buffer[0] == 'X') &&
        hex_scalar(buffer + 1, length - 1, &code))
        return npl_char(code);
    read_error("unknown character name");
    return npl_nil();
}

static NplVal bytevector_from_list(NplVal list)
{
    size_t count = 0;
    for (NplVal rest = list; !npl_is_nil(rest); rest = npl_cdr(rest)) count++;
    NplVal *items = count ? (NplVal *)malloc(count * sizeof(NplVal)) : NULL;
    if (count && !items) npl_runtime_oom_internal();
    size_t index = 0;
    for (NplVal rest = list; !npl_is_nil(rest); rest = npl_cdr(rest))
        items[index++] = npl_car(rest);
    NplVal value = npl_make_bytevector((int64_t)count, items);
    free(items);
    return value;
}

static NplVal read_hash_radix_datum(NplDatumReader *reader, int prefix)
{
    char *buffer = NULL;
    size_t length = 0;
    size_t capacity = 0;
    buffer_push(&buffer, &length, &capacity, '#');
    buffer_push(&buffer, &length, &capacity, prefix);
    while (!datum_delimiter(reader_peek(reader)))
        buffer_push(&buffer, &length, &capacity, reader_get(reader));
    NplVal text = npl_string_from_utf8((const uint8_t *)buffer, length,
                                       BOX_STRING);
    NplVal value = npl_number_from_string(text);
    free(buffer);
    if (value == npl_false()) read_error("invalid radix number");
    return value;
}

/* ---- datum labels ---- */

static NplVal label_entry(NplDatumReader *reader, int64_t label)
{
    for (NplVal rest = reader->labels; !npl_is_nil(rest);
         rest = npl_pair_ptr(rest)->cdr) {
        NplVal entry = npl_pair_ptr(rest)->car;
        if (npl_fixnum_value(npl_pair_ptr(entry)->car) == label) return entry;
    }
    return npl_nil();
}

/* Pointer set for the patch walk (objects stay reachable from the datum). */
typedef struct {
    NplVal *slots;
    size_t capacity;
    size_t count;
} NplSeen;

static int seen_add(NplSeen *seen, NplVal value)
{
    if (seen->count * 2 >= seen->capacity) {
        size_t capacity = seen->capacity ? seen->capacity * 2 : 64;
        NplVal *slots = (NplVal *)calloc(capacity, sizeof(NplVal));
        if (!slots) npl_runtime_oom_internal();
        for (size_t i = 0; i < seen->capacity; i++) {
            if (!seen->slots[i]) continue;
            size_t k = (size_t)(seen->slots[i] >> 3) & (capacity - 1);
            while (slots[k]) k = (k + 1) & (capacity - 1);
            slots[k] = seen->slots[i];
        }
        free(seen->slots);
        seen->slots = slots;
        seen->capacity = capacity;
    }
    size_t k = (size_t)(value >> 3) & (seen->capacity - 1);
    while (seen->slots[k]) {
        if (seen->slots[k] == value) return 0;
        k = (k + 1) & (seen->capacity - 1);
    }
    seen->slots[k] = value;
    seen->count++;
    return 1;
}

/* Replace references to placeholder inside value with replacement. */
static NplVal patch_placeholder(NplVal original, NplVal placeholder,
                                NplVal replacement, NplSeen *seen)
{
    if (original == placeholder) return replacement;
    NplVal value = original;
    while (npl_is_ptr(value) && seen_add(seen, value)) {
        uint32_t tag = npl_header(value)->tag;
        if (tag == BOX_VECTOR) {
            NplVector *vector = (NplVector *)npl_header(value);
            for (uint32_t i = 0; i < vector->length; i++) {
                vector->data[i] = patch_placeholder(vector->data[i], placeholder,
                                                    replacement, seen);
                npl_gc_write_barrier(vector, vector->data[i]);
            }
            break;
        }
        if (tag != BOX_PAIR) break;
        NplPair *pair = npl_pair_ptr(value);
        pair->car = patch_placeholder(pair->car, placeholder, replacement, seen);
        npl_gc_write_barrier(pair, pair->car);
        if (pair->cdr == placeholder) {
            pair->cdr = replacement;
            npl_gc_write_barrier(pair, replacement);
            break;
        }
        value = pair->cdr;
    }
    return original;
}

static int array_pair_p(NplVal value)
{
    return npl_is_ptr(value) && npl_header(value)->tag == BOX_PAIR;
}

static int array_proper_list_p(NplVal list)
{
    NplVal slow = list;
    NplVal fast = list;
    while (array_pair_p(fast) && array_pair_p(npl_pair_ptr(fast)->cdr)) {
        slow = npl_pair_ptr(slow)->cdr;
        fast = npl_pair_ptr(npl_pair_ptr(fast)->cdr)->cdr;
        if (slow == fast) return 0;
    }
    while (array_pair_p(list)) list = npl_pair_ptr(list)->cdr;
    return npl_is_nil(list);
}

static int shape_equal(NplVal left, NplVal right)
{
    while (array_pair_p(left) && array_pair_p(right)) {
        if (npl_fixnum_value(npl_pair_ptr(left)->car) !=
            npl_fixnum_value(npl_pair_ptr(right)->car))
            return 0;
        left = npl_pair_ptr(left)->cdr;
        right = npl_pair_ptr(right)->cdr;
    }
    return npl_is_nil(left) && npl_is_nil(right);
}

/* Returns the shape as a list of dimensions and appends the row-major data
 * to *flat_reversed. */
static NplVal array_collect(NplVal list, int64_t rank, NplVal *flat_reversed)
{
    if (!array_proper_list_p(list))
        read_error("array literal row is not a proper list");
    if (rank == 1) {
        int64_t count = 0;
        NplVal cursor = list;
        while (array_pair_p(cursor)) {
            *flat_reversed =
                npl_cons(npl_pair_ptr(cursor)->car, *flat_reversed);
            count++;
            cursor = npl_pair_ptr(cursor)->cdr;
        }
        if (!npl_is_nil(cursor)) read_error("improper array row");
        return npl_cons(npl_fixnum(count), npl_nil());
    }
    int64_t rows = 0;
    NplVal shape = npl_nil();
    NplVal cursor = list;
    while (array_pair_p(cursor)) {
        NplVal row = npl_pair_ptr(cursor)->car;
        if (!array_pair_p(row) && !npl_is_nil(row))
            read_error("array literal row is not a list");
        NplVal row_shape = array_collect(row, rank - 1, flat_reversed);
        if (rows == 0)
            shape = row_shape;
        else if (!shape_equal(shape, row_shape))
            read_error("array literal rows differ in shape");
        rows++;
        cursor = npl_pair_ptr(cursor)->cdr;
    }
    if (!npl_is_nil(cursor)) read_error("improper array list");
    if (rows == 0) {
        for (int64_t i = 0; i < rank - 1; i++)
            shape = npl_cons(npl_fixnum(0), shape);
    }
    return npl_cons(npl_fixnum(rows), shape);
}

/* Build [npl-array-tag shape flat-data] from the list read after #nA. */
static NplVal array_from_items(NplVal list, int64_t rank)
{
    if (rank < 1) read_error("array literal rank must be at least 1");
    if (rank > 32) read_error("array literal rank is too large");
    if (rank == 1) {
        if (!array_proper_list_p(list))
            read_error("array literal row is not a proper list");
        return npl_list_to_vector(list);
    }
    NplVal flat_reversed = npl_nil();
    NplVal shape = npl_list_to_vector(array_collect(list, rank, &flat_reversed));
    NplVal data = npl_list_to_vector(reverse_onto(flat_reversed, npl_nil()));
    NplVal items[3];
    items[0] = npl_symbol("npl-array-literal-5f8d", 22);
    items[1] = shape;
    items[2] = data;
    return npl_make_vector(3, items);
}

/* #n=datum or #n# after the digits of n. */
static NplVal read_label_after_digits(NplDatumReader *reader, int64_t label,
                                      int code)
{
    if (code == '#') {
        NplVal entry = label_entry(reader, label);
        if (npl_is_nil(entry)) read_error("undefined datum label");
        return npl_pair_ptr(npl_pair_ptr(entry)->cdr)->cdr;
    }
    if (code != '=') read_error("malformed datum label");
    if (!npl_is_nil(label_entry(reader, label)))
        read_error("duplicate datum label");
    /* A fresh pair stands for the datum until it is read. */
    NplVal placeholder = npl_cons(npl_false(), npl_false());
    NplVal entry = npl_cons(npl_fixnum(label),
                            npl_cons(placeholder, placeholder));
    reader->labels = npl_cons(entry, reader->labels);
    NplVal datum = read_datum_inner(reader, 0);
    if (datum == placeholder)
        read_error("datum label cannot reference itself");
    NplSeen seen = { NULL, 0, 0 };
    datum = patch_placeholder(datum, placeholder, datum, &seen);
    free(seen.slots);
    npl_pair_ptr(npl_pair_ptr(entry)->cdr)->cdr = datum;
    npl_gc_write_barrier(npl_pair_ptr(npl_pair_ptr(entry)->cdr), datum);
    return datum;
}

static NplVal read_hash_datum(NplDatumReader *reader)
{
    int code = reader_get(reader);
    if (code >= '0' && code <= '9') {
        int64_t rank = code - '0';
        int next;
        while ((next = reader_get(reader)) >= '0' && next <= '9') {
            if (rank > (INT64_C(1) << 40))
                read_error("datum label too large");
            rank = rank * 10 + (next - '0');
        }
        if (next == 'A' || next == 'a') {
            if (rank > 32) read_error("array literal rank is too large");
            reader_skip_space(reader);
            if (reader_peek(reader) != '(')
                read_error("array literal requires (...)");
            (void)reader_get(reader);
            return array_from_items(read_list_datum(reader), rank);
        }
        return read_label_after_digits(reader, rank, next);
    }
    if (code == 't' || code == 'f') {
        /* #t #f #true #false (R7RS 2.1). */
        const char *rest = code == 't' ? "rue" : "alse";
        if (!datum_delimiter(reader_peek(reader))) {
            for (const char *p = rest; *p; p++)
                if (reader_get(reader) != *p)
                    read_error("invalid boolean literal");
            if (!datum_delimiter(reader_peek(reader)))
                read_error("invalid boolean literal");
        }
        return code == 't' ? npl_true() : npl_false();
    }
    if (code == 'b' || code == 'B' || code == 'o' || code == 'O' ||
        code == 'd' || code == 'D' || code == 'x' || code == 'X' ||
        code == 'e' || code == 'E' || code == 'i' || code == 'I')
        return read_hash_radix_datum(reader, code);
    if (code == '\\') return read_character_datum(reader);
    if (code == '(') return npl_list_to_vector(read_list_datum(reader));
    if (code == 'u') {
        if (reader_get(reader) != '8' || reader_get(reader) != '(')
            read_error("malformed bytevector");
        return bytevector_from_list(read_list_datum(reader));
    }
    read_error("unknown # syntax");
    return npl_nil();
}

static NplVal read_datum_inner(NplDatumReader *reader, int allow_eof)
{
    reader_skip_space(reader);
    int code = reader_get(reader);
    if (code == EOF) {
        if (allow_eof) return npl_port_eof_internal();
        read_error("unexpected EOF");
    }
    if (code == '(') return read_list_datum(reader);
    if (code == ')') read_error("unexpected )");
    if (code == '"') return read_string_datum(reader);
    if (code == '\'') return quoted_datum(read_datum_inner(reader, 0));
    if (code == '`')
        return abbreviated_datum("quasiquote", read_datum_inner(reader, 0));
    if (code == ',') {
        if (reader_peek(reader) == '@') {
            (void)reader_get(reader);
            return abbreviated_datum("unquote-splicing",
                                     read_datum_inner(reader, 0));
        }
        return abbreviated_datum("unquote", read_datum_inner(reader, 0));
    }
    if (code == '#') return read_hash_datum(reader);
    return read_token_datum(reader, code);
}

NplVal npl_read_datum(NplVal port_value_arg)
{
    NplPort *port = npl_port_require_mode_internal(port_value_arg, NPL_PORT_INPUT,
                                     NPL_PORT_TEXTUAL, "read");
    NplDatumReader reader = { port, 0, 0, npl_nil(), 0 };
    npl_gc_root(&reader.labels);
    NplVal value = read_datum_inner(&reader, 1);
    npl_gc_unroot(&reader.labels);
    if (reader.has_lookahead && reader.lookahead != EOF) {
        uint8_t bytes[4];
        size_t count = npl_utf8_encode_internal((uint32_t)reader.lookahead,
                                                bytes);
        npl_port_unread_bytes_internal(port, bytes, count);
    }
    return value;
}

NplVal npl_read_result(NplVal port_value)
{
    jmp_buf recovery;
    jmp_buf *previous = g_read_recovery;
    g_read_recovery = &recovery;
    /* read_error longjmps past the reader's roots; drop them on recovery. */
    int roots = npl_gc_root_count_internal();
    NplVal items[2];
    if (setjmp(recovery) == 0) {
        items[0] = npl_true();
        items[1] = npl_read_datum(port_value);
    } else {
        npl_gc_restore_root_count_internal(roots);
        items[0] = npl_false();
        items[1] = npl_string(g_read_error_message,
                              (uint32_t)strlen(g_read_error_message));
    }
    g_read_recovery = previous;
    return npl_make_vector(2, items);
}

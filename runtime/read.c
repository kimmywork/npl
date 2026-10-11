/* Datum input: read (R7RS 6.13.2), decoding UTF-8. See port.h for the
 * module split. */

#include "port.h"
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    NplPort *port;
    int lookahead;
    int has_lookahead;
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
    fprintf(stderr, "read: %s\n", message);
    exit(1);
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
           code == ' ' || code == '\t' || code == '\n' || code == '\r';
}

static void reader_skip_space(NplDatumReader *reader)
{
    for (;;) {
        int code = reader_peek(reader);
        if (code == ' ' || code == '\t' || code == '\n' || code == '\r') {
            (void)reader_get(reader);
        } else if (code == ';') {
            while ((code = reader_get(reader)) != EOF && code != '\n') {}
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

static NplVal read_token_datum(NplDatumReader *reader, int first)
{
    char *buffer = NULL;
    size_t length = 0;
    size_t capacity = 0;
    buffer_push(&buffer, &length, &capacity, first);
    while (!datum_delimiter(reader_peek(reader)))
        buffer_push(&buffer, &length, &capacity, reader_get(reader));
    buffer_push(&buffer, &length, &capacity, 0);

    NplVal token = npl_string_from_utf8((const uint8_t *)buffer, length - 1,
                                        BOX_STRING);
    NplVal value = npl_number_from_string(token);
    if (value == npl_false()) {
        int numeric_start = buffer[0] >= '0' && buffer[0] <= '9';
        if ((buffer[0] == '+' || buffer[0] == '-') && length > 2)
            numeric_start = buffer[1] >= '0' && buffer[1] <= '9';
        if (numeric_start) read_buffer_error(buffer, "invalid exact number");
        value = npl_symbol(buffer, (uint32_t)(length - 1));
    }
    free(buffer);
    return value;
}

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
        NplVal value = read_datum_inner(reader, 0);
        if (symbol_named(value, ".")) {
            if (npl_is_nil(reversed)) read_error("misplaced dot");
            NplVal tail = read_datum_inner(reader, 0);
            reader_skip_space(reader);
            if (reader_get(reader) != ')')
                read_error("expected ) after dotted datum");
            return reverse_onto(reversed, tail);
        }
        reversed = npl_cons(value, reversed);
    }
}

static NplVal quoted_datum(NplVal value)
{
    return npl_cons(npl_symbol("quote", 5), npl_cons(value, npl_nil()));
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

static NplVal read_hash_datum(NplDatumReader *reader)
{
    int code = reader_get(reader);
    if (code == 't' || code == 'f') {
        if (!datum_delimiter(reader_peek(reader)))
            read_error("invalid boolean literal");
        return code == 't' ? npl_true() : npl_false();
    }
    if (code == 'b' || code == 'B' || code == 'o' || code == 'O' ||
        code == 'd' || code == 'D' || code == 'x' || code == 'X')
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
    if (code == '#') return read_hash_datum(reader);
    return read_token_datum(reader, code);
}

NplVal npl_read_datum(NplVal port_value_arg)
{
    NplPort *port = npl_port_require_mode_internal(port_value_arg, NPL_PORT_INPUT,
                                     NPL_PORT_TEXTUAL, "read");
    NplDatumReader reader = { port, 0, 0 };
    NplVal value = read_datum_inner(&reader, 1);
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
    NplVal items[2];
    if (setjmp(recovery) == 0) {
        items[0] = npl_true();
        items[1] = npl_read_datum(port_value);
    } else {
        items[0] = npl_false();
        items[1] = npl_string(g_read_error_message,
                              (uint32_t)strlen(g_read_error_message));
    }
    g_read_recovery = previous;
    return npl_make_vector(2, items);
}

/* Datum output: display and write (R7RS 6.13.3), UTF-8 on textual ports.
 * See port.h for the module split. */

#include "port.h"
#include "unicode_tables.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define port_value npl_port_value_internal
#define require_port_mode npl_port_require_mode_internal
#define port_write_bytes npl_port_write_bytes_internal

static void output_bytes(NplPort *port, const char *bytes, size_t count)
{
    port_write_bytes(port, (const uint8_t *)bytes, count, "write");
}

static void output_text(NplPort *port, const char *text)
{
    output_bytes(port, text, strlen(text));
}

/* Write one character (a Unicode scalar value) as UTF-8. */
static void output_char(NplPort *port, int code)
{
    npl_port_write_char_internal(port, (uint32_t)code);
}

static void output_string(NplPort *port, const NplString *string)
{
    size_t length;
    int owned;
    const char *bytes = npl_string_utf8(string, &length, &owned);
    output_bytes(port, bytes, length);
    if (owned) free((void *)bytes);
}

static void output_integer(NplPort *port, long long value)
{
    char buffer[32];
    int length = snprintf(buffer, sizeof(buffer), "%lld", value);
    output_bytes(port, buffer, (size_t)length);
}

static void output_integer_value(NplPort *port, NplVal value)
{
    NplVal text = npl_number_to_string(value);
    npl_gc_root(&text);
    NplString *string = (NplString *)npl_header(text);
    output_bytes(port, string->data, string->length);  /* ASCII digits */
    npl_gc_unroot(&text);
}

static void display_to(NplPort *port, NplVal value)
{
    if (npl_is_fixnum(value)) {
        output_integer(port, (long long)npl_fixnum_value(value));
    } else if (npl_is_nil(value)) {
        output_text(port, "()");
    } else if (value == npl_true()) {
        output_text(port, "#t");
    } else if (value == npl_false()) {
        output_text(port, "#f");
    } else if (npl_is_char(value)) {
        output_char(port, (int)npl_char_value(value));
    } else if (npl_is_ptr(value)) {
        NplHeader *header = npl_header(value);
        switch (header->tag) {
        case BOX_PAIR: {
            NplPair *pair = (NplPair *)header;
            output_char(port, '(');
            display_to(port, pair->car);
            NplVal rest = pair->cdr;
            while (!npl_is_nil(rest) && npl_is_ptr(rest) &&
                   npl_box_tag(rest) == BOX_PAIR) {
                output_char(port, ' ');
                display_to(port, npl_car(rest));
                rest = npl_cdr(rest);
            }
            if (!npl_is_nil(rest)) {
                output_text(port, " . ");
                display_to(port, rest);
            }
            output_char(port, ')');
            break;
        }
        case BOX_STRING:
        case BOX_SYMBOL:
            output_string(port, (const NplString *)header);
            break;
        case BOX_FLOAT:
        case BOX_BIGNUM:
        case BOX_RATIONAL:
        case BOX_COMPLEX:
            output_integer_value(port, value);
            break;
        case BOX_RECORD: {
            /* The type descriptor is the record type's name (a symbol). */
            NplVal type = ((NplVector *)header)->data[0];
            output_text(port, "#<");
            display_to(port, type);
            output_char(port, '>');
            break;
        }
        case BOX_VECTOR: {
            NplVector *vector = (NplVector *)header;
            output_text(port, "#(");
            for (uint32_t i = 0; i < vector->length; i++) {
                if (i) output_char(port, ' ');
                display_to(port, vector->data[i]);
            }
            output_char(port, ')');
            break;
        }
        case BOX_BYTEVECTOR: {
            NplBytevector *vector = (NplBytevector *)header;
            output_text(port, "#u8(");
            for (uint32_t i = 0; i < vector->length; i++) {
                if (i) output_char(port, ' ');
                output_integer(port, vector->data[i]);
            }
            output_char(port, ')');
            break;
        }
        case BOX_VALUES: output_text(port, "#<multiple-values>"); break;
        case BOX_PROMISE: output_text(port, "#<promise>"); break;
        case BOX_PORT: output_text(port, "#<port>"); break;
        case BOX_EOF: output_text(port, "#<eof>"); break;
        default: output_text(port, "#<unknown>"); break;
        }
    } else {
        output_text(port, "#<unknown>");
    }
}

/* Characters write prints literally; the rest use hex escapes. As in Guile:
 * general categories Cc Cf Cs Co Cn Zs Zl Zp other than SPACE are
 * non-graphic (generated table, tools/gen_unicode.py). */
static int write_graphic(uint32_t code)
{
    size_t low = 0;
    size_t high = sizeof(npl_nongraphic_ranges) /
                  sizeof(npl_nongraphic_ranges[0]);
    while (low < high) {
        size_t middle = low + (high - low) / 2;
        if (code < npl_nongraphic_ranges[middle][0]) high = middle;
        else if (code > npl_nongraphic_ranges[middle][1]) low = middle + 1;
        else return 0;
    }
    return 1;
}

static void output_hex(NplPort *port, uint32_t code)
{
    char buffer[16];
    int length = snprintf(buffer, sizeof(buffer), "%x", (unsigned)code);
    output_bytes(port, buffer, (size_t)length);
}

static void write_escaped_string(NplPort *port, const NplString *string)
{
    output_char(port, '"');
    uint32_t count = npl_string_char_count_internal(string);
    for (uint32_t i = 0; i < count; i++) {
        uint32_t code = npl_string_char_at_internal(string, i);
        if (code == '"' || code == '\\') {
            output_char(port, '\\');
            output_char(port, (int)code);
        } else if (code == '\n') {
            output_text(port, "\\n");
        } else if (code == '\r') {
            output_text(port, "\\r");
        } else if (code == '\t') {
            output_text(port, "\\t");
        } else if (code == 7) {
            output_text(port, "\\a");
        } else if (code == 8) {
            output_text(port, "\\b");
        } else if (!write_graphic(code)) {
            output_text(port, "\\x");
            output_hex(port, code);
            output_char(port, ';');
        } else {
            output_char(port, (int)code);
        }
    }
    output_char(port, '"');
}

/* R7RS 6.6 character names, then #\xHH for other non-graphic characters. */
static void write_character(NplPort *port, uint32_t code)
{
    static const struct { uint32_t code; const char *name; } names[] = {
        { 7, "alarm" }, { 8, "backspace" }, { 0x7F, "delete" },
        { 0x1B, "escape" }, { 10, "newline" }, { 0, "nul" },
        { 13, "return" }, { 32, "space" }, { 9, "tab" },
    };
    /* U+0000 is written #\nul as Guile and Racket do (R7RS names it
     * #\null; read accepts both). */
    output_text(port, "#\\");
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        if (names[i].code == code) {
            output_text(port, names[i].name);
            return;
        }
    }
    if (write_graphic(code)) {
        output_char(port, (int)code);
    } else {
        output_char(port, 'x');
        output_hex(port, code);
    }
}

static void write_to(NplPort *port, NplVal value)
{
    if (npl_is_char(value)) {
        write_character(port, npl_char_value(value));
        return;
    }
    if (npl_is_ptr(value) && npl_header(value)->tag == BOX_STRING) {
        write_escaped_string(port, (NplString *)npl_header(value));
        return;
    }
    if (npl_is_ptr(value) && npl_header(value)->tag == BOX_PAIR) {
        NplPair *pair = (NplPair *)npl_header(value);
        output_char(port, '(');
        write_to(port, pair->car);
        NplVal rest = pair->cdr;
        while (!npl_is_nil(rest) && npl_is_ptr(rest) &&
               npl_box_tag(rest) == BOX_PAIR) {
            output_char(port, ' ');
            write_to(port, npl_car(rest));
            rest = npl_cdr(rest);
        }
        if (!npl_is_nil(rest)) {
            output_text(port, " . ");
            write_to(port, rest);
        }
        output_char(port, ')');
        return;
    }
    if (npl_is_ptr(value) && npl_header(value)->tag == BOX_VECTOR) {
        NplVector *vector = (NplVector *)npl_header(value);
        output_text(port, "#(");
        for (uint32_t i = 0; i < vector->length; i++) {
            if (i) output_char(port, ' ');
            write_to(port, vector->data[i]);
        }
        output_char(port, ')');
        return;
    }
    display_to(port, value);
}

NplVal npl_display(NplVal value)
{
    npl_runtime_init();
    npl_gc_root(&value);
    display_to(port_value(npl_current_output_port(), "display"), value);
    npl_gc_unroot(&value);
    return npl_nil();
}

NplVal npl_newline(void)
{
    npl_runtime_init();
    output_char(port_value(npl_current_output_port(), "newline"), '\n');
    return npl_nil();
}

NplVal npl_display_port(NplVal value, NplVal port_value_arg)
{
    NplPort *port = require_port_mode(port_value_arg, NPL_PORT_OUTPUT,
                                     NPL_PORT_TEXTUAL, "display");
    npl_gc_root(&value);
    npl_gc_root(&port_value_arg);
    display_to(port, value);
    npl_gc_unroot(&port_value_arg);
    npl_gc_unroot(&value);
    return npl_nil();
}

NplVal npl_write_port(NplVal value, NplVal port_value_arg)
{
    NplPort *port = require_port_mode(port_value_arg, NPL_PORT_OUTPUT,
                                     NPL_PORT_TEXTUAL, "write");
    npl_gc_root(&value);
    npl_gc_root(&port_value_arg);
    write_to(port, value);
    npl_gc_unroot(&port_value_arg);
    npl_gc_unroot(&value);
    return npl_nil();
}

NplVal npl_newline_port(NplVal port_value_arg)
{
    NplPort *port = require_port_mode(port_value_arg, NPL_PORT_OUTPUT,
                                     NPL_PORT_TEXTUAL, "newline");
    output_char(port, '\n');
    return npl_nil();
}

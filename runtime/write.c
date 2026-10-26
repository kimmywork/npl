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

/* write prints a symbol inside |...| unless its text is an R7RS 7.1.1
 * <identifier> other than |...|: an <initial> followed by <subsequent>s, or
 * a <peculiar identifier> (+ - ... +a -.b and so on), which by grammar can
 * never be a number. Non-ASCII graphic characters count as <initial>
 * (Guile and Racket print them unbarred; docs/design-l6.md). */
static int ascii_letter(uint32_t c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static int special_initial(uint32_t c)
{
    return c == '!' || c == '$' || c == '%' || c == '&' || c == '*' ||
           c == '/' || c == ':' || c == '<' || c == '=' || c == '>' ||
           c == '?' || c == '^' || c == '_' || c == '~';
}

static int identifier_initial(uint32_t c)
{
    return ascii_letter(c) || special_initial(c) ||
           (c >= 0x80 && write_graphic(c));
}

static int identifier_subsequent(uint32_t c)
{
    return identifier_initial(c) || (c >= '0' && c <= '9') ||
           c == '+' || c == '-' || c == '.' || c == '@';
}

static int sign_subsequent(uint32_t c)
{
    return identifier_initial(c) || c == '+' || c == '-' || c == '@';
}

static int symbol_needs_bars(const NplString *symbol)
{
    uint32_t count = npl_string_char_count_internal(symbol);
    if (count == 0) return 1;
    uint32_t first = npl_string_char_at_internal(symbol, 0);
    uint32_t i = 1;
    if (identifier_initial(first)) {
        /* ordinary identifier */
    } else if (first == '+' || first == '-') {
        if (count == 1) return 0;
        uint32_t second = npl_string_char_at_internal(symbol, 1);
        if (second == '.') {
            if (count == 2) return 1;
            uint32_t third = npl_string_char_at_internal(symbol, 2);
            if (!sign_subsequent(third) && third != '.') return 1;
            i = 3;
        } else {
            if (!sign_subsequent(second)) return 1;
            i = 2;
        }
    } else if (first == '.') {
        if (count == 1) return 1;
        uint32_t second = npl_string_char_at_internal(symbol, 1);
        if (!sign_subsequent(second) && second != '.') return 1;
        i = 2;
    } else {
        return 1;
    }
    for (; i < count; i++)
        if (!identifier_subsequent(npl_string_char_at_internal(symbol, i)))
            return 1;
    /* The grammar's <peculiar identifier> overlaps the number syntax
     * (+i, -inf.0, +nan.0): such text reads back as a number. */
    if (first == '+' || first == '-' || first == '.') {
        size_t length;
        int owned;
        const char *bytes = npl_string_utf8(symbol, &length, &owned);
        NplVal text = npl_string(bytes, (uint32_t)length);
        if (owned) free((void *)bytes);
        if (npl_number_from_string(text) != npl_false()) return 1;
    }
    return 0;
}

static void write_symbol(NplPort *port, const NplString *symbol)
{
    if (!symbol_needs_bars(symbol)) {
        output_string(port, symbol);
        return;
    }
    output_char(port, '|');
    uint32_t count = npl_string_char_count_internal(symbol);
    for (uint32_t i = 0; i < count; i++) {
        uint32_t c = npl_string_char_at_internal(symbol, i);
        if (c == '|' || c == '\\') {
            output_char(port, '\\');
            output_char(port, (int)c);
        } else if (!write_graphic(c) && c != ' ') {
            output_text(port, "\\x");
            output_hex(port, c);
            output_char(port, ';');
        } else {
            output_char(port, (int)c);
        }
    }
    output_char(port, '|');
}

/* ---- datum labels for output (R7RS 2.4, 6.13.3) ----
 * A pre-pass over pairs and vectors marks the objects that need labels:
 * for write and display, objects on a cycle (reached again while still on
 * the depth-first path); for write-shared, every object reached more than
 * once. write-simple skips the pass. Labels are numbered from 0 in output
 * order (R7RS 2.4's example; Racket). The table holds raw addresses and
 * allocates no managed objects, so printing cannot move anything. */

typedef struct {
    NplVal key;
    uint32_t state;   /* pre-pass: 1 on path, 2 done; printing: label + 1 */
    uint32_t marked;  /* needs a label */
} NplLabelSlot;

typedef struct {
    NplLabelSlot *slots;
    size_t capacity;
    size_t count;
    uint32_t next_label;
    int any;
} NplLabels;

static NplLabelSlot *label_slot(NplLabels *labels, NplVal key, int create)
{
    if (create && (labels->count + 1) * 2 > labels->capacity) {
        size_t capacity = labels->capacity ? labels->capacity * 2 : 64;
        NplLabelSlot *slots = (NplLabelSlot *)calloc(capacity, sizeof(*slots));
        if (!slots) npl_runtime_oom_internal();
        for (size_t i = 0; i < labels->capacity; i++) {
            if (!labels->slots[i].key) continue;
            size_t k = (size_t)(labels->slots[i].key >> 3) & (capacity - 1);
            while (slots[k].key) k = (k + 1) & (capacity - 1);
            slots[k] = labels->slots[i];
        }
        free(labels->slots);
        labels->slots = slots;
        labels->capacity = capacity;
    }
    if (!labels->capacity) return NULL;
    size_t k = (size_t)(key >> 3) & (labels->capacity - 1);
    while (labels->slots[k].key) {
        if (labels->slots[k].key == key) return &labels->slots[k];
        k = (k + 1) & (labels->capacity - 1);
    }
    if (!create) return NULL;
    labels->slots[k].key = key;
    labels->count++;
    return &labels->slots[k];
}

static int labelled_kind(NplVal value)
{
    if (!npl_is_ptr(value)) return 0;
    uint32_t tag = npl_header(value)->tag;
    return tag == BOX_PAIR || tag == BOX_VECTOR;
}

/* Returns 1 when value was already visited (and marks it as needed). */
static int visit(NplLabels *labels, NplVal value, int shared)
{
    NplLabelSlot *slot = label_slot(labels, value, 1);
    if (!slot->state) {
        slot->state = 1;
        return 0;
    }
    if (shared || slot->state == 1) {
        slot->marked = 1;
        labels->any = 1;
    }
    return 1;
}

static void finish(NplLabels *labels, NplVal value)
{
    label_slot(labels, value, 0)->state = 2;
}

/* Depth-first over cars and vector elements; a list's cdr chain is walked
 * iteratively and all its pairs stay on the path until the chain ends. */
static void find_labels(NplLabels *labels, NplVal value, int shared)
{
    if (!labelled_kind(value) || visit(labels, value, shared)) return;
    if (npl_header(value)->tag == BOX_VECTOR) {
        NplVector *vector = (NplVector *)npl_header(value);
        for (uint32_t i = 0; i < vector->length; i++)
            find_labels(labels, vector->data[i], shared);
        finish(labels, value);
        return;
    }
    NplVal pair = value;
    for (;;) {
        find_labels(labels, npl_pair_ptr(pair)->car, shared);
        NplVal next = npl_pair_ptr(pair)->cdr;
        if (!labelled_kind(next)) break;
        if (npl_header(next)->tag == BOX_VECTOR) {
            find_labels(labels, next, shared);
            break;
        }
        if (visit(labels, next, shared)) break;
        pair = next;
    }
    for (NplVal p = value;; p = npl_pair_ptr(p)->cdr) {
        finish(labels, p);
        if (p == pair) break;
    }
}

typedef struct {
    NplPort *port;
    int write;          /* write syntax: quoted strings, #\ characters */
    NplLabels *labels;  /* NULL for write-simple */
} NplPrinter;

static void print_datum(NplPrinter *printer, NplVal value);

static NplLabelSlot *marked_slot(NplPrinter *printer, NplVal value)
{
    if (!printer->labels || !printer->labels->any || !labelled_kind(value))
        return NULL;
    NplLabelSlot *slot = label_slot(printer->labels, value, 0);
    return slot && slot->marked ? slot : NULL;
}

static void output_label(NplPort *port, uint32_t label, char suffix)
{
    char buffer[24];
    int length = snprintf(buffer, sizeof(buffer), "#%u%c", (unsigned)label,
                          suffix);
    output_bytes(port, buffer, (size_t)length);
}

static void print_pair(NplPrinter *printer, NplVal value)
{
    NplPort *port = printer->port;
    output_char(port, '(');
    print_datum(printer, npl_pair_ptr(value)->car);
    NplVal rest = npl_pair_ptr(value)->cdr;
    /* A labelled cdr is printed in dotted form so its label has a place. */
    while (npl_is_ptr(rest) && npl_box_tag(rest) == BOX_PAIR &&
           !marked_slot(printer, rest)) {
        output_char(port, ' ');
        print_datum(printer, npl_pair_ptr(rest)->car);
        rest = npl_pair_ptr(rest)->cdr;
    }
    if (!npl_is_nil(rest)) {
        output_text(port, " . ");
        print_datum(printer, rest);
    }
    output_char(port, ')');
}

static void print_datum(NplPrinter *printer, NplVal value)
{
    NplPort *port = printer->port;
    NplLabelSlot *slot = marked_slot(printer, value);
    if (slot) {
        if (slot->state > 2) {
            output_label(port, slot->state - 3, '#');
            return;
        }
        slot->state = 3 + printer->labels->next_label++;
        output_label(port, slot->state - 3, '=');
    }
    if (npl_is_fixnum(value)) {
        output_integer(port, (long long)npl_fixnum_value(value));
    } else if (npl_is_nil(value)) {
        output_text(port, "()");
    } else if (value == npl_true()) {
        output_text(port, "#t");
    } else if (value == npl_false()) {
        output_text(port, "#f");
    } else if (value == npl_unspecified()) {
        output_text(port, "#<unspecified>");
    } else if (npl_is_char(value)) {
        if (printer->write) write_character(port, npl_char_value(value));
        else output_char(port, (int)npl_char_value(value));
    } else if (npl_is_ptr(value)) {
        NplHeader *header = npl_header(value);
        switch (header->tag) {
        case BOX_PAIR:
            print_pair(printer, value);
            break;
        case BOX_STRING:
            if (printer->write)
                write_escaped_string(port, (const NplString *)header);
            else
                output_string(port, (const NplString *)header);
            break;
        case BOX_SYMBOL:
            if (printer->write) write_symbol(port, (const NplString *)header);
            else output_string(port, (const NplString *)header);
            break;
        case BOX_FLOAT:
        case BOX_BIGNUM:
        case BOX_RATIONAL:
        case BOX_COMPLEX:
            output_integer_value(port, value);
            break;
        case BOX_RECORD: {
            /* The type descriptor is the record type's name. */
            NplVal type = ((NplVector *)header)->data[0];
            output_text(port, "#<");
            NplPrinter plain = { port, 0, NULL };
            print_datum(&plain, type);
            output_char(port, '>');
            break;
        }
        case BOX_VECTOR: {
            NplVector *vector = (NplVector *)header;
            output_text(port, "#(");
            for (uint32_t i = 0; i < vector->length; i++) {
                if (i) output_char(port, ' ');
                print_datum(printer, vector->data[i]);
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
        case BOX_POINTER: {
            char text[40];
            snprintf(text, sizeof text, "#<pointer 0x%llx>",
                     (unsigned long long)(uintptr_t)
                         ((NplPointer *)header)->address);
            output_text(port, text);
            break;
        }
        default: output_text(port, "#<unknown>"); break;
        }
    } else {
        output_text(port, "#<unknown>");
    }
}

/* mode: 0 display, 1 write, 2 write-shared, 3 write-simple. */
static void print_value(NplPort *port, NplVal value, int mode)
{
    NplLabels labels = { NULL, 0, 0, 0, 0 };
    NplPrinter printer = { port, mode != 0, mode == 3 ? NULL : &labels };
    if (mode != 3) find_labels(&labels, value, mode == 2);
    print_datum(&printer, value);
    free(labels.slots);
}

static NplVal print_to_port(NplVal value, NplVal port_value_arg, int mode,
                            const char *who)
{
    NplPort *port = require_port_mode(port_value_arg, NPL_PORT_OUTPUT,
                                     NPL_PORT_TEXTUAL, who);
    npl_gc_root(&value);
    npl_gc_root(&port_value_arg);
    print_value(port, value, mode);
    npl_gc_unroot(&port_value_arg);
    npl_gc_unroot(&value);
    return npl_unspecified();
}

NplVal npl_display(NplVal value)
{
    npl_runtime_init();
    return print_to_port(value, npl_current_output_port(), 0, "display");
}

NplVal npl_display_port(NplVal value, NplVal port)
{
    return print_to_port(value, port, 0, "display");
}

NplVal npl_write_port(NplVal value, NplVal port)
{
    return print_to_port(value, port, 1, "write");
}

NplVal npl_write_shared_port(NplVal value, NplVal port)
{
    return print_to_port(value, port, 2, "write-shared");
}

NplVal npl_write_simple_port(NplVal value, NplVal port)
{
    return print_to_port(value, port, 3, "write-simple");
}

NplVal npl_newline(void)
{
    npl_runtime_init();
    output_char(port_value(npl_current_output_port(), "newline"), '\n');
    return npl_unspecified();
}


NplVal npl_newline_port(NplVal port_value_arg)
{
    NplPort *port = require_port_mode(port_value_arg, NPL_PORT_OUTPUT,
                                     NPL_PORT_TEXTUAL, "newline");
    output_char(port, '\n');
    return npl_unspecified();
}

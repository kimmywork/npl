#include "read-internal.h"
#include <string.h>

/* Number of elements in a proper list, or -1 if it is improper. */
static int directive_count(NplVal list)
{
    int count = 0;
    while (npl_is_ptr(list) && npl_header(list)->tag == BOX_PAIR) {
        count++;
        list = npl_cdr(list);
    }
    return npl_is_nil(list) ? count : -1;
}

static int directive_symbol_name(NplVal value, char *buffer, size_t size)
{
    if (!npl_is_ptr(value) || npl_header(value)->tag != BOX_SYMBOL) return 0;
    const NplString *symbol = (const NplString *)npl_header(value);
    uint32_t count = npl_string_char_count_internal(symbol);
    if ((size_t)count + 1 >= size) return 0;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t code = npl_string_char_at_internal(symbol, i);
        if (code >= 0x80) return 0;
        buffer[i] = (char)code;
    }
    buffer[count] = '\0';
    return 1;
}

/* One registry entry for simple and parenthesized directives.  Arguments
 * are datums; current entries take identifiers or none. */
static void directive_apply(NplDatumReader *reader, const char *name,
                            NplVal args)
{
    int count = directive_count(args);
    if (count < 0) npl_read_error("malformed directive");

    if (strcmp(name, "fold-case") == 0) {
        if (count != 0) npl_read_error("directive takes no arguments");
        reader->fold_case = 1;
        reader->port->read_fold_case = 1;
    } else if (strcmp(name, "no-fold-case") == 0) {
        if (count != 0) npl_read_error("directive takes no arguments");
        reader->fold_case = 0;
        reader->port->read_fold_case = 0;
    } else if (strcmp(name, "curly-infix") == 0) {
        if (!(count == 0 ||
              (count == 1 && npl_symbol_named(npl_car(args), "neoteric"))))
            npl_read_error("curly-infix directive takes no arguments or neoteric");
        reader->neoteric = 1;
        reader->port->read_neoteric = 1;
    } else if (strcmp(name, "srfi-88") == 0 ||
               strcmp(name, "colon-keyword") == 0) {
        if (count != 0) npl_read_error("directive takes no arguments");
        reader->keyword_colon = 1;
        reader->port->read_keyword_colon = 1;
    } else {
        npl_read_error("unknown directive");
    }
}

static void read_parameterized_directive(NplDatumReader *reader)
{
    (void)npl_reader_get(reader);   /* ( */
    int saved = reader->in_directive;
    reader->in_directive = 1;
    NplVal list = npl_read_list_datum(reader, '(');
    reader->in_directive = saved;
    if (directive_count(list) < 1) npl_read_error("malformed directive");
    char name[32];
    if (!directive_symbol_name(npl_car(list), name, sizeof(name)))
        npl_read_error("malformed directive");
    directive_apply(reader, name, npl_cdr(list));
}

/* #!directive or #!(name args...) at a datum boundary. */
void npl_read_directive(NplDatumReader *reader)
{
    if (npl_reader_peek(reader) == '(') {
        read_parameterized_directive(reader);
        return;
    }
    char name[32];
    size_t length = 0;
    while (!npl_datum_delimiter(npl_reader_peek(reader))) {
        int code = npl_reader_get(reader);
        if (length + 1 >= sizeof(name)) npl_read_error("unknown directive");
        name[length++] = (char)code;
    }
    name[length] = '\0';
    directive_apply(reader, name, npl_nil());
}

/* Skips whitespace and comments: line (;), block (#|...|#), datum (#;),
 * and #! directives. A '#' that starts none of these is pushed back as the
 * reader's lookahead pair: reader->pending_hash. */

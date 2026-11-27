#include "read-internal.h"

/* Cyclic operators cannot be compared structurally forever; fall back to
 * treating excessively deep comparisons as unequal ($nfx$). */
#define CURLY_EQUAL_MAX_DEPTH 1024

static int curly_equal_depth(NplVal left, NplVal right, int depth)
{
    if (depth > CURLY_EQUAL_MAX_DEPTH) return 0;
    if (left == right) return 1;
    if (!npl_is_ptr(left) || !npl_is_ptr(right))
        return npl_eqv(left, right) == npl_true();
    NplHeader *a = npl_header(left);
    NplHeader *b = npl_header(right);
    if (a->tag != b->tag) return 0;
    switch (a->tag) {
    case BOX_PAIR:
        return curly_equal_depth(npl_car(left), npl_car(right), depth + 1) &&
               curly_equal_depth(npl_cdr(left), npl_cdr(right), depth + 1);
    case BOX_VECTOR: {
        NplVector *va = (NplVector *)a;
        NplVector *vb = (NplVector *)b;
        if (va->length != vb->length) return 0;
        for (uint32_t i = 0; i < va->length; i++)
            if (!curly_equal_depth(va->data[i], vb->data[i], depth + 1))
                return 0;
        return 1;
    }
    case BOX_STRING:
    case BOX_SYMBOL:
    case BOX_KEYWORD:
        return npl_string_same_chars_internal(
            (const NplString *)a, (const NplString *)b);
    default:
        return npl_eqv(left, right) == npl_true();
    }
}

static int curly_equal(NplVal left, NplVal right)
{
    return curly_equal_depth(left, right, 0);
}

static NplVal neoteric_suffix(NplDatumReader *reader, NplVal value);

static NplVal curly_operands(NplVal items, NplVal reversed)
{
    if (npl_is_nil(items)) return npl_reverse_onto(reversed, npl_nil());
    NplVal rest = npl_cdr(items);
    if (npl_is_nil(rest))
        return npl_reverse_onto(npl_cons(npl_car(items), reversed), npl_nil());
    return curly_operands(npl_cdr(rest),
                          npl_cons(npl_car(items), reversed));
}

NplVal npl_read_curly_infix(NplDatumReader *reader)
{
    NplVal reversed = npl_nil();
    int64_t count = 0;
    npl_gc_root(&reversed);
    for (;;) {
        npl_safepoint_poll();
        npl_reader_skip_space(reader);
        int code = npl_reader_peek(reader);
        if (code == EOF) npl_read_error("unexpected EOF in curly-infix list");
        if (code == '}') {
            (void)npl_reader_get(reader);
            break;
        }
        if (code == '.') {
            (void)npl_reader_get(reader);
            if (npl_datum_delimiter(npl_reader_peek(reader))) {
                npl_reader_skip_space(reader);
                NplVal tail = npl_read_datum_inner(reader, 0);
                npl_gc_root(&tail);
                npl_reader_skip_space(reader);
                if (npl_reader_peek(reader) != '}')
                    npl_read_error("expected } after dotted curly-infix datum");
                (void)npl_reader_get(reader);
                NplVal items = npl_reverse_onto(reversed, tail);
                npl_gc_unroot(&tail);
                npl_gc_unroot(&reversed);
                return npl_cons(npl_symbol("$nfx$", 5), items);
            }
            NplVal item = neoteric_suffix(
                reader, npl_read_token_datum(reader, '.'));
            reversed = npl_cons(item, reversed);
            count++;
            continue;
        }
        NplVal item = npl_read_datum_inner(reader, 0);
        reversed = npl_cons(item, reversed);
        count++;
    }
    NplVal items = npl_reverse_onto(reversed, npl_nil());
    npl_gc_unroot(&reversed);
    if (count == 0) return npl_nil();
    if (count == 1) return npl_car(items);
    if (count == 2)
        return npl_cons(npl_car(items),
                        npl_cons(npl_car(npl_cdr(items)), npl_nil()));
    if ((count % 2) == 0)
        return npl_cons(npl_symbol("$nfx$", 5), items);

    NplVal operator = npl_car(npl_cdr(items));
    NplVal rest = npl_cdr(npl_cdr(items));
    while (!npl_is_nil(rest)) {
        rest = npl_cdr(rest);        /* skip the operand */
        if (!npl_is_nil(rest)) {
            if (!curly_equal(operator, npl_car(rest)))
                return npl_cons(npl_symbol("$nfx$", 5), items);
            rest = npl_cdr(rest);    /* skip the next operator */
        }
    }
    return npl_cons(operator, curly_operands(items, npl_nil()));
}

static NplVal neoteric_suffix(NplDatumReader *reader, NplVal value)
{
    if (!reader->neoteric) return value;
    for (;;) {
        int next = npl_reader_peek(reader);
        if (next == '(') {
            (void)npl_reader_get(reader);
            npl_reader_skip_space(reader);
            NplVal args;
            if (npl_reader_peek(reader) == '.') {
                (void)npl_reader_get(reader);
                if (npl_datum_delimiter(npl_reader_peek(reader))) {
                    /* e(. rest) is the improper call (e . rest). */
                    npl_reader_skip_space(reader);
                    NplVal tail = npl_read_datum_inner(reader, 0);
                    npl_reader_skip_space(reader);
                    if (npl_reader_peek(reader) != ')')
                        npl_read_error(
                            "expected ) after dotted neoteric argument");
                    (void)npl_reader_get(reader);
                    value = npl_cons(value, tail);
                    continue;
                }
                NplVal first = neoteric_suffix(
                    reader, npl_read_token_datum(reader, '.'));
                args = npl_cons(first, npl_read_list_datum(reader, '('));
            } else {
                args = npl_read_list_datum(reader, '(');
            }
            value = npl_cons(value, args);
        } else if (next == '[') {
            (void)npl_reader_get(reader);
            NplVal args = npl_read_list_datum(reader, '[');
            value = npl_cons(npl_symbol("$bracket-apply$", 15),
                             npl_cons(value, args));
        } else if (next == '{') {
            (void)npl_reader_get(reader);
            NplVal content = npl_read_curly_infix(reader);
            value = npl_is_nil(content)
                ? npl_cons(value, npl_nil())
                : npl_cons(value, npl_cons(content, npl_nil()));
        } else {
            break;
        }
    }
    return value;
}

NplVal npl_read_datum_inner(NplDatumReader *reader, int allow_eof)
{
    return neoteric_suffix(reader, npl_read_datum_plain(reader, allow_eof));
}

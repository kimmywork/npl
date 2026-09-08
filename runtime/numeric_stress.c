/* Focused exact-integer substrate checks, independent of compiler lowering. */
#include "npl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;

static void check(int condition, const char *name)
{
    checks++;
    if (!condition) {
        fprintf(stderr, "numeric stress: FAIL %s\n", name);
        exit(1);
    }
}

static NplVal parse(const char *text)
{
    return npl_integer_from_string(npl_string(text, (uint32_t)strlen(text)));
}

static void expect_value(NplVal value, const char *expected)
{
    NplVal text = npl_number_to_string(value);
    const NplString *actual = (const NplString *)npl_header(text);
    check(actual->length == strlen(expected) &&
          memcmp(actual->data, expected, actual->length) == 0, expected);
}

static void expect_text(const char *input, const char *expected, int big)
{
    NplVal value = parse(input);
    npl_gc_root(&value);
    expect_value(value, expected);
    check(npl_is_fixnum(value) == !big, "canonical representation");
    check(npl_number_p(value) == npl_true(), "number predicate");
    npl_gc_unroot(&value);
}

static void boundaries(void)
{
    expect_text("0", "0", 0);
    expect_text("-000", "0", 0);
    expect_text("+00042", "42", 0);
    expect_text("2305843009213693951", "2305843009213693951", 0);
    expect_text("2305843009213693952", "2305843009213693952", 1);
    expect_text("-2305843009213693952", "-2305843009213693952", 0);
    expect_text("-2305843009213693953", "-2305843009213693953", 1);
    expect_text("18446744073709551616", "18446744073709551616", 1);
    expect_text("999999999999999999999999999", "999999999999999999999999999", 1);
    expect_text("1234567890123456789012345678901234567890",
                "1234567890123456789012345678901234567890", 1);
    const char *invalid[] = { "", "+", "-", "1x", " 1", "1.0", "1/2", "#x10" };
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++)
        check(npl_integer_string_p(npl_string(invalid[i],
              (uint32_t)strlen(invalid[i]))) == npl_false(), "invalid decimal");
    check(npl_integer_string_p(npl_string("1\0x", 3)) == npl_false(), "embedded NUL");
    check(npl_integer_string_p(npl_true()) == npl_false(), "non-string");
    check(npl_number_p(npl_string("42", 2)) == npl_false(), "non-number");

    const char *decimal = "x+10000000000000000000000000000000000000000y";
    NplVal source = npl_string(decimal, (uint32_t)strlen(decimal));
    npl_gc_root(&source);
    NplVal scanned = npl_lex_number(source, npl_fixnum(1),
                                    npl_fixnum((int64_t)strlen(decimal) - 1));
    npl_gc_root(&scanned);
    expect_value(scanned, "10000000000000000000000000000000000000000");
    check(npl_eqv(scanned, parse("10000000000000000000000000000000000000000")) == npl_true(), "numeric eqv across allocations");
    check(npl_eqv(scanned, npl_fixnum(42)) == npl_false(), "numeric eqv unequal");
    npl_gc_unroot(&scanned);
    npl_gc_unroot(&source);

    NplVal max = npl_fixnum(INT64_C(2305843009213693951));
    NplVal min = npl_fixnum(-INT64_C(2305843009213693952));
    expect_value(npl_number_mul(min, npl_fixnum(1)), "-2305843009213693952");
    expect_value(npl_number_mul(min, npl_fixnum(-1)), "2305843009213693952");
    check(npl_number_mul(min, npl_fixnum(0)) == npl_fixnum(0), "multiply min by zero");
    NplVal promoted = npl_number_add(max, npl_fixnum(1));
    npl_gc_root(&promoted);
    expect_value(promoted, "2305843009213693952");
    check(npl_number_sub(promoted, npl_fixnum(1)) == max, "demotion");
    expect_value(npl_number_sub(min, npl_fixnum(1)), "-2305843009213693953");
    expect_value(npl_number_sub(npl_fixnum(0), min), "2305843009213693952");
    expect_value(npl_number_mul(promoted, promoted),
                 "5316911983139663491615228241121378304");
    npl_gc_unroot(&promoted);
}

static void generated(void)
{
    uint32_t seed = 12345;
    for (unsigned i = 0; i < 2000; i++) {
        seed = seed * UINT32_C(1664525) + UINT32_C(1013904223);
        int64_t a = (int64_t)(seed % 2000000001u) - 1000000000;
        seed = seed * UINT32_C(1664525) + UINT32_C(1013904223);
        int64_t b = (int64_t)(seed % 2000000001u) - 1000000000;
        check(npl_number_add(npl_fixnum(a), npl_fixnum(b)) == npl_fixnum(a + b), "generated add");
        check(npl_number_sub(npl_fixnum(a), npl_fixnum(b)) == npl_fixnum(a - b), "generated subtract");
        check(npl_number_mul(npl_fixnum(a), npl_fixnum(b)) == npl_fixnum(a * b), "generated multiply");
        check(npl_number_compare(npl_fixnum(a), npl_fixnum(b)) == ((a > b) - (a < b)), "generated compare");
    }
    for (unsigned i = 0; i < 64; i++) {
        char digits[403];
        size_t length = 40 + i * 5;
        digits[0] = i % 2 ? '-' : '+';
        digits[1] = '1';
        for (size_t j = 2; j <= length; j++) {
            seed = seed * UINT32_C(1664525) + UINT32_C(1013904223);
            digits[j] = (char)('0' + seed % 10);
        }
        digits[length + 1] = '\0';
        NplVal a = parse(digits);
        NplVal b = parse("999999999999999999999999999999999999");
        NplVal sum = npl_nil(), product = npl_nil(), negated = npl_nil();
        npl_gc_root(&a);
        npl_gc_root(&b);
        npl_gc_root(&sum);
        npl_gc_root(&product);
        npl_gc_root(&negated);
        sum = npl_number_add(a, b);
        check(npl_number_compare(npl_number_sub(sum, b), a) == 0, "large add/sub inverse");
        negated = npl_number_sub(npl_fixnum(0), a);
        check(npl_number_add(a, negated) == npl_fixnum(0), "large cancellation");
        product = npl_number_mul(a, npl_fixnum(2));
        check(npl_number_compare(product, npl_number_add(a, a)) == 0, "large double");
        npl_gc_collect();
        expect_value(a, digits[0] == '+' ? digits + 1 : digits);
        check(npl_number_compare(npl_number_sub(product, a), a) == 0, "GC survival");
        npl_gc_unroot(&negated);
        npl_gc_unroot(&product);
        npl_gc_unroot(&sum);
        npl_gc_unroot(&b);
        npl_gc_unroot(&a);
    }
}

static void ports(void)
{
    const char *expected = "1234567890123456789012345678901234567890";
    NplVal input = npl_open_input_string(npl_string(expected, (uint32_t)strlen(expected)));
    npl_gc_root(&input);
    NplVal value = npl_read_datum(input);
    npl_gc_root(&value);
    expect_value(value, expected);
    NplVal output = npl_open_output_string();
    npl_gc_root(&output);
    npl_write_port(value, output);
    npl_write_port(value, output);
    NplVal text = npl_get_output_string(output);
    const NplString *actual = (const NplString *)npl_header(text);
    check(actual->length == 80 &&
          memcmp(actual->data, expected, 40) == 0 &&
          memcmp(actual->data + 40, expected, 40) == 0, "writer buffer growth");
    npl_close_port(input);
    npl_close_port(output);
    npl_gc_unroot(&output);
    npl_gc_unroot(&value);
    npl_gc_unroot(&input);
}

int main(void)
{
    npl_runtime_init();
    boundaries();
    generated();
    ports();
    printf("numeric substrate: PASS (%u checks)\n", checks);
    return 0;
}

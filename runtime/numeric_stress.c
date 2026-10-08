/* Focused exact-integer substrate checks, independent of compiler lowering. */
#include "npl.h"
#include <math.h>
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

    expect_value(npl_number_quotient(parse("12345678901234567890"),
                                    parse("1234567890")), "10000000001");
    expect_value(npl_number_remainder(parse("12345678901234567890"),
                                      parse("1234567890")), "0");
    expect_value(npl_number_quotient(parse("-17"), parse("5")), "-3");
    expect_value(npl_number_remainder(parse("-17"), parse("5")), "-2");
    expect_value(npl_number_gcd(parse("-12345678901234567890"),
                                parse("9876543210")), "90");

    NplVal max = npl_fixnum(INT64_C(2305843009213693951));
    NplVal min = npl_fixnum(-INT64_C(2305843009213693952));
    expect_value(npl_number_quotient(npl_fixnum(17), npl_fixnum(-5)), "-3");
    expect_value(npl_number_remainder(npl_fixnum(17), npl_fixnum(-5)), "2");
    expect_value(npl_number_quotient(npl_fixnum(-17), npl_fixnum(-5)), "3");
    expect_value(npl_number_remainder(npl_fixnum(-17), npl_fixnum(-5)), "-2");
    expect_value(npl_number_quotient(min, npl_fixnum(-1)), "2305843009213693952");
    check(npl_number_remainder(min, npl_fixnum(-1)) == npl_fixnum(0), "min remainder");
    check(npl_number_gcd(npl_fixnum(0), npl_fixnum(0)) == npl_fixnum(0), "gcd zero");
    expect_value(npl_number_gcd(min, npl_fixnum(0)), "2305843009213693952");
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
        if (b != 0) {
            check(npl_number_quotient(npl_fixnum(a), npl_fixnum(b)) == npl_fixnum(a / b), "generated quotient");
            check(npl_number_remainder(npl_fixnum(a), npl_fixnum(b)) == npl_fixnum(a % b), "generated remainder");
        }
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
        if (i % 4 >= 2) b = npl_number_sub(npl_fixnum(0), b);
        product = npl_number_mul(a, b);
        check(npl_number_compare(npl_number_quotient(product, b), a) == 0, "large exact quotient");
        check(npl_number_remainder(product, b) == npl_fixnum(0), "large exact remainder");
        sum = npl_number_quotient(a, b);
        product = npl_number_mul(sum, b);
        negated = npl_number_remainder(a, b);
        product = npl_number_add(product, negated);
        check(npl_number_compare(product, a) == 0, "division reconstruction");
        check(npl_number_compare(negated, npl_fixnum(0)) == (i % 2 ? -1 : 1), "remainder sign");
        if (i % 2) negated = npl_number_sub(npl_fixnum(0), negated);
        if (i % 4 >= 2) b = npl_number_sub(npl_fixnum(0), b);
        check(npl_number_compare(negated, b) < 0, "remainder magnitude");
        product = npl_number_mul(a, b);
        product = npl_number_gcd(product, b);
        check(npl_number_compare(product, b) == 0, "large gcd");
        npl_gc_unroot(&negated);
        npl_gc_unroot(&product);
        npl_gc_unroot(&sum);
        npl_gc_unroot(&b);
        npl_gc_unroot(&a);
    }
}

static void inexact(void)
{
    NplVal half = npl_number_from_string(npl_string("0.5", 3));
    NplVal one_point_five = npl_number_from_string(npl_string("1.5", 3));
    NplVal inf = npl_number_from_string(npl_string("+inf.0", 6));
    NplVal neg_inf = npl_number_from_string(npl_string("-inf.0", 6));
    NplVal nan = npl_number_from_string(npl_string("+nan.0", 6));
    npl_gc_root(&half); npl_gc_root(&one_point_five); npl_gc_root(&inf);
    npl_gc_root(&neg_inf); npl_gc_root(&nan);
    check(npl_number_p(half) == npl_true(), "float number predicate");
    check(npl_inexact_p(half) == npl_true(), "inexact predicate");
    check(npl_inexact_p(npl_fixnum(1)) == npl_false(), "exact predicate");
    expect_value(half, "0.5");
    expect_value(one_point_five, "1.5");
    expect_value(inf, "+inf.0");
    expect_value(neg_inf, "-inf.0");
    expect_value(nan, "+nan.0");
    check(npl_finite_p(half) == npl_true(), "finite predicate");
    check(npl_finite_p(inf) == npl_false(), "infinite finite predicate");
    check(npl_nan_p(nan) == npl_true(), "nan predicate");
    check(npl_infinite_p(neg_inf) == npl_true(), "infinite predicate");
    check(npl_number_compare(npl_inexact_to_exact(half),
              npl_rational_make(npl_fixnum(1), npl_fixnum(2))) == 0,
          "inexact to exact half");
    NplVal third = npl_rational_make(npl_fixnum(1), npl_fixnum(3));
    npl_gc_root(&third);
    NplVal third_float = npl_exact_to_inexact(third);
    npl_gc_root(&third_float);
    check(npl_inexact_p(third_float) == npl_true(), "rational to inexact");
    check(npl_number_compare(third_float, third) != 0, "mixed rational compare");
    check(npl_inexact_eqv(half, npl_exact_to_inexact(
              npl_rational_make(npl_fixnum(1), npl_fixnum(2)))) == npl_true(),
          "inexact eqv");
    check(npl_eqv(half, npl_fixnum(1)) == npl_false(), "exact inexact eqv");
    check(npl_number_compare(npl_number_add(half, one_point_five), npl_fixnum(2)) == 0,
          "float add");
    check(npl_number_compare(npl_number_mul(half, npl_fixnum(4)), npl_fixnum(2)) == 0,
          "mixed multiply");
    check(npl_number_compare(inf, npl_fixnum(100)) > 0, "infinity compare");
    check(npl_number_compare(neg_inf, npl_fixnum(-100)) < 0, "negative infinity compare");
    check(npl_number_compare(nan, npl_fixnum(0)) == 2, "nan unordered");
    npl_gc_collect();
    expect_value(third_float, "0.3333333333333333");
    npl_gc_unroot(&third_float); npl_gc_unroot(&third);
    npl_gc_unroot(&nan); npl_gc_unroot(&neg_inf); npl_gc_unroot(&inf);
    npl_gc_unroot(&one_point_five); npl_gc_unroot(&half);
}

static void expect_inverse_branch(int operation, double real, double imag,
                                  double expected_real, double expected_imag)
{
    NplVal real_part = npl_float(real);
    npl_gc_root(&real_part);
    NplVal imag_part = npl_float(imag);
    npl_gc_root(&imag_part);
    NplVal input = npl_complex_make(real_part, imag_part);
    npl_gc_root(&input);
    npl_gc_unroot(&imag_part);
    npl_gc_unroot(&real_part);
    NplVal result = npl_inexact_op(npl_fixnum(operation), input);
    npl_gc_root(&result);
    check(npl_complex_p(result) == npl_true(), "inverse branch complex result");
    double actual_real = npl_real_double(npl_complex_real(result));
    double actual_imag = npl_real_double(npl_complex_imag(result));
    check(fabs(actual_real - expected_real) < 0.000000000001,
          "inverse branch real part");
    check(fabs(actual_imag - expected_imag) < 0.000000000001,
          operation == 11
              ? (real > 0 ? "asin positive branch" : "asin negative branch")
              : (real > 0 ? "acos positive branch" : "acos negative branch"));
    npl_gc_unroot(&result);
    npl_gc_unroot(&input);
}

static void expect_atan(double y, double x, double expected)
{
    NplVal result = npl_inexact_binary(npl_fixnum(0), npl_float(y),
                                       npl_float(x));
    npl_gc_root(&result);
    double actual = npl_real_double(result);
    check(fabs(actual - expected) < 0.000000000001, "atan boundary value");
    check(signbit(actual) == signbit(expected), "atan boundary zero sign");
    npl_gc_unroot(&result);
}

static void branches(void)
{
    const double pi = 3.14159265358979323846;
    const double pi_over_two = 1.57079632679489661923;
    const double height = log(2.0 + sqrt(3.0));
    for (int side = 0; side < 2; side++) {
        double imag = side ? -0.0 : 0.0;
        expect_inverse_branch(11, 2.0, imag, pi_over_two, -height);
        expect_inverse_branch(12, 2.0, imag, 0.0, height);
        expect_inverse_branch(11, -2.0, imag, -pi_over_two, height);
        expect_inverse_branch(12, -2.0, imag, pi, -height);
    }

    NplVal unary = npl_inexact_op(npl_fixnum(16), npl_float(-0.0));
    npl_gc_root(&unary);
    check(signbit(npl_real_double(unary)), "unary atan negative zero");
    npl_gc_unroot(&unary);

    expect_atan(0.0, 1.0, 0.0);
    expect_atan(-0.0, 1.0, -0.0);
    expect_atan(0.0, -1.0, pi);
    expect_atan(-0.0, -1.0, -pi);
    expect_atan(0.0, 0.0, 0.0);
    expect_atan(-0.0, 0.0, -0.0);
    expect_atan(0.0, -0.0, pi);
    expect_atan(-0.0, -0.0, -pi);
}

static NplVal fraction(const char *numerator, const char *denominator);

/* Rectangular construction canonicalizes exact-zero imaginary parts and
 * mixed exactness, including results of exact complex arithmetic. */
static void rectangular(void)
{
    NplVal i = npl_complex_make(npl_fixnum(0), npl_fixnum(1));
    npl_gc_root(&i);
    NplVal value = npl_number_mul(i, i);
    npl_gc_root(&value);
    check(value == npl_fixnum(-1), "exact i*i demotes to -1");
    value = npl_number_sub(i, i);
    check(value == npl_fixnum(0), "exact i-i demotes to 0");
    value = npl_complex_make(npl_fixnum(3), npl_fixnum(0));
    check(value == npl_fixnum(3), "exact zero imaginary demotes");
    NplVal half = fraction("1", "2");
    npl_gc_root(&half);
    value = npl_complex_make(half, npl_fixnum(0));
    check(value == half, "rational with exact zero imaginary demotes");
    value = npl_float(1.5);
    value = npl_complex_make(value, npl_fixnum(0));
    check(npl_is_inexact(value) && npl_real_double(value) == 1.5,
          "inexact real with exact zero imaginary demotes");
    value = npl_float(0.0);
    value = npl_complex_make(npl_fixnum(3), value);
    npl_gc_collect();
    check(npl_complex_p(value) == npl_true(), "inexact zero imaginary stays complex");
    check(npl_is_inexact(npl_complex_real(value)) &&
          npl_real_double(npl_complex_real(value)) == 3.0,
          "mixed exactness coerces real part");
    value = npl_float(2.0);
    value = npl_complex_make(value, half);
    npl_gc_collect();
    check(npl_is_inexact(npl_complex_imag(value)) &&
          npl_real_double(npl_complex_imag(value)) == 0.5,
          "mixed exactness coerces imaginary part");
    expect_value(value, "2.0+0.5i");
    value = npl_complex_make(npl_fixnum(1), npl_fixnum(2));
    check(npl_complex_p(value) == npl_true() &&
          npl_complex_real(value) == npl_fixnum(1) &&
          npl_complex_imag(value) == npl_fixnum(2), "exact complex stays exact");
    value = npl_number_from_string(npl_string("4+0i", 4));
    check(value == npl_fixnum(4), "reader exact zero imaginary demotes");
    npl_gc_unroot(&half);
    npl_gc_unroot(&value);
    npl_gc_unroot(&i);
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

static NplVal fraction(const char *numerator, const char *denominator)
{
    NplVal n = parse(numerator);
    npl_gc_root(&n);
    NplVal d = parse(denominator);
    NplVal result = npl_rational_make(n, d);
    npl_gc_unroot(&n);
    return result;
}

static void rationals(void)
{
    expect_value(fraction("6", "-8"), "-3/4");
    expect_value(fraction("-6", "-8"), "3/4");
    check(fraction("0", "-8") == npl_fixnum(0), "fraction zero demotion");
    check(fraction("-24", "-8") == npl_fixnum(3), "fraction integer demotion");
    expect_value(fraction("123456789012345678901234567890", "246913578024691357802469135780"), "1/2");
    NplVal a = fraction("123456789012345678901234567891", "100000000000000000000000000003");
    npl_gc_root(&a);
    NplVal b = fraction("-7", "13");
    npl_gc_root(&b);
    NplVal temporary = npl_nil();
    npl_gc_root(&temporary);
    npl_gc_collect();
    expect_value(a, "123456789012345678901234567891/100000000000000000000000000003");
    check(npl_number_p(a) == npl_true(), "rational number predicate");
    expect_value(npl_rational_numerator(a), "123456789012345678901234567891");
    expect_value(npl_rational_denominator(a), "100000000000000000000000000003");
    check(npl_rational_numerator(npl_fixnum(-3)) == npl_fixnum(-3), "integer numerator");
    check(npl_rational_denominator(npl_fixnum(-3)) == npl_fixnum(1), "integer denominator");
    check(npl_number_compare(a, npl_fixnum(1)) > 0, "rational integer compare");
    check(npl_number_compare(b, a) < 0, "rational signed compare");
    check(npl_number_compare(a, a) == 0, "rational equal compare");
    temporary = npl_number_add(a, b);
    temporary = npl_number_sub(temporary, b);
    check(npl_eqv(temporary, a) == npl_true(), "rational add inverse eqv");
    temporary = npl_number_mul(a, b);
    temporary = npl_number_div(temporary, b);
    check(npl_eqv(temporary, a) == npl_true(), "rational multiply inverse eqv");
    check(npl_number_div(a, a) == npl_fixnum(1), "rational divide demotion");
    check(npl_number_sub(a, a) == npl_fixnum(0), "rational subtract demotion");
    expect_value(npl_number_div(npl_fixnum(3), npl_fixnum(-4)), "-3/4");
    expect_value(npl_number_add(b, npl_fixnum(2)), "19/13");
    expect_value(npl_number_sub(npl_fixnum(2), b), "33/13");
    expect_value(npl_number_mul(b, npl_fixnum(2)), "-14/13");
    expect_value(npl_number_div(npl_fixnum(2), b), "-26/7");
    temporary = npl_open_output_string();
    npl_write_port(a, temporary);
    NplVal text = npl_get_output_string(temporary);
    npl_gc_root(&text);
    NplVal expected = npl_number_to_string(a);
    check(npl_string_equal(text, expected), "rational write");
    npl_close_port(temporary);
    npl_gc_unroot(&text);
    npl_gc_unroot(&temporary);
    npl_gc_unroot(&b);
    npl_gc_unroot(&a);
}

static void oracle(const char *operation, const char *left, const char *right)
{
    NplVal a = parse(left);
    npl_gc_root(&a);
    NplVal b = parse(right);
    npl_gc_root(&b);
    NplVal value;
    if (strcmp(operation, "quotient") == 0) value = npl_number_quotient(a, b);
    else if (strcmp(operation, "remainder") == 0) value = npl_number_remainder(a, b);
    else if (strcmp(operation, "gcd") == 0) value = npl_number_gcd(a, b);
    else if (strcmp(operation, "divide") == 0) value = npl_number_div(a, b);
    else if (strcmp(operation, "rational") == 0) value = npl_rational_make(a, b);
    else if (strncmp(operation, "fraction-", 9) == 0) {
        NplVal x = npl_rational_make(a, b);
        npl_gc_root(&x);
        NplVal y = npl_rational_make(b, a);
        npl_gc_root(&y);
        if (strcmp(operation, "fraction-add") == 0) value = npl_number_add(x, y);
        else if (strcmp(operation, "fraction-sub") == 0) value = npl_number_sub(x, y);
        else if (strcmp(operation, "fraction-mul") == 0) value = npl_number_mul(x, y);
        else if (strcmp(operation, "fraction-div") == 0) value = npl_number_div(x, y);
        else if (strcmp(operation, "fraction-cmp") == 0)
            value = npl_fixnum(npl_number_compare(x, y));
        else { fprintf(stderr, "unknown fraction operation\n"); exit(2); }
        /* Keep the result rooted while releasing temporary operands.  An
         * exact operation may return one of its operands after canonical
         * demotion, so unrooting x/y first can leave value dangling. */
        npl_gc_root(&value);
        npl_gc_unroot(&y);
        npl_gc_unroot(&x);
    }
    else { fprintf(stderr, "unknown numeric operation\n"); exit(2); }
    if (strncmp(operation, "fraction-", 9) != 0) npl_gc_root(&value);
    npl_display(value);
    npl_newline();
    npl_gc_unroot(&value);
    npl_gc_unroot(&b);
    npl_gc_unroot(&a);
}

int main(int argc, char **argv)
{
    npl_runtime_init();
    if (argc == 4) {
        oracle(argv[1], argv[2], argv[3]);
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "--oracle") == 0) {
        char operation[32], left[1024], right[1024];
        while (scanf("%31s %1023s %1023s", operation, left, right) == 3)
            oracle(operation, left, right);
        return 0;
    }
    boundaries();
    generated();
    rationals();
    inexact();
    branches();
    rectangular();
    ports();
    printf("numeric substrate: PASS (%u checks)\n", checks);
    return 0;
}

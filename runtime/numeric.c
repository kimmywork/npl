/* numeric.c — exact integer/rational representation and numeric dispatch. */

#include "npl.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NPL_BIG_BASE UINT32_C(1000000000)
#define NPL_FIXNUM_MAGNITUDE (UINT64_C(1) << 61)

typedef struct {
    int32_t sign;
    uint32_t count;
    const uint32_t *limbs;
    uint32_t small[3];
} NplIntegerView;

/* Argument errors raise catchable error objects (runtime/error.c). */
static void numeric_error(const char *who, const char *message,
                          NplVal irritant)
{
    npl_primitive_error(who, message, irritant);
}

static NplVal box_integer_limbs(int32_t sign, const uint32_t *limbs,
                               uint32_t count)
{
    while (count != 0 && limbs[count - 1] == 0) count--;
    if (count == 0) return npl_fixnum(0);
    if (count <= 3 && (count < 3 || limbs[2] <= 2)) {
        uint64_t magnitude = 0;
        for (uint32_t i = count; i != 0; i--)
            magnitude = magnitude * NPL_BIG_BASE + limbs[i - 1];
        if ((sign > 0 && magnitude < NPL_FIXNUM_MAGNITUDE) ||
            (sign < 0 && magnitude <= NPL_FIXNUM_MAGNITUDE)) {
            int64_t value = (int64_t)magnitude;
            return npl_fixnum(sign < 0 ? -value : value);
        }
    }
    NplBigint *number = (NplBigint *)npl_alloc_object(
        sizeof(NplBigint) + (size_t)count * sizeof(uint32_t));
    number->header.tag = BOX_BIGNUM;
    number->header.flags = 0;
    number->sign = sign < 0 ? -1 : 1;
    number->count = count;
    number->capacity = count;
    memcpy(number->limbs, limbs, (size_t)count * sizeof(uint32_t));
    return (NplVal)(uintptr_t)number | TAG_PTR;
}

static void integer_view(NplIntegerView *view, NplVal value,
                         const char *who)
{
    memset(view, 0, sizeof(*view));
    if (npl_is_fixnum(value)) {
        int64_t integer = npl_fixnum_value(value);
        uint64_t magnitude = integer < 0
            ? (uint64_t)(-(integer + 1)) + 1 : (uint64_t)integer;
        if (magnitude == 0) return;
        view->sign = integer < 0 ? -1 : 1;
        view->small[0] = (uint32_t)(magnitude % NPL_BIG_BASE);
        magnitude /= NPL_BIG_BASE;
        view->count = 1;
        if (magnitude != 0) {
            view->small[1] = (uint32_t)(magnitude % NPL_BIG_BASE);
            magnitude /= NPL_BIG_BASE;
            view->count++;
        }
        if (magnitude != 0) {
            view->small[2] = (uint32_t)magnitude;
            view->count++;
        }
        view->limbs = view->small;
        return;
    }
    if (npl_is_ptr(value) && npl_header(value)->tag == BOX_BIGNUM) {
        NplBigint *number = (NplBigint *)npl_header(value);
        view->sign = number->sign;
        view->count = number->count;
        view->limbs = number->limbs;
        return;
    }
    numeric_error(who, "expected an exact integer", value);
}

static int integer_magnitude_compare(const NplIntegerView *left,
                                     const NplIntegerView *right)
{
    if (left->count != right->count)
        return left->count < right->count ? -1 : 1;
    for (uint32_t index = left->count; index != 0; index--) {
        if (left->limbs[index - 1] != right->limbs[index - 1])
            return left->limbs[index - 1] < right->limbs[index - 1]
                ? -1 : 1;
    }
    return 0;
}

/* When subtracting, the caller guarantees left magnitude >= right magnitude. */
static NplVal integer_add_magnitudes(int32_t sign,
                                     const NplIntegerView *left,
                                     const NplIntegerView *right,
                                     int subtract)
{
    uint32_t count = left->count > right->count ? left->count : right->count;
    uint64_t capacity = (uint64_t)count + 1;
    if (capacity > UINT32_MAX || capacity > SIZE_MAX / sizeof(uint32_t))
        npl_runtime_oom_internal();
    uint32_t *limbs = (uint32_t *)calloc((size_t)capacity, sizeof(uint32_t));
    if (!limbs) npl_runtime_oom_internal();
    uint64_t carry = 0;
    for (uint32_t index = 0; index < count; index++) {
        int64_t a = index < left->count ? left->limbs[index] : 0;
        int64_t b = index < right->count ? right->limbs[index] : 0;
        int64_t result = subtract ? a - b - (int64_t)carry
                                  : a + b + (int64_t)carry;
        if (subtract) {
            if (result < 0) { result += NPL_BIG_BASE; carry = 1; }
            else carry = 0;
        } else {
            carry = result >= NPL_BIG_BASE;
            if (carry) result -= NPL_BIG_BASE;
        }
        limbs[index] = (uint32_t)result;
    }
    if (!subtract && carry) limbs[count++] = (uint32_t)carry;
    NplVal result = box_integer_limbs(sign, limbs, count);
    free(limbs);
    return result;
}

/* Numeric syntax is ASCII, so the parsers below read narrow string bytes
 * directly; a wide string (a character above 255) is never a number. */
static int narrow_string(NplVal value)
{
    return npl_is_ptr(value) && npl_header(value)->tag == BOX_STRING &&
           (npl_header(value)->flags & NPL_FLAG_WIDE) == 0;
}

NplVal npl_integer_string_p(NplVal value)
{
    if (!narrow_string(value)) return npl_false();
    NplString *text = (NplString *)npl_header(value);
    uint32_t index = (text->length != 0 &&
                      (text->data[0] == '+' || text->data[0] == '-')) ? 1 : 0;
    if (index == text->length) return npl_false();
    for (; index < text->length; index++)
        if (text->data[index] < '0' || text->data[index] > '9')
            return npl_false();
    return npl_true();
}

NplVal npl_integer_from_string(NplVal value)
{
    if (npl_integer_string_p(value) != npl_true())
        numeric_error("integer-from-string", "invalid decimal integer", value);
    const NplString *text = (const NplString *)npl_header(value);
    uint32_t index = 0;
    int32_t sign = 1;
    if (text->length != 0 &&
        (text->data[0] == '+' || text->data[0] == '-')) {
        sign = text->data[0] == '-' ? -1 : 1;
        index = 1;
    }
    uint32_t capacity = text->length / 9 + 1;
    uint32_t *limbs = (uint32_t *)calloc(capacity, sizeof(uint32_t));
    if (!limbs) npl_runtime_oom_internal();
    uint32_t count = 0;
    for (; index < text->length; index++) {
        unsigned char digit = (unsigned char)text->data[index];
        uint64_t carry = (uint64_t)(digit - '0');
        for (uint32_t limb = 0; limb < count; limb++) {
            uint64_t product = (uint64_t)limbs[limb] * 10 + carry;
            limbs[limb] = (uint32_t)(product % NPL_BIG_BASE);
            carry = product / NPL_BIG_BASE;
        }
        if (carry != 0) limbs[count++] = (uint32_t)carry;
    }
    NplVal result = box_integer_limbs(sign, limbs, count);
    free(limbs);
    return result;
}

NplVal npl_integer_p(NplVal value)
{
    return (npl_is_fixnum(value) ||
            (npl_is_ptr(value) && npl_header(value)->tag == BOX_BIGNUM))
        ? npl_true() : npl_false();
}

NplVal npl_inexact_eqv(NplVal left, NplVal right)
{
    if (npl_is_complex(left) || npl_is_complex(right))
        return (npl_is_complex(left) && npl_is_complex(right) &&
                npl_eqv(npl_complex_real(left), npl_complex_real(right)) == npl_true() &&
                npl_eqv(npl_complex_imag(left), npl_complex_imag(right)) == npl_true())
            ? npl_true() : npl_false();
    if (!npl_is_inexact(left) || !npl_is_inexact(right))
        return npl_false();
    double a = ((NplFloat *)npl_header(left))->value;
    double b = ((NplFloat *)npl_header(right))->value;
    uint64_t abits, bbits;
    memcpy(&abits, &a, sizeof(abits));
    memcpy(&bbits, &b, sizeof(bbits));
    return abits == bbits ? npl_true() : npl_false();
}

NplVal npl_exact_to_inexact(NplVal value)
{
    if (npl_number_p(value) != npl_true())
        numeric_error("exact->inexact", "expected a number", value);
    return npl_is_inexact(value) ? value : npl_float(npl_real_double(value));
}

NplVal npl_inexact_to_exact(NplVal value)
{
    if (npl_number_p(value) != npl_true() || !npl_is_inexact(value))
        numeric_error("inexact->exact", "expected an inexact number", value);
    return npl_real_exact(value);
}

static int radix_digit(unsigned char character)
{
    if (character >= '0' && character <= '9') return character - '0';
    if (character >= 'a' && character <= 'z') return character - 'a' + 10;
    if (character >= 'A' && character <= 'Z') return character - 'A' + 10;
    return -1;
}

static NplVal integer_from_radix_digits(NplVal value, uint32_t begin,
                                        uint32_t end, int radix, int signed_value)
{
    const NplString *text = (const NplString *)npl_header(value);
    int negative = 0;
    if (signed_value && begin < end &&
        (text->data[begin] == '+' || text->data[begin] == '-')) {
        negative = text->data[begin] == '-';
        begin++;
    }
    if (begin == end) return npl_false();
    NplVal result = npl_fixnum(0);
    npl_gc_root(&value);
    npl_gc_root(&result);
    for (uint32_t index = begin; index < end; index++) {
        int digit = radix_digit((unsigned char)text->data[index]);
        if (digit < 0 || digit >= radix) {
            npl_gc_unroot(&result);
            npl_gc_unroot(&value);
            return npl_false();
        }
        result = npl_number_mul(result, npl_fixnum(radix));
        result = npl_number_add(result, npl_fixnum(digit));
    }
    if (negative) result = npl_number_sub(npl_fixnum(0), result);
    npl_gc_unroot(&result);
    npl_gc_unroot(&value);
    return result;
}

static NplVal exact_number_from_string(NplVal value, int radix, uint32_t begin)
{
    const NplString *text = (const NplString *)npl_header(value);
    uint32_t slash = begin;
    while (slash < text->length && text->data[slash] != '/') slash++;
    if (slash == begin || (slash < text->length && slash + 1 == text->length))
        return npl_false();
    if (slash == text->length)
        return integer_from_radix_digits(value, begin, slash, radix, 1);
    if (memchr(text->data + slash + 1, '/', text->length - slash - 1))
        return npl_false();
    npl_gc_root(&value);
    NplVal numerator = integer_from_radix_digits(value, begin, slash, radix, 1);
    npl_gc_root(&numerator);
    if (numerator == npl_false()) {
        npl_gc_unroot(&numerator);
        npl_gc_unroot(&value);
        return npl_false();
    }
    text = (const NplString *)npl_header(value);
    NplVal denominator = integer_from_radix_digits(value, slash + 1,
                                                    text->length, radix, 0);
    npl_gc_root(&denominator);
    NplVal result = denominator == npl_false() || denominator == npl_fixnum(0)
        ? npl_false() : npl_rational_make(numerator, denominator);
    npl_gc_unroot(&denominator);
    npl_gc_unroot(&numerator);
    npl_gc_unroot(&value);
    return result;
}

NplVal npl_number_from_string_radix(NplVal value, NplVal radix)
{
    if (!narrow_string(value) || !npl_is_fixnum(radix)) return npl_false();
    int64_t base = npl_fixnum_value(radix);
    if (base < 2 || base > 36) return npl_false();
    return exact_number_from_string(value, (int)base, 0);
}

/* Length of a "+inf.0" / "-nan.0" style token at text[start] (R7RS 7.1.1
 * <infnan>, any case), or 0. */
static uint32_t infnan_length(const char *text, uint32_t start, uint32_t end)
{
    static const char *names[] = { "inf.0", "nan.0" };
    if (start + 6 > end || (text[start] != '+' && text[start] != '-')) return 0;
    for (int n = 0; n < 2; n++) {
        int match = 1;
        for (int k = 0; k < 5 && match; k++) {
            char c = text[start + 1 + k];
            if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
            if (c != names[n][k]) match = 0;
        }
        if (match) return 6;
    }
    return 0;
}

/* Where the imaginary part of a rectangular complex begins: the last '+'
 * or '-' that is not an exponent sign or the start of the text, so that
 * "1e+2-3i" splits after "1e+2". Infinities are skipped as a whole. */
static uint32_t imaginary_start(const char *text, uint32_t end)
{
    uint32_t split = 0;
    uint32_t i = 0;
    while (i < end) {
        uint32_t infnan = infnan_length(text, i, end);
        if (infnan) {
            if (i > 0) split = i;
            i += infnan;
            continue;
        }
        char c = text[i];
        if ((c == '+' || c == '-') && i > 0) {
            char before = text[i - 1];
            if (before != 'e' && before != 'E') split = i;
        }
        i++;
    }
    return split;
}

/* Rectangular complex syntax (R7RS 7.1.1): [real] (+|-) [ureal] i, where an
 * omitted imaginary magnitude means 1. Any-case 'i' and infinities. */
static NplVal complex_from_string(NplVal value)
{
    const NplString *text = (const NplString *)npl_header(value);
    uint32_t length = text->length;
    if (length < 2) return npl_false();
    char last = text->data[length - 1];
    if (last != 'i' && last != 'I') return npl_false();
    /* "+inf.0" itself ends with no i; "+inf.0i" is a pure imaginary. */
    uint32_t split = imaginary_start(text->data, length - 1);
    char sign = text->data[split];
    if (sign != '+' && sign != '-') return npl_false();
    npl_gc_root(&value);
    NplVal real = npl_fixnum(0);
    if (split > 0) {
        NplVal real_text = npl_substring(value, npl_fixnum(0),
                                         npl_fixnum(split));
        npl_gc_root(&real_text);
        real = npl_number_from_string(real_text);
        npl_gc_unroot(&real_text);
        if (real == npl_false() || npl_is_complex(real)) {
            npl_gc_unroot(&value);
            return npl_false();
        }
    }
    npl_gc_root(&real);
    NplVal imag;
    if (split + 2 == length) {
        imag = npl_fixnum(sign == '-' ? -1 : 1);       /* "+i", "-i" */
    } else {
        text = (const NplString *)npl_header(value);
        NplVal imag_text = npl_substring(value, npl_fixnum(split),
                                         npl_fixnum(length - 1));
        npl_gc_root(&imag_text);
        imag = npl_number_from_string(imag_text);
        npl_gc_unroot(&imag_text);
        if (imag != npl_false() && npl_is_complex(imag)) imag = npl_false();
    }
    NplVal result = imag == npl_false() ? npl_false()
                                        : npl_complex_make(real, imag);
    npl_gc_unroot(&real);
    npl_gc_unroot(&value);
    return result;
}

/* #e / #i applied to a parsed number (componentwise for complex). An
 * infinity or NaN has no exact value: the syntax is then not a number. */
static NplVal apply_exactness(NplVal number, int exactness)
{
    if (npl_is_complex(number)) {
        NplVal real = apply_exactness(npl_complex_real(number), exactness);
        if (real == npl_false()) return npl_false();
        npl_gc_root(&real);
        NplVal imag = apply_exactness(npl_complex_imag(number), exactness);
        npl_gc_unroot(&real);
        if (imag == npl_false()) return npl_false();
        return npl_complex_make(real, imag);
    }
    if (exactness == 'i')
        return npl_is_inexact(number) ? number
                                      : npl_float(npl_real_double(number));
    if (!npl_is_inexact(number)) return number;
    if (npl_finite_p(number) != npl_true()) return npl_false();
    return npl_real_exact(number);
}

/* The number syntax without prefixes (decimal or a radix). */
static NplVal unprefixed_number(NplVal value, int radix)
{
    NplVal complex = complex_from_string(value);
    if (complex != npl_false()) return complex;
    if (radix != 10) return exact_number_from_string(value, radix, 0);
    NplVal exact = npl_number_from_string_radix(value, npl_fixnum(10));
    return exact == npl_false() ? npl_inexact_parse(value) : exact;
}

/* R7RS 7.1.1 prefixes: at most one radix (#b #o #d #x) and one exactness
 * (#e #i) marker, in either order. */
NplVal npl_number_from_string(NplVal value)
{
    if (!narrow_string(value)) return npl_false();
    const NplString *text = (const NplString *)npl_header(value);
    int radix = 0, exactness = 0;
    uint32_t start = 0;
    while (start + 1 < text->length && text->data[start] == '#') {
        unsigned char c = (unsigned char)text->data[start + 1];
        int r = c == 'b' || c == 'B' ? 2 : c == 'o' || c == 'O' ? 8
              : c == 'd' || c == 'D' ? 10 : c == 'x' || c == 'X' ? 16 : 0;
        int e = c == 'e' || c == 'E' ? 'e' : c == 'i' || c == 'I' ? 'i' : 0;
        if (r && !radix) radix = r;
        else if (e && !exactness) exactness = e;
        else return npl_false();
        start += 2;
    }
    if (start == text->length) return npl_false();
    if (start == 0) return unprefixed_number(value, 10);
    if (radix && radix != 10 && !exactness && start == 2) {
        /* A non-decimal radix alone: the radix parser accepts "#x...". */
        NplVal number = complex_from_string(value);
        return number != npl_false() ? npl_false()
                                     : exact_number_from_string(value, radix, 2);
    }
    NplVal rest = npl_string(text->data + start, text->length - start);
    npl_gc_root(&rest);
    NplVal number = unprefixed_number(rest, radix ? radix : 10);
    npl_gc_unroot(&rest);
    if (number == npl_false() || !exactness) return number;
    return apply_exactness(number, exactness);
}

static int is_rational_box(NplVal value)
{
    return npl_is_ptr(value) && npl_header(value)->tag == BOX_RATIONAL;
}

NplVal npl_rational_numerator(NplVal value)
{
    if (is_rational_box(value)) return ((NplRational *)npl_header(value))->numerator;
    NplIntegerView view;
    integer_view(&view, value, "numerator");
    return value;
}

NplVal npl_rational_denominator(NplVal value)
{
    if (is_rational_box(value)) return ((NplRational *)npl_header(value))->denominator;
    NplIntegerView view;
    integer_view(&view, value, "denominator");
    return npl_fixnum(1);
}

NplVal npl_rational_make(NplVal numerator, NplVal denominator)
{
    NplIntegerView n, d;
    integer_view(&n, numerator, "rational");
    integer_view(&d, denominator, "rational");
    if (d.sign == 0) numeric_error("rational", "zero denominator", numerator);
    if (n.sign == 0) return npl_fixnum(0);
    npl_gc_root(&numerator);
    npl_gc_root(&denominator);
    NplVal common = npl_number_gcd(numerator, denominator);
    npl_gc_root(&common);
    numerator = npl_number_quotient(numerator, common);
    denominator = npl_number_quotient(denominator, common);
    if (d.sign < 0) {
        numerator = npl_number_sub(npl_fixnum(0), numerator);
        denominator = npl_number_sub(npl_fixnum(0), denominator);
    }
    NplVal result = numerator;
    if (denominator != npl_fixnum(1)) {
        NplRational *value = (NplRational *)npl_alloc_object(sizeof(NplRational));
        value->header.tag = BOX_RATIONAL;
        value->header.flags = 0;
        value->numerator = numerator;
        value->denominator = denominator;
        result = (NplVal)(uintptr_t)value | TAG_PTR;
    }
    npl_gc_unroot(&common);
    npl_gc_unroot(&denominator);
    npl_gc_unroot(&numerator);
    return result;
}

/* Components are integers, so recursive numeric operations stay on the
 * integer path. Root every component and intermediate across allocations. */
static NplVal rational_arithmetic(NplVal left, NplVal right, char operation)
{
    NplVal values[6] = {npl_rational_numerator(left),
                       npl_rational_denominator(left),
                       npl_rational_numerator(right),
                       npl_rational_denominator(right), npl_nil(), npl_nil()};
    for (unsigned i = 0; i < 6; i++) npl_gc_root(&values[i]);
    if (operation == '+' || operation == '-') {
        values[4] = npl_number_mul(values[0], values[3]);
        values[5] = npl_number_mul(values[2], values[1]);
        values[4] = operation == '+' ? npl_number_add(values[4], values[5])
                                     : npl_number_sub(values[4], values[5]);
        values[5] = npl_number_mul(values[1], values[3]);
    } else if (operation == '*') {
        values[4] = npl_number_mul(values[0], values[2]);
        values[5] = npl_number_mul(values[1], values[3]);
    } else {
        if (values[2] == npl_fixnum(0))
            numeric_error("/", "division by zero", left);
        values[4] = npl_number_mul(values[0], values[3]);
        values[5] = npl_number_mul(values[1], values[2]);
    }
    NplVal result = npl_rational_make(values[4], values[5]);
    for (unsigned i = 6; i != 0; i--) npl_gc_unroot(&values[i - 1]);
    return result;
}

NplVal npl_number_div(NplVal left, NplVal right)
{
    if (npl_is_complex(left) || npl_is_complex(right))
        return npl_complex_arithmetic(left, right, '/');
    if (npl_is_inexact(left) || npl_is_inexact(right))
        return npl_inexact_arithmetic(left, right, '/');
    return rational_arithmetic(left, right, '/');
}

NplVal npl_number_p(NplVal value)
{
    return (npl_is_fixnum(value) || is_rational_box(value) || npl_is_complex(value) ||
            npl_is_inexact(value) ||
            (npl_is_ptr(value) && npl_header(value)->tag == BOX_BIGNUM))
        ? npl_true() : npl_false();
}

NplVal npl_number_zero_p(NplVal value)
{
    if (npl_is_complex(value))
        return (npl_number_zero_p(npl_complex_real(value)) == npl_true() &&
                npl_number_zero_p(npl_complex_imag(value)) == npl_true())
            ? npl_true() : npl_false();
    if (npl_number_p(value) != npl_true()) return npl_false();
    return npl_number_compare(value, npl_fixnum(0)) == 0 ? npl_true() : npl_false();
}

/* Fixnums are tested first: the common case must not pay for the complex,
 * inexact, and rational tests. An overflowing fixnum result falls through to
 * the bignum path. */
NplVal npl_number_add(NplVal left, NplVal right)
{
    if (npl_is_fixnum(left) && npl_is_fixnum(right)) {
        int64_t sum = npl_fixnum_value(left) + npl_fixnum_value(right);
        if (sum >= -(INT64_C(1) << 61) && sum < (INT64_C(1) << 61))
            return npl_fixnum(sum);
    }
    if (npl_is_complex(left) || npl_is_complex(right))
        return npl_complex_arithmetic(left, right, '+');
    if (npl_is_inexact(left) || npl_is_inexact(right))
        return npl_inexact_arithmetic(left, right, '+');
    if (is_rational_box(left) || is_rational_box(right))
        return rational_arithmetic(left, right, '+');
    NplIntegerView a;
    NplIntegerView b;
    integer_view(&a, left, "+");
    integer_view(&b, right, "+");
    if (a.sign == 0) return right;
    if (b.sign == 0) return left;
    if (a.sign == b.sign) return integer_add_magnitudes(a.sign, &a, &b, 0);
    int order = integer_magnitude_compare(&a, &b);
    if (order == 0) return npl_fixnum(0);
    if (order > 0) return integer_add_magnitudes(a.sign, &a, &b, 1);
    return integer_add_magnitudes(b.sign, &b, &a, 1);
}

NplVal npl_number_sub(NplVal left, NplVal right)
{
    if (npl_is_fixnum(left) && npl_is_fixnum(right)) {
        int64_t difference = npl_fixnum_value(left) - npl_fixnum_value(right);
        if (difference >= -(INT64_C(1) << 61) && difference < (INT64_C(1) << 61))
            return npl_fixnum(difference);
    }
    if (npl_is_complex(left) || npl_is_complex(right))
        return npl_complex_arithmetic(left, right, '-');
    if (npl_is_inexact(left) || npl_is_inexact(right))
        return npl_inexact_arithmetic(left, right, '-');
    if (is_rational_box(left) || is_rational_box(right))
        return rational_arithmetic(left, right, '-');
    NplIntegerView a;
    NplIntegerView b;
    integer_view(&a, left, "-");
    integer_view(&b, right, "-");
    if (b.sign == 0) return left;
    b.sign = -b.sign;
    if (a.sign == 0) {
        uint32_t *limbs = (uint32_t *)malloc(
            (size_t)b.count * sizeof(uint32_t));
        if (!limbs) npl_runtime_oom_internal();
        memcpy(limbs, b.limbs, (size_t)b.count * sizeof(uint32_t));
        NplVal result = box_integer_limbs(b.sign, limbs, b.count);
        free(limbs);
        return result;
    }
    if (a.sign == b.sign) return integer_add_magnitudes(a.sign, &a, &b, 0);
    int order = integer_magnitude_compare(&a, &b);
    if (order == 0) return npl_fixnum(0);
    if (order > 0) return integer_add_magnitudes(a.sign, &a, &b, 1);
    return integer_add_magnitudes(b.sign, &b, &a, 1);
}

NplVal npl_number_mul(NplVal left, NplVal right)
{
    if (npl_is_complex(left) || npl_is_complex(right))
        return npl_complex_arithmetic(left, right, '*');
    if (npl_is_inexact(left) || npl_is_inexact(right))
        return npl_inexact_arithmetic(left, right, '*');
    if (is_rational_box(left) || is_rational_box(right))
        return rational_arithmetic(left, right, '*');
    if (npl_is_fixnum(left) && npl_is_fixnum(right)) {
        int64_t x = npl_fixnum_value(left), y = npl_fixnum_value(right);
        uint64_t ax = (uint64_t)(x < 0 ? -x : x);
        uint64_t ay = (uint64_t)(y < 0 ? -y : y);
        uint64_t limit = (x < 0) != (y < 0)
            ? NPL_FIXNUM_MAGNITUDE : NPL_FIXNUM_MAGNITUDE - 1;
        if (ay == 0 || ax <= limit / ay) return npl_fixnum(x * y);
    }
    NplIntegerView a;
    NplIntegerView b;
    integer_view(&a, left, "*");
    integer_view(&b, right, "*");
    if (a.sign == 0 || b.sign == 0) return npl_fixnum(0);
    uint64_t capacity = (uint64_t)a.count + b.count + 1;
    if (capacity > UINT32_MAX || capacity > SIZE_MAX / sizeof(uint32_t))
        npl_runtime_oom_internal();
    uint32_t count = (uint32_t)capacity - 1;
    uint32_t *limbs = (uint32_t *)calloc((size_t)capacity, sizeof(uint32_t));
    if (!limbs) npl_runtime_oom_internal();
    for (uint32_t i = 0; i < a.count; i++) {
        uint64_t carry = 0;
        for (uint32_t j = 0; j < b.count; j++) {
            uint64_t current = (uint64_t)a.limbs[i] * b.limbs[j] +
                               limbs[i + j] + carry;
            limbs[i + j] = (uint32_t)(current % NPL_BIG_BASE);
            carry = current / NPL_BIG_BASE;
        }
        uint32_t index = i + b.count;
        while (carry != 0) {
            uint64_t current = (uint64_t)limbs[index] + carry;
            limbs[index++] = (uint32_t)(current % NPL_BIG_BASE);
            carry = current / NPL_BIG_BASE;
        }
    }
    NplVal result = box_integer_limbs(a.sign == b.sign ? 1 : -1,
                                      limbs, count);
    free(limbs);
    return result;
}

int32_t npl_number_compare(NplVal left, NplVal right)
{
    if (npl_is_fixnum(left) && npl_is_fixnum(right)) {
        int64_t x = npl_fixnum_value(left), y = npl_fixnum_value(right);
        return x < y ? -1 : (x > y ? 1 : 0);
    }
    if (npl_is_complex(left) || npl_is_complex(right))
        numeric_error("number comparison", "complex number is unordered",
                      npl_is_complex(left) ? left : right);
    if (npl_is_inexact(left) || npl_is_inexact(right))
        return npl_inexact_compare(left, right);
    if (is_rational_box(left) || is_rational_box(right)) {
        npl_gc_root(&left);
        npl_gc_root(&right);
        NplVal a = npl_number_mul(npl_rational_numerator(left),
                                  npl_rational_denominator(right));
        npl_gc_root(&a);
        NplVal b = npl_number_mul(npl_rational_numerator(right),
                                  npl_rational_denominator(left));
        int32_t result = npl_number_compare(a, b);
        npl_gc_unroot(&a);
        npl_gc_unroot(&right);
        npl_gc_unroot(&left);
        return result;
    }
    NplIntegerView a;
    NplIntegerView b;
    integer_view(&a, left, "number comparison");
    integer_view(&b, right, "number comparison");
    if (a.sign != b.sign) return a.sign < b.sign ? -1 : 1;
    if (a.sign == 0) return 0;
    int order = integer_magnitude_compare(&a, &b);
    return a.sign < 0 ? -order : order;
}

NplVal npl_eqv(NplVal left, NplVal right)
{
    if (npl_is_complex(left) || npl_is_complex(right))
        return npl_inexact_eqv(left, right);
    if (npl_is_inexact(left) || npl_is_inexact(right))
        return npl_inexact_eqv(left, right);
    if (npl_number_p(left) == npl_true() && npl_number_p(right) == npl_true())
        return npl_number_compare(left, right) == 0 ? npl_true() : npl_false();
    /* Two pointer boxes are the same pointer when their addresses are. */
    if (npl_is_pointer(left) && npl_is_pointer(right))
        return npl_pointer_address(left, "eqv?") ==
               npl_pointer_address(right, "eqv?") ? npl_true() : npl_false();
    return left == right ? npl_true() : npl_false();
}

/* Search each base-10^9 quotient digit using unsigned limb magnitudes.
 * Scratch storage is outside the GC; only the final result is boxed. */
/* Divide a magnitude (base 10^9 limbs, little endian) in place by a small
 * divisor (< 2^32) and return the remainder: one linear pass. */
static uint32_t limbs_divide_small(uint32_t *limbs, uint32_t *count,
                                   uint32_t divisor)
{
    uint64_t remainder = 0;
    for (uint32_t i = *count; i != 0; i--) {
        uint64_t current = remainder * NPL_BIG_BASE + limbs[i - 1];
        limbs[i - 1] = (uint32_t)(current / divisor);
        remainder = current % divisor;
    }
    while (*count && limbs[*count - 1] == 0) (*count)--;
    return (uint32_t)remainder;
}

static NplVal integer_divide(NplVal left, NplVal right, int want_remainder)
{
    const char *who = want_remainder ? "remainder" : "quotient";
    NplIntegerView a, b;
    integer_view(&a, left, who);
    integer_view(&b, right, who);
    if (b.sign == 0) numeric_error(who, "division by zero", left);
    if (a.sign == 0) return npl_fixnum(0);
    if (npl_is_fixnum(left) && npl_is_fixnum(right)) {
        int64_t x = npl_fixnum_value(left), y = npl_fixnum_value(right);
        if (want_remainder) return npl_fixnum(x % y);
        if (x != -(INT64_C(1) << 61) || y != -1)
            return npl_fixnum(x / y);
    }
    if (integer_magnitude_compare(&a, &b) < 0)
        return want_remainder ? left : npl_fixnum(0);
    if (b.count == 1) {
        uint32_t count = a.count;
        uint32_t *quotient = malloc((size_t)count * sizeof(uint32_t));
        if (!quotient) npl_runtime_oom_internal();
        memcpy(quotient, a.limbs, (size_t)count * sizeof(uint32_t));
        uint32_t remainder = limbs_divide_small(quotient, &count, b.limbs[0]);
        NplVal result = want_remainder
            ? box_integer_limbs(a.sign, &remainder, remainder ? 1 : 0)
            : box_integer_limbs(a.sign == b.sign ? 1 : -1, quotient, count);
        free(quotient);
        return result;
    }
    /* Long division (Knuth, TAOCP vol. 2, 4.3.1, Algorithm D) in base 10^9.
     * Both operands are scaled by d = BASE / (top divisor limb + 1) so the
     * divisor's top limb is at least BASE / 2; each quotient limb is then
     * estimated from the top two remainder limbs and is at most 2 too
     * large, which the correction steps fix. */
    uint32_t n = b.count, m = a.count - b.count;
    uint32_t *u = calloc((size_t)a.count + 1, sizeof(uint32_t));
    uint32_t *v = calloc((size_t)n, sizeof(uint32_t));
    uint32_t *q = calloc((size_t)m + 1, sizeof(uint32_t));
    if (!u || !v || !q) npl_runtime_oom_internal();
    uint64_t d = NPL_BIG_BASE / ((uint64_t)b.limbs[n - 1] + 1);
    uint64_t carry = 0;
    for (uint32_t i = 0; i < a.count; i++) {
        uint64_t t = (uint64_t)a.limbs[i] * d + carry;
        u[i] = (uint32_t)(t % NPL_BIG_BASE);
        carry = t / NPL_BIG_BASE;
    }
    u[a.count] = (uint32_t)carry;
    carry = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint64_t t = (uint64_t)b.limbs[i] * d + carry;
        v[i] = (uint32_t)(t % NPL_BIG_BASE);
        carry = t / NPL_BIG_BASE;
    }
    for (uint32_t j = m + 1; j != 0; j--) {
        uint32_t k = j - 1;
        uint64_t top = (uint64_t)u[k + n] * NPL_BIG_BASE + u[k + n - 1];
        uint64_t qhat = top / v[n - 1];
        uint64_t rhat = top % v[n - 1];
        while (qhat >= NPL_BIG_BASE ||
               (n >= 2 && qhat * v[n - 2] >
                          rhat * NPL_BIG_BASE + u[k + n - 2])) {
            qhat--;
            rhat += v[n - 1];
            if (rhat >= NPL_BIG_BASE) break;
        }
        /* u[k .. k+n] -= qhat * v */
        int64_t borrow = 0;
        carry = 0;
        for (uint32_t i = 0; i < n; i++) {
            uint64_t product = qhat * v[i] + carry;
            carry = product / NPL_BIG_BASE;
            int64_t t = (int64_t)u[k + i] -
                        (int64_t)(product % NPL_BIG_BASE) - borrow;
            borrow = t < 0;
            u[k + i] = (uint32_t)(t + (borrow ? (int64_t)NPL_BIG_BASE : 0));
        }
        int64_t t = (int64_t)u[k + n] - (int64_t)carry - borrow;
        borrow = t < 0;
        u[k + n] = (uint32_t)(t + (borrow ? (int64_t)NPL_BIG_BASE : 0));
        if (borrow) {
            /* qhat was one too large: add v back. */
            qhat--;
            uint64_t add = 0;
            for (uint32_t i = 0; i < n; i++) {
                uint64_t sum = (uint64_t)u[k + i] + v[i] + add;
                u[k + i] = (uint32_t)(sum % NPL_BIG_BASE);
                add = sum / NPL_BIG_BASE;
            }
            u[k + n] = (uint32_t)(((uint64_t)u[k + n] + add) % NPL_BIG_BASE);
        }
        q[k] = (uint32_t)qhat;
    }
    NplVal result;
    if (want_remainder) {
        /* Unscale: the remainder is u[0 .. n-1] / d. */
        uint32_t count = n;
        while (count && u[count - 1] == 0) count--;
        (void)limbs_divide_small(u, &count, (uint32_t)d);
        result = box_integer_limbs(a.sign, u, count);
    } else {
        result = box_integer_limbs(a.sign == b.sign ? 1 : -1, q, m + 1);
    }
    free(q);
    free(v);
    free(u);
    return result;
}

NplVal npl_number_quotient(NplVal left, NplVal right)
{
    return integer_divide(left, right, 0);
}

NplVal npl_number_remainder(NplVal left, NplVal right)
{
    return integer_divide(left, right, 1);
}

NplVal npl_number_gcd(NplVal left, NplVal right)
{
    NplIntegerView a, b;
    integer_view(&a, left, "gcd");
    integer_view(&b, right, "gcd");
    npl_gc_root(&left);
    npl_gc_root(&right);
    if (a.sign < 0) left = npl_number_sub(npl_fixnum(0), left);
    if (b.sign < 0) right = npl_number_sub(npl_fixnum(0), right);
    while (right != npl_fixnum(0)) {
        NplVal rest = npl_number_remainder(left, right);
        left = right;
        right = rest;
    }
    npl_gc_unroot(&right);
    npl_gc_unroot(&left);
    return left;
}

/* Digits of an integer in radix 2..36. Base 10 prints each base-10^9 limb
 * as nine digits directly (linear time). Other radices take whole chunks
 * of digits per division by radix^k (the largest power below 2^32), so a
 * conversion is quadratic in the length with a small constant, not cubic
 * as with one full division per digit. */
static NplVal integer_to_string_radix(NplVal value, int radix)
{
    static const char digits[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    NplIntegerView view;
    integer_view(&view, value, "number->string");
    if (view.sign == 0) return npl_string("0", 1);
    size_t capacity = (size_t)view.count * 32 + 2;
    char *buffer = malloc(capacity);
    if (!buffer) npl_runtime_oom_internal();
    size_t length = 0;
    if (radix == 10) {
        char chunk[16];
        int used = snprintf(chunk, sizeof(chunk), "%u",
                            (unsigned)view.limbs[view.count - 1]);
        if (view.sign < 0) buffer[length++] = '-';
        memcpy(buffer + length, chunk, (size_t)used);
        length += (size_t)used;
        for (uint32_t i = view.count - 1; i != 0; i--) {
            snprintf(chunk, sizeof(chunk), "%09u", (unsigned)view.limbs[i - 1]);
            memcpy(buffer + length, chunk, 9);
            length += 9;
        }
    } else {
        uint32_t chunk_digits = 0;
        uint64_t power = 1;
        while (power * (uint64_t)radix <= UINT32_MAX) {
            power *= (uint64_t)radix;
            chunk_digits++;
        }
        uint32_t count = view.count;
        uint32_t *work = malloc((size_t)count * sizeof(uint32_t));
        if (!work) npl_runtime_oom_internal();
        memcpy(work, view.limbs, (size_t)count * sizeof(uint32_t));
        while (count) {
            uint32_t chunk = limbs_divide_small(work, &count, (uint32_t)power);
            for (uint32_t k = 0; k < chunk_digits; k++) {
                if (count == 0 && chunk == 0) break;
                buffer[length++] = digits[chunk % (uint32_t)radix];
                chunk /= (uint32_t)radix;
            }
        }
        free(work);
        if (view.sign < 0) buffer[length++] = '-';
        for (size_t index = 0; index < length / 2; index++) {
            char swap = buffer[index];
            buffer[index] = buffer[length - index - 1];
            buffer[length - index - 1] = swap;
        }
    }
    NplVal result = npl_string(buffer, (uint32_t)length);
    free(buffer);
    return result;
}

NplVal npl_number_to_string_radix(NplVal value, NplVal radix)
{
    if (!npl_is_fixnum(radix) || npl_fixnum_value(radix) < 2 ||
        npl_fixnum_value(radix) > 36) return npl_false();
    int base = (int)npl_fixnum_value(radix);
    if (is_rational_box(value)) {
        npl_gc_root(&value);
        NplVal numerator = npl_number_to_string_radix(
            npl_rational_numerator(value), radix);
        npl_gc_root(&numerator);
        NplVal denominator = npl_number_to_string_radix(
            npl_rational_denominator(value), radix);
        const NplString *n = (const NplString *)npl_header(numerator);
        const NplString *d = (const NplString *)npl_header(denominator);
        uint64_t length = (uint64_t)n->length + 1 + d->length;
        if (length > UINT32_MAX || length > SIZE_MAX) npl_runtime_oom_internal();
        char *buffer = malloc((size_t)length);
        if (!buffer) npl_runtime_oom_internal();
        memcpy(buffer, n->data, n->length);
        buffer[n->length] = '/';
        memcpy(buffer + n->length + 1, d->data, d->length);
        NplVal result = npl_string(buffer, (uint32_t)length);
        free(buffer);
        npl_gc_unroot(&numerator);
        npl_gc_unroot(&value);
        return result;
    }
    return integer_to_string_radix(value, base);
}

NplVal npl_number_to_string(NplVal value)
{
    if (npl_is_complex(value)) {
        npl_gc_root(&value);
        NplVal real = npl_number_to_string(npl_complex_real(value));
        NplVal imag = npl_number_to_string(npl_complex_imag(value));
        npl_gc_root(&real); npl_gc_root(&imag);
        const NplString *r = (const NplString *)npl_header(real);
        const NplString *i = (const NplString *)npl_header(imag);
        /* The imaginary text supplies its own sign when it has one ("-2",
         * "+inf.0", "-nan.0"); otherwise a '+' joins the parts. */
        int signed_text = i->length && (i->data[0] == '-' || i->data[0] == '+');
        uint64_t length = (uint64_t)r->length + 1 + i->length + 1;
        if (length > UINT32_MAX || length > SIZE_MAX) npl_runtime_oom_internal();
        char *buffer = malloc((size_t)length);
        if (!buffer) npl_runtime_oom_internal();
        memcpy(buffer, r->data, r->length);
        size_t used = r->length;
        if (!signed_text) buffer[used++] = '+';
        memcpy(buffer + used, i->data, i->length);
        used += i->length;
        buffer[used++] = 'i';
        NplVal result = npl_string(buffer, (uint32_t)used);
        free(buffer);
        npl_gc_unroot(&imag); npl_gc_unroot(&real); npl_gc_unroot(&value);
        return result;
    }
    if (npl_is_inexact(value)) return npl_inexact_string(value);
    return npl_number_to_string_radix(value, npl_fixnum(10));
}

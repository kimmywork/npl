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

static void numeric_error(const char *who, const char *message)
{
    fprintf(stderr, "%s: %s\n", who, message);
    exit(1);
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
    numeric_error(who, "expected an exact integer");
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
        numeric_error("integer-from-string", "invalid decimal integer");
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
    if (!npl_number_p(value)) numeric_error("exact->inexact", "expected a number");
    return npl_is_inexact(value) ? value : npl_float(npl_real_double(value));
}

NplVal npl_inexact_to_exact(NplVal value)
{
    if (!npl_number_p(value) || !npl_is_inexact(value))
        numeric_error("inexact->exact", "expected an inexact number");
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

static NplVal complex_from_string(NplVal value)
{
    const NplString *text = (const NplString *)npl_header(value);
    if (text->length < 4 || text->data[text->length - 1] != 'i')
        return npl_false();
    uint32_t split = 1;
    while (split + 1 < text->length && text->data[split] != '+' &&
           text->data[split] != '-') split++;
    if (split + 1 >= text->length) return npl_false();
    npl_gc_root(&value);
    NplVal real_text = npl_substring(value, npl_fixnum(0), npl_fixnum(split));
    NplVal imag_text = npl_substring(value, npl_fixnum(split),
                                      npl_fixnum(text->length - 1));
    npl_gc_root(&real_text); npl_gc_root(&imag_text);
    NplVal real = npl_number_from_string(real_text);
    NplVal imag = npl_number_from_string(imag_text);
    npl_gc_root(&real); npl_gc_root(&imag);
    NplVal result = (real == npl_false() || imag == npl_false())
        ? npl_false() : npl_complex_make(real, imag);
    npl_gc_unroot(&imag); npl_gc_unroot(&real);
    npl_gc_unroot(&imag_text); npl_gc_unroot(&real_text); npl_gc_unroot(&value);
    return result;
}

NplVal npl_number_from_string(NplVal value)
{
    if (!narrow_string(value)) return npl_false();
    const NplString *text = (const NplString *)npl_header(value);
    NplVal complex = complex_from_string(value);
    if (complex != npl_false()) return complex;
    if (text->length >= 3 && text->data[0] == '#') {
        unsigned char prefix = (unsigned char)text->data[1];
        int radix = prefix == 'b' || prefix == 'B' ? 2
                  : prefix == 'o' || prefix == 'O' ? 8
                  : prefix == 'd' || prefix == 'D' ? 10
                  : prefix == 'x' || prefix == 'X' ? 16 : 0;
        if (radix) return exact_number_from_string(value, radix, 2);
    }
    NplVal exact = npl_number_from_string_radix(value, npl_fixnum(10));
    return exact == npl_false() ? npl_inexact_parse(value) : exact;
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
    if (d.sign == 0) numeric_error("rational", "zero denominator");
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
        if (values[2] == npl_fixnum(0)) numeric_error("/", "division by zero");
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
    if (!npl_number_p(value)) return npl_false();
    return npl_number_compare(value, npl_fixnum(0)) == 0 ? npl_true() : npl_false();
}

NplVal npl_number_add(NplVal left, NplVal right)
{
    if (npl_is_complex(left) || npl_is_complex(right))
        return npl_complex_arithmetic(left, right, '+');
    if (npl_is_inexact(left) || npl_is_inexact(right))
        return npl_inexact_arithmetic(left, right, '+');
    if (is_rational_box(left) || is_rational_box(right))
        return rational_arithmetic(left, right, '+');
    if (npl_is_fixnum(left) && npl_is_fixnum(right)) {
        int64_t sum = npl_fixnum_value(left) + npl_fixnum_value(right);
        if (sum >= -(INT64_C(1) << 61) && sum < (INT64_C(1) << 61))
            return npl_fixnum(sum);
    }
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
    if (npl_is_complex(left) || npl_is_complex(right))
        return npl_complex_arithmetic(left, right, '-');
    if (npl_is_inexact(left) || npl_is_inexact(right))
        return npl_inexact_arithmetic(left, right, '-');
    if (is_rational_box(left) || is_rational_box(right))
        return rational_arithmetic(left, right, '-');
    if (npl_is_fixnum(left) && npl_is_fixnum(right)) {
        int64_t difference = npl_fixnum_value(left) - npl_fixnum_value(right);
        if (difference >= -(INT64_C(1) << 61) && difference < (INT64_C(1) << 61))
            return npl_fixnum(difference);
    }
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
    if (npl_is_complex(left) || npl_is_complex(right))
        numeric_error("number comparison", "complex number is unordered");
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
    if (npl_is_fixnum(left) && npl_is_fixnum(right)) {
        int64_t x = npl_fixnum_value(left), y = npl_fixnum_value(right);
        return x < y ? -1 : (x > y ? 1 : 0);
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
    return left == right ? npl_true() : npl_false();
}

/* Search each base-10^9 quotient digit using unsigned limb magnitudes.
 * Scratch storage is outside the GC; only the final result is boxed. */
static NplVal integer_divide(NplVal left, NplVal right, int want_remainder)
{
    const char *who = want_remainder ? "remainder" : "quotient";
    NplIntegerView a, b;
    integer_view(&a, left, who);
    integer_view(&b, right, who);
    if (b.sign == 0) numeric_error(who, "division by zero");
    if (a.sign == 0) return npl_fixnum(0);
    if (npl_is_fixnum(left) && npl_is_fixnum(right)) {
        int64_t x = npl_fixnum_value(left), y = npl_fixnum_value(right);
        if (want_remainder) return npl_fixnum(x % y);
        if (x != -(INT64_C(1) << 61) || y != -1)
            return npl_fixnum(x / y);
    }
    if (integer_magnitude_compare(&a, &b) < 0)
        return want_remainder ? left : npl_fixnum(0);
    uint64_t capacity = (uint64_t)b.count + 1;
    uint64_t quotient_capacity = a.count;
    if (capacity > UINT32_MAX || capacity > SIZE_MAX / sizeof(uint32_t) ||
        quotient_capacity > SIZE_MAX / sizeof(uint32_t))
        npl_runtime_oom_internal();
    uint32_t *digits = calloc(a.count, sizeof(uint32_t));
    uint32_t *rest = calloc((size_t)capacity, sizeof(uint32_t));
    uint32_t *product = calloc((size_t)capacity, sizeof(uint32_t));
    if (!digits || !rest || !product) npl_runtime_oom_internal();
    NplIntegerView r = {0}, p = {0};
    r.limbs = rest;
    p.limbs = product;
    for (uint32_t i = a.count; i != 0; i--) {
        memmove(rest + 1, rest, (size_t)r.count * sizeof(uint32_t));
        rest[0] = a.limbs[i - 1];
        r.count++;
        while (r.count && rest[r.count - 1] == 0) r.count--;
        uint32_t low = 0, high = NPL_BIG_BASE - 1;
        while (low < high) {
            uint32_t middle = low + (high - low + 1) / 2;
            uint64_t carry = 0;
            for (uint32_t j = 0; j < b.count; j++) {
                uint64_t limb = (uint64_t)b.limbs[j] * middle + carry;
                product[j] = (uint32_t)(limb % NPL_BIG_BASE);
                carry = limb / NPL_BIG_BASE;
            }
            product[b.count] = (uint32_t)carry;
            p.count = b.count + (carry != 0);
            if (integer_magnitude_compare(&p, &r) <= 0) low = middle;
            else high = middle - 1;
        }
        digits[i - 1] = low;
        uint64_t carry = 0;
        int64_t borrow = 0;
        for (uint32_t j = 0; j < r.count; j++) {
            uint64_t limb = carry;
            if (j < b.count) limb += (uint64_t)b.limbs[j] * low;
            carry = limb / NPL_BIG_BASE;
            int64_t difference = (int64_t)rest[j] -
                                 (int64_t)(limb % NPL_BIG_BASE) - borrow;
            borrow = difference < 0;
            rest[j] = (uint32_t)(difference + (borrow ? NPL_BIG_BASE : 0));
        }
        while (r.count && rest[r.count - 1] == 0) r.count--;
    }
    NplVal result = want_remainder
        ? box_integer_limbs(a.sign, rest, r.count)
        : box_integer_limbs(a.sign == b.sign ? 1 : -1, digits, a.count);
    free(product);
    free(rest);
    free(digits);
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

static NplVal integer_to_string_radix(NplVal value, int radix)
{
    NplIntegerView view;
    integer_view(&view, value, "number->string");
    static const char digits[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    int negative = view.sign < 0;
    NplVal current = value;
    npl_gc_root(&current);
    if (negative) current = npl_number_sub(npl_fixnum(0), current);
    size_t capacity = 32, length = 0;
    char *buffer = malloc(capacity);
    if (!buffer) npl_runtime_oom_internal();
    do {
        NplVal remainder = npl_number_remainder(current, npl_fixnum(radix));
        current = npl_number_quotient(current, npl_fixnum(radix));
        if (length == capacity) {
            capacity *= 2;
            char *grown = realloc(buffer, capacity);
            if (!grown) npl_runtime_oom_internal();
            buffer = grown;
        }
        buffer[length++] = digits[npl_fixnum_value(remainder)];
    } while (current != npl_fixnum(0));
    if (negative) {
        if (length == capacity) {
            capacity *= 2;
            char *grown = realloc(buffer, capacity);
            if (!grown) npl_runtime_oom_internal();
            buffer = grown;
        }
        buffer[length++] = '-';
    }
    for (size_t index = 0; index < length / 2; index++) {
        char swap = buffer[index];
        buffer[index] = buffer[length - index - 1];
        buffer[length - index - 1] = swap;
    }
    NplVal result = npl_string(buffer, (uint32_t)length);
    free(buffer);
    npl_gc_unroot(&current);
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
        int negative = npl_number_compare(npl_complex_imag(value),
                                          npl_fixnum(0)) < 0;
        uint64_t length = (uint64_t)r->length + 1 + i->length + 1;
        if (length > UINT32_MAX || length > SIZE_MAX) npl_runtime_oom_internal();
        char *buffer = malloc((size_t)length);
        if (!buffer) npl_runtime_oom_internal();
        memcpy(buffer, r->data, r->length);
        buffer[r->length] = negative ? '-' : '+';
        memcpy(buffer + r->length + 1, i->data + (negative ? 1 : 0),
               i->length - (negative ? 1 : 0));
        size_t used = r->length + 1 + i->length - (negative ? 1 : 0);
        buffer[used++] = 'i';
        NplVal result = npl_string(buffer, (uint32_t)used);
        free(buffer);
        npl_gc_unroot(&imag); npl_gc_unroot(&real); npl_gc_unroot(&value);
        return result;
    }
    if (npl_is_inexact(value)) return npl_inexact_string(value);
    return npl_number_to_string_radix(value, npl_fixnum(10));
}

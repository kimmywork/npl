/* number_read.c — exact and inexact number syntax: string->number and
 * the integer reader (R7RS 6.2.6, 7.1.1). Split from numeric.c, which owns
 * the representation and arithmetic. */

#include "npl.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define NPL_BIG_BASE UINT32_C(1000000000)

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
        npl_primitive_error("integer-from-string", "invalid decimal integer",
                            value);
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
    NplVal result = npl_box_integer_limbs_internal(sign, limbs, count);
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
        npl_primitive_error("exact->inexact", "expected a number", value);
    return npl_is_inexact(value) ? value : npl_float(npl_real_double(value));
}

NplVal npl_inexact_to_exact(NplVal value)
{
    if (npl_number_p(value) != npl_true() || !npl_is_inexact(value))
        npl_primitive_error("inexact->exact", "expected an inexact number",
                            value);
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

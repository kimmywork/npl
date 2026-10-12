/* Binary64 mechanisms shared by numeric dispatch, reader, and compiler data. */
#include "npl.h"
#include <complex.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int npl_is_inexact(NplVal value)
{
    return npl_is_ptr(value) && npl_header(value)->tag == BOX_FLOAT;
}

NplVal npl_inexact_p(NplVal value)
{
    return npl_is_inexact(value) ? npl_true() : npl_false();
}

NplVal npl_inexact_bits(NplVal value)
{
    if (!npl_is_inexact(value)) {
        fprintf(stderr, "inexact-bits: expected an inexact number\n");
        exit(1);
    }
    uint64_t bits;
    double real = ((NplFloat *)npl_header(value))->value;
    memcpy(&bits, &real, sizeof(bits));
    char text[32];
    int length = snprintf(text, sizeof(text), "%llu",
                          (unsigned long long)bits);
    return npl_integer_from_string(npl_string(text, (uint32_t)length));
}

static NplVal power_two(unsigned exponent)
{
    NplVal value = npl_fixnum(1), base = npl_fixnum(2);
    npl_gc_root(&value);
    npl_gc_root(&base);
    while (exponent) {
        if (exponent & 1) value = npl_number_mul(value, base);
        exponent >>= 1;
        if (exponent) base = npl_number_mul(base, base);
    }
    npl_gc_unroot(&base);
    npl_gc_unroot(&value);
    return value;
}

NplVal npl_real_exact(NplVal value)
{
    if (!npl_is_inexact(value)) return value;
    double x = ((NplFloat *)npl_header(value))->value;
    if (!isfinite(x)) return npl_false();
    if (x == 0) return npl_fixnum(0);
    int exponent;
    double fraction = frexp(x, &exponent);
    NplVal numerator = npl_fixnum((int64_t)ldexp(fraction, 53));
    npl_gc_root(&numerator);
    NplVal scale = power_two((unsigned)abs(exponent - 53));
    npl_gc_root(&scale);
    NplVal result = exponent >= 53 ? npl_number_mul(numerator, scale)
                                  : npl_rational_make(numerator, scale);
    npl_gc_unroot(&scale);
    npl_gc_unroot(&numerator);
    return result;
}

/* Estimate log2 from a leading limb; exact comparisons below correct it. */
static double integer_log2(NplVal value)
{
    if (npl_is_fixnum(value)) return log2((double)npl_fixnum_value(value));
    NplBigint *v = (NplBigint *)npl_header(value);
    return log2((double)v->limbs[v->count - 1]) + (v->count - 1) * log2(1e9);
}

double npl_real_double(NplVal value)
{
    if (npl_is_inexact(value)) return ((NplFloat *)npl_header(value))->value;
    if (npl_is_fixnum(value)) return (double)npl_fixnum_value(value);
    NplVal n = npl_rational_numerator(value), d = npl_rational_denominator(value);
    NplVal scale = npl_nil(), q = npl_nil(), r = npl_nil();
    npl_gc_root(&n); npl_gc_root(&d); npl_gc_root(&scale);
    npl_gc_root(&q); npl_gc_root(&r);
    int negative = npl_number_compare(n, npl_fixnum(0)) < 0;
    if (negative) n = npl_number_sub(npl_fixnum(0), n);
    double estimate = integer_log2(n) - integer_log2(d);
    double result;
    if (estimate > 1025) result = INFINITY;
    else if (estimate < -1076) result = 0;
    else {
        int exponent = (int)floor(estimate);
        scale = power_two((unsigned)abs(exponent));
        if (exponent >= 0) d = npl_number_mul(d, scale);
        else n = npl_number_mul(n, scale);
        while (npl_number_compare(n, d) < 0) {
            n = npl_number_mul(n, npl_fixnum(2)); exponent--;
        }
        q = npl_number_mul(d, npl_fixnum(2));
        while (npl_number_compare(n, q) >= 0) {
            d = q; exponent++;
            q = npl_number_mul(d, npl_fixnum(2));
        }
        /* Subnormals round on the fixed 2^-1074 grid. */
        int bits = exponent < -1022 ? exponent + 1074 : 52;
        scale = power_two((unsigned)abs(bits));
        if (bits >= 0) n = npl_number_mul(n, scale);
        else d = npl_number_mul(d, scale);
        q = npl_number_quotient(n, d);
        r = npl_number_remainder(n, d);
        r = npl_number_mul(r, npl_fixnum(2));
        int order = npl_number_compare(r, d);
        int64_t significand = npl_fixnum_value(q);
        if (order > 0 || (order == 0 && (significand & 1))) significand++;
        result = ldexp((double)significand, exponent - bits);
    }
    npl_gc_unroot(&r); npl_gc_unroot(&q); npl_gc_unroot(&scale);
    npl_gc_unroot(&d); npl_gc_unroot(&n);
    return negative ? -result : result;
}

NplVal npl_inexact_arithmetic(NplVal left, NplVal right, char operation)
{
    npl_gc_root(&left); npl_gc_root(&right);
    double a = npl_real_double(left), b = npl_real_double(right);
    double result = operation == '+' ? a + b : operation == '-' ? a - b
                  : operation == '*' ? a * b : a / b;
    npl_gc_unroot(&right); npl_gc_unroot(&left);
    return npl_float(result);
}

/* 2 denotes unordered; callers must exclude it for every ordered relation. */
int32_t npl_inexact_compare(NplVal left, NplVal right)
{
    if (npl_is_inexact(left) && npl_is_inexact(right)) {
        double a = npl_real_double(left), b = npl_real_double(right);
        return isnan(a) || isnan(b) ? 2 : a < b ? -1 : a > b ? 1 : 0;
    }
    int left_float = npl_is_inexact(left);
    double x = npl_real_double(left_float ? left : right);
    if (isnan(x)) return 2;
    if (isinf(x)) return (x < 0 ? -1 : 1) * (left_float ? 1 : -1);
    npl_gc_root(&left); npl_gc_root(&right);
    if (left_float) left = npl_real_exact(left);
    else right = npl_real_exact(right);
    int32_t result = npl_number_compare(left, right);
    npl_gc_unroot(&right); npl_gc_unroot(&left);
    return result;
}

NplVal npl_inexact_parse(NplVal value)
{
    const NplString *text = (const NplString *)npl_header(value);
    const char *s = text->data;
    uint32_t length = text->length, i = 0, digits = 0;
    if (length == 6 && (s[0] == '+' || s[0] == '-')) {
        /* <infnan> is case-insensitive like the rest of number syntax. */
        char lower[5];
        for (int k = 0; k < 5; k++)
            lower[k] = (s[1 + k] >= 'A' && s[1 + k] <= 'Z')
                ? (char)(s[1 + k] - 'A' + 'a') : s[1 + k];
        if (memcmp(lower, "inf.0", 5) == 0) return npl_float(s[0] == '-' ? -INFINITY : INFINITY);
        if (memcmp(lower, "nan.0", 5) == 0) return npl_float(NAN);
    }
    if (i < length && (s[i] == '+' || s[i] == '-')) i++;
    while (i < length && s[i] >= '0' && s[i] <= '9') { i++; digits++; }
    int marker = 0;
    if (i < length && s[i] == '.') {
        i++; marker = 1;
        while (i < length && s[i] >= '0' && s[i] <= '9') { i++; digits++; }
    }
    if (!digits) return npl_false();
    if (i < length && (s[i] == 'e' || s[i] == 'E')) {
        i++; marker = 1;
        if (i < length && (s[i] == '+' || s[i] == '-')) i++;
        uint32_t start = i;
        while (i < length && s[i] >= '0' && s[i] <= '9') i++;
        if (i == start) return npl_false();
    }
    if (!marker || i != length) return npl_false();
    return npl_float(strtod(s, NULL));
}

NplVal npl_inexact_string(NplVal value)
{
    double x = npl_real_double(value);
    if (isnan(x)) return npl_string("+nan.0", 6);
    if (isinf(x)) return npl_string(x < 0 ? "-inf.0" : "+inf.0", 6);
    char buffer[64];
    /* Keep ordinary integral binary64 values in decimal rather than %g's e form. */
    if (trunc(x) == x && fabs(x) < 1e15) {
        snprintf(buffer, sizeof(buffer), "%.1f", x);
        return npl_string(buffer, (uint32_t)strlen(buffer));
    }
    /* Find a decimal precision that preserves all binary64 bits. */
    for (int precision = 1; precision <= 17; precision++) {
        snprintf(buffer, sizeof(buffer), "%.*g", precision, x);
        if (strtod(buffer, NULL) == x) break;
    }
    /* Scheme form, as Guile writes: the mantissa always has a decimal point
     * ("5.0e-324") and the exponent no '+' or leading zeros ("1.0e21"). */
    char *e = strchr(buffer, 'e');
    if (!e) {
        if (!strchr(buffer, '.')) strcat(buffer, ".0");
        return npl_string(buffer, (uint32_t)strlen(buffer));
    }
    char exponent[16];
    const char *digits = e + 1;
    int negative = *digits == '-';
    if (*digits == '+' || *digits == '-') digits++;
    while (*digits == '0' && digits[1]) digits++;
    snprintf(exponent, sizeof(exponent), "e%s%s", negative ? "-" : "", digits);
    *e = '\0';
    if (!strchr(buffer, '.')) strcat(buffer, ".0");
    strcat(buffer, exponent);
    return npl_string(buffer, (uint32_t)strlen(buffer));
}

NplVal npl_finite_p(NplVal value)
{
    if (npl_complex_p(value) == npl_true())
        return npl_finite_p(npl_complex_real(value)) == npl_true() &&
               npl_finite_p(npl_complex_imag(value)) == npl_true()
            ? npl_true() : npl_false();
    return npl_is_inexact(value)
        ? (isfinite(npl_real_double(value)) ? npl_true() : npl_false())
        : npl_number_p(value);
}

NplVal npl_nan_p(NplVal value)
{
    if (npl_complex_p(value) == npl_true())
        return npl_nan_p(npl_complex_real(value)) == npl_true() ||
               npl_nan_p(npl_complex_imag(value)) == npl_true()
            ? npl_true() : npl_false();
    return npl_is_inexact(value) && isnan(npl_real_double(value))
        ? npl_true() : npl_false();
}

NplVal npl_infinite_p(NplVal value)
{
    if (npl_complex_p(value) == npl_true())
        return npl_infinite_p(npl_complex_real(value)) == npl_true() ||
               npl_infinite_p(npl_complex_imag(value)) == npl_true()
            ? npl_true() : npl_false();
    return npl_is_inexact(value) && isinf(npl_real_double(value))
        ? npl_true() : npl_false();
}

/* libc supplies the numerical algorithms; NPL validates the public calls. */
static NplVal inexact_math(int code, NplVal value)
{
    npl_gc_root(&value);
    double real = npl_real_double(npl_complex_real(value));
    int rectangular = npl_complex_p(value) == npl_true();
    double imag = rectangular ? npl_real_double(npl_complex_imag(value)) : 0.0;
    npl_gc_unroot(&value);
    int complex_domain = rectangular ||
        ((code == 11 || code == 12) && fabs(real) > 1.0) ||
        (code == 14 && signbit(real)) || (code == 15 && real < 0.0);
    if (!complex_domain) {
        double result;
        switch (code) {
        case 8: result = sin(real); break;
        case 9: result = cos(real); break;
        case 10: result = tan(real); break;
        case 11: result = asin(real); break;
        case 12: result = acos(real); break;
        case 13: result = exp(real); break;
        case 14: result = log(real); break;
        case 15: result = sqrt(real); break;
        default: result = atan(real); break;
        }
        return npl_float(result);
    }
    /* C99 specifies the same representation as a two-element real array.
     * Avoid real + I*imag, which can lose signed zero or contaminate infinities. */
    double parts[2] = {real, imag};
    double complex argument, result;
    memcpy(&argument, parts, sizeof(argument));
    switch (code) {
    case 8: result = csin(argument); break;
    case 9: result = ccos(argument); break;
    case 10: result = ctan(argument); break;
    case 11: result = casin(argument); break;
    case 12: result = cacos(argument); break;
    case 13: result = cexp(argument); break;
    case 14: result = clog(argument); break;
    case 15: result = csqrt(argument); break;
    default: result = catan(argument); break;
    }
    double result_real = creal(result);
    double result_imag = cimag(result);
    if ((code == 11 || code == 12) && isfinite(real) &&
        fabs(real) > 1.0 && imag == 0.0) {
        if (code == 11)
            result_imag = real < 0.0 ? fabs(result_imag) : -fabs(result_imag);
        else
            result_imag = real < 0.0 ? -fabs(result_imag) : fabs(result_imag);
    }
    NplVal real_part = npl_float(result_real);
    npl_gc_root(&real_part);
    NplVal imag_part = npl_float(result_imag);
    npl_gc_root(&imag_part);
    NplVal boxed = npl_complex_make(real_part, imag_part);
    npl_gc_unroot(&imag_part);
    npl_gc_unroot(&real_part);
    return boxed;
}

/* Unary raw operations. Public argument/domain policy stays in NPL. */
NplVal npl_inexact_op(NplVal operation, NplVal value)
{
    int code = (int)npl_fixnum_value(operation);
    int boxed = npl_is_inexact(value);
    if (code == 0) return boxed ? npl_true() : npl_false();
    double x = boxed ? npl_real_double(value) : 0;
    if (code == 1) return npl_finite_p(value);
    if (code == 2) return npl_nan_p(value);
    if (code == 3) return npl_infinite_p(value);
    if (code == 4) return boxed ? value : npl_float(npl_real_double(value));
    if (code == 5) return npl_real_exact(value);
    if (code == 6) return boxed && isfinite(x) && trunc(x) == x ? npl_true() : npl_false();
    if (code == 7) {
        uint64_t bits;
        memcpy(&bits, &x, sizeof(bits));
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%llu", (unsigned long long)bits);
        return npl_string(buffer, (uint32_t)strlen(buffer));
    }
    if (code >= 8 && code <= 16) return inexact_math(code, value);
    fprintf(stderr, "inexact operation: invalid operation\n");
    exit(1);
}

NplVal npl_inexact_binary(NplVal operation, NplVal left, NplVal right)
{
    int code = (int)npl_fixnum_value(operation);
    if (code == 0) {
        npl_gc_root(&left);
        npl_gc_root(&right);
        double y = npl_real_double(left);
        double x = npl_real_double(right);
        npl_gc_unroot(&right);
        npl_gc_unroot(&left);
        return npl_float(atan2(y, x));
    }
    fprintf(stderr, "inexact binary operation: invalid operation\n");
    exit(1);
}

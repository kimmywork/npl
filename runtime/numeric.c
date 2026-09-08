/* numeric.c — exact integer representation and arithmetic substrate. */

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

NplVal npl_integer_string_p(NplVal value)
{
    if (!npl_is_ptr(value) || npl_header(value)->tag != BOX_STRING)
        return npl_false();
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

NplVal npl_number_p(NplVal value)
{
    return (npl_is_fixnum(value) ||
            (npl_is_ptr(value) && npl_header(value)->tag == BOX_BIGNUM))
        ? npl_true() : npl_false();
}

NplVal npl_number_add(NplVal left, NplVal right)
{
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
    if (npl_number_p(left) == npl_true() && npl_number_p(right) == npl_true())
        return npl_number_compare(left, right) == 0 ? npl_true() : npl_false();
    return left == right ? npl_true() : npl_false();
}

NplVal npl_number_to_string(NplVal value)
{
    if (npl_is_fixnum(value)) {
        char buffer[32];
        int length = snprintf(buffer, sizeof(buffer), "%lld",
                              (long long)npl_fixnum_value(value));
        return npl_string(buffer, (uint32_t)length);
    }
    NplIntegerView view;
    integer_view(&view, value, "number->string");
    if (view.count > (UINT32_MAX - 2) / 9)
        npl_runtime_oom_internal();
    size_t capacity = (size_t)view.count * 9 + 2;
    char *buffer = (char *)malloc(capacity);
    if (!buffer) npl_runtime_oom_internal();
    char *cursor = buffer;
    if (view.sign < 0) *cursor++ = '-';
    cursor += snprintf(cursor, capacity - (size_t)(cursor - buffer),
                       "%u", view.limbs[view.count - 1]);
    for (uint32_t index = view.count - 1; index != 0; index--)
        cursor += snprintf(cursor, capacity - (size_t)(cursor - buffer),
                           "%09u", view.limbs[index - 1]);
    NplVal result = npl_string(buffer, (uint32_t)(cursor - buffer));
    free(buffer);
    return result;
}

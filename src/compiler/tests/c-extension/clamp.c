/* docs/ffi.md example: C functions declared with define-c-primitive in
 * main.npl and linked next to the runtime archive. */
#include "npl.h"

/* (clamp x lo hi) on fixnums. */
NplVal demo_clamp(NplVal x, NplVal lo, NplVal hi)
{
    if (!npl_is_fixnum(x)) npl_primitive_error("clamp", "expected a fixnum", x);
    if (!npl_is_fixnum(lo)) npl_primitive_error("clamp", "expected a fixnum", lo);
    if (!npl_is_fixnum(hi)) npl_primitive_error("clamp", "expected a fixnum", hi);
    int64_t value = npl_fixnum_value(x);
    if (value < npl_fixnum_value(lo)) return lo;
    if (value > npl_fixnum_value(hi)) return hi;
    return x;
}

/* (pair-up a b) is (a b). tail stays live on the C stack, which the
 * collector scans, while the second cons allocates. */
NplVal demo_pair_up(NplVal a, NplVal b)
{
    NplVal tail = npl_cons(b, npl_nil());
    return npl_cons(a, tail);
}

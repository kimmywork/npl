/* A C function returning several values, for the values result type of
 * define-c-primitive (docs/ffi.md). */
#include "npl.h"

/* (divmod a b) returns the quotient and the remainder. */
NplVal demo_divmod(NplVal a, NplVal b)
{
    if (!npl_is_fixnum(a)) npl_primitive_error("divmod", "expected a fixnum", a);
    if (!npl_is_fixnum(b) || npl_fixnum_value(b) == 0)
        npl_primitive_error("divmod", "expected a nonzero fixnum", b);
    NplVal parts[2];
    parts[0] = npl_fixnum(npl_fixnum_value(a) / npl_fixnum_value(b));
    parts[1] = npl_fixnum(npl_fixnum_value(a) % npl_fixnum_value(b));
    return npl_make_values(2, parts);
}

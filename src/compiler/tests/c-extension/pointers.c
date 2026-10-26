/* Pointer values for the (npl ffi) tests: a counted C resource, so the
 * tests can see each release. */
#include "npl.h"
#include <stdlib.h>

static int64_t g_open_resources = 0;

/* (demo-open n) is a pointer to a malloc'd int64_t holding n. */
NplVal demo_open(NplVal n)
{
    if (!npl_is_fixnum(n)) npl_primitive_error("demo-open", "expected a fixnum", n);
    int64_t *cell = (int64_t *)malloc(sizeof *cell);
    if (!cell) npl_primitive_error("demo-open", "out of memory", n);
    *cell = npl_fixnum_value(n);
    g_open_resources++;
    return npl_pointer(cell);
}

NplVal demo_value(NplVal resource)
{
    int64_t *cell = (int64_t *)npl_pointer_address(resource, "demo-value");
    if (!cell) npl_primitive_error("demo-value", "null pointer", resource);
    return npl_fixnum(*cell);
}

NplVal demo_close(NplVal resource)
{
    free(npl_pointer_address(resource, "demo-close"));
    g_open_resources--;
    return npl_unspecified();
}

NplVal demo_open_count(void)
{
    return npl_fixnum(g_open_resources);
}

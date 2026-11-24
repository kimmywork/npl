/* Typed struct argument test helper: a plain C function with a struct
 * pointer, linked beside the generated stubs. */

#define _POSIX_C_SOURCE 200809L

#include <stddef.h>
#include <stdlib.h>
#include <time.h>

int npl_time_now_ts(int which, struct timespec *out)
{
    return clock_gettime(which ? CLOCK_MONOTONIC : CLOCK_REALTIME, out);
}

long npl_time_timespec_size(void)
{
    return (long)sizeof(struct timespec);
}

/* A typedef-name struct (ldiv_t) as a define-c-function argument. */
long npl_ldiv_quot(ldiv_t *value)
{
    return (long)value->quot;
}

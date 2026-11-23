/* Typed struct argument test helper: a plain C function with a struct
 * pointer, linked beside the generated stubs. */

#define _POSIX_C_SOURCE 200809L

#include <time.h>

int npl_time_now_ts(int which, struct timespec *out)
{
    return clock_gettime(which ? CLOCK_MONOTONIC : CLOCK_REALTIME, out);
}

/* Clocks for (scheme time) (R7RS 6.14). */

#define _POSIX_C_SOURCE 200809L

#include "npl.h"
#include <time.h>

/* Jiffies are microseconds of a monotonic clock, as exact integers. */
#define NPL_JIFFIES_PER_SECOND 1000000

NplVal npl_jiffies_per_second(void)
{
    return npl_fixnum(NPL_JIFFIES_PER_SECOND);
}

NplVal npl_current_jiffy(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        npl_primitive_error("current-jiffy", "clock unavailable", npl_nil());
    return npl_fixnum((int64_t)now.tv_sec * NPL_JIFFIES_PER_SECOND +
                      now.tv_nsec / 1000);
}

/* TAI is approximated by POSIX time (UTC without leap seconds), as R7RS
 * permits; the result is an inexact number of seconds. */
NplVal npl_current_second(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_REALTIME, &now) != 0)
        npl_primitive_error("current-second", "clock unavailable", npl_nil());
    return npl_float((double)now.tv_sec + (double)now.tv_nsec / 1e9);
}

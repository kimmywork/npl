/* (npl time) native entry points (core package, linked mode A).
 *
 * The public library declares these with define-c-primitive; nplc emits
 * direct calls and links libnpl_time, while npli links the shared object and
 * resolves the same symbols.  Struct views come from the F2 layer.
 */

#define _DARWIN_C_SOURCE
#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "npl.h"

#include <errno.h>
#include <time.h>

static struct timespec *time_view(NplVal value, const char *who)
{
    return (struct timespec *)npl_ffi_struct_pointer(value, "struct timespec", who);
}

static struct tm *time_tm_view(NplVal value, const char *who)
{
    return (struct tm *)npl_ffi_struct_pointer(value, "struct tm", who);
}

NplVal npl_time_now_prim(NplVal which, NplVal view)
{
    struct timespec *out;
    if (!npl_is_fixnum(which))
        npl_primitive_error("current-time", "expected a clock selector", which);
    out = time_view(view, "current-time");
    return npl_fixnum(clock_gettime(
        npl_fixnum_value(which) ? CLOCK_MONOTONIC : CLOCK_REALTIME, out));
}

NplVal npl_time_resolution_prim(NplVal which, NplVal view)
{
    struct timespec *out;
    if (!npl_is_fixnum(which))
        npl_primitive_error("time-resolution", "expected a clock selector",
                            which);
    out = time_view(view, "time-resolution");
    return npl_fixnum(clock_getres(
        npl_fixnum_value(which) ? CLOCK_MONOTONIC : CLOCK_REALTIME, out));
}

NplVal npl_time_gmtime_prim(NplVal value, NplVal out)
{
    struct timespec *stamp = time_view(value, "time-utc->date");
    time_t seconds = (time_t)stamp->tv_sec;
    return npl_fixnum(gmtime_r(&seconds, time_tm_view(out, "time-utc->date"))
                          ? 0 : -1);
}

NplVal npl_time_localtime_prim(NplVal value, NplVal out)
{
    struct timespec *stamp = time_view(value, "time-utc->date");
    time_t seconds = (time_t)stamp->tv_sec;
    return npl_fixnum(
        localtime_r(&seconds, time_tm_view(out, "time-utc->date")) ? 0 : -1);
}

NplVal npl_time_mktime_prim(NplVal value, NplVal out)
{
    struct timespec *stamp = time_view(out, "date->time-utc");
    time_t seconds;
    /* POSIX mktime/timegm set errno on failure.  A legitimate -1 result
     * (1969-12-31 23:59:59) with errno still 0 is a success. */
    errno = 0;
    seconds = mktime(time_tm_view(value, "date->time-utc"));
    if (seconds == (time_t)-1 && errno != 0) return npl_fixnum(-1);
    stamp->tv_sec = seconds;
    stamp->tv_nsec = 0;
    return npl_fixnum(0);
}

NplVal npl_time_timegm_prim(NplVal value, NplVal out)
{
    struct timespec *stamp = time_view(out, "date->time-utc");
    time_t seconds;
    /* Same POSIX contract as mktime above. */
    errno = 0;
    seconds = timegm(time_tm_view(value, "date->time-utc"));
    if (seconds == (time_t)-1 && errno != 0) return npl_fixnum(-1);
    stamp->tv_sec = seconds;
    stamp->tv_nsec = 0;
    return npl_fixnum(0);
}

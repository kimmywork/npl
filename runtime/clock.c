/* Clocks for (scheme time) (R7RS 6.14). */

#define _DARWIN_C_SOURCE
#define _GNU_SOURCE
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

/* Typed wrappers for (npl time).  They take the F2 struct views' addresses
 * from define-c-function stubs; the clock ids stay in C. */
int npl_time_now_ts(int which, struct timespec *out)
{
    return clock_gettime(which ? CLOCK_MONOTONIC : CLOCK_REALTIME, out);
}

int npl_time_resolution_ts(int which, struct timespec *out)
{
    return clock_getres(which ? CLOCK_MONOTONIC : CLOCK_REALTIME, out);
}

int npl_time_gmtime_ts(const struct timespec *value, struct tm *out)
{
    time_t seconds = (time_t)value->tv_sec;
    return gmtime_r(&seconds, out) ? 0 : -1;
}

int npl_time_localtime_ts(const struct timespec *value, struct tm *out)
{
    time_t seconds = (time_t)value->tv_sec;
    return localtime_r(&seconds, out) ? 0 : -1;
}

int npl_time_mktime_ts(struct tm *value, struct timespec *out)
{
    time_t seconds = mktime(value);
    if (seconds == (time_t)-1) return -1;
    out->tv_sec = seconds;
    out->tv_nsec = 0;
    return 0;
}

int npl_time_timegm_ts(struct tm *value, struct timespec *out)
{
    time_t seconds = timegm(value);
    if (seconds == (time_t)-1) return -1;
    out->tv_sec = seconds;
    out->tv_nsec = 0;
    return 0;
}

/* Primitive entry points for (npl time): the struct views come from F2. */
static struct timespec *time_view(NplVal value, const char *who)
{
    return (struct timespec *)npl_ffi_struct_pointer(value, who);
}

static struct tm *time_tm_view(NplVal value, const char *who)
{
    return (struct tm *)npl_ffi_struct_pointer(value, who);
}

NplVal npl_time_now_prim(NplVal which, NplVal view)
{
    if (!npl_is_fixnum(which))
        npl_primitive_error("current-time", "expected a clock selector", which);
    return npl_fixnum(npl_time_now_ts((int)npl_fixnum_value(which),
                                      time_view(view, "current-time")));
}

NplVal npl_time_resolution_prim(NplVal which, NplVal view)
{
    if (!npl_is_fixnum(which))
        npl_primitive_error("time-resolution", "expected a clock selector",
                            which);
    return npl_fixnum(npl_time_resolution_ts((int)npl_fixnum_value(which),
                                             time_view(view, "time-resolution")));
}

NplVal npl_time_gmtime_prim(NplVal value, NplVal out)
{
    return npl_fixnum(npl_time_gmtime_ts(time_view(value, "time-utc->date"),
                                         time_tm_view(out, "time-utc->date")));
}

NplVal npl_time_localtime_prim(NplVal value, NplVal out)
{
    return npl_fixnum(npl_time_localtime_ts(time_view(value, "time-utc->date"),
                                            time_tm_view(out,
                                                         "time-utc->date")));
}

NplVal npl_time_mktime_prim(NplVal value, NplVal out)
{
    return npl_fixnum(npl_time_mktime_ts(time_tm_view(value, "date->time-utc"),
                                         time_view(out, "date->time-utc")));
}

NplVal npl_time_timegm_prim(NplVal value, NplVal out)
{
    return npl_fixnum(npl_time_timegm_ts(time_tm_view(value, "date->time-utc"),
                                         time_view(out, "date->time-utc")));
}

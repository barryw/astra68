/*
 * The NDK's clock.
 *
 * A thin thing on purpose: one syscall, and the calendar that the kernel's
 * boot line, `ls -l` and `date` already share. What it adds is the shape an
 * application wants -- both renderings of one reading, so a window that shows
 * a local clock and a log that records UTC never disagree about which second
 * they were describing.
 */

#include <astra/datetime.h>

#include <astra/runtime.h>
#include <astra/syscall.h>

#include <string.h>
#include <time.h>

#define NANOSECONDS_PER_SECOND 1000000000u
#define DEFAULT_FORMAT "%Y-%m-%d %H:%M:%S %Z"

bool astra_datetime_now(AstraDateTime *out)
{
    AstraDateTime reading;

    if (out == NULL)
        return false;
    memset(&reading, 0, sizeof(reading));
    if (astra_clock_realtime_zone(&reading.unix_nanoseconds, &reading.zone) !=
        ASTRA_SYSCALL_OK)
        return false;
    if (!astra_civil_from_unix_ns_zone(reading.unix_nanoseconds,
                                       &reading.zone, &reading.local))
        return false;
    if (!astra_civil_from_unix_ns(reading.unix_nanoseconds, &reading.utc))
        return false;
    *out = reading;
    return true;
}

bool astra_datetime_unix_seconds(uint64_t *seconds)
{
    uint64_t nanoseconds = 0u;

    if (seconds == NULL ||
        astra_clock_realtime(&nanoseconds) != ASTRA_SYSCALL_OK)
        return false;
    *seconds = nanoseconds / NANOSECONDS_PER_SECOND;
    return true;
}

size_t astra_datetime_format(const AstraCivilTime *civil, const char *format,
                             char *out, size_t capacity)
{
    struct tm rendered;

    if (civil == NULL || out == NULL || capacity == 0u)
        return 0u;
    memset(&rendered, 0, sizeof(rendered));
    rendered.tm_year = (int)civil->year - 1900;
    rendered.tm_mon = (int)civil->month - 1;
    rendered.tm_mday = (int)civil->day;
    rendered.tm_hour = (int)civil->hour;
    rendered.tm_min = (int)civil->minute;
    rendered.tm_sec = (int)civil->second;
    rendered.tm_wday = (int)civil->weekday;
    rendered.tm_yday = (int)civil->year_day - 1;
    rendered.tm_isdst = -1;
#ifdef __TM_GMTOFF
    rendered.__TM_GMTOFF = civil->utc_offset;
#endif
#ifdef __TM_ZONE
    rendered.__TM_ZONE = civil->zone;
#endif
    {
        char expanded[128];

        /* %Z and %z come from the instant, not from a TZ nobody set. */
        if (astra_civil_expand_zone(format != NULL ? format : DEFAULT_FORMAT,
                                    civil, expanded, sizeof(expanded)) == 0u)
            return 0u;
        return strftime(out, capacity, expanded, &rendered);
    }
}

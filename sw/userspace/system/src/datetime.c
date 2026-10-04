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

#include <astra/divide.h>
#include <astra/runtime.h>
#include <astra/syscall.h>

#include <string.h>

#define NANOSECONDS_PER_SECOND 1000000000u

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
    *seconds = astra_divide_u64(nanoseconds, NANOSECONDS_PER_SECOND, NULL);
    return true;
}


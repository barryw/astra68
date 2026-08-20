/*
 * The calendar, against the host's.
 *
 * A hand-written civil-time conversion is the kind of code that looks right
 * for a decade and then puts February 29th in a year that has none, so this
 * does not check a handful of instants a person chose: it sweeps, and it
 * compares every field against `gmtime_r`, which is the same question answered
 * by code nobody here wrote.
 *
 * The sweep is deliberately uneven -- a prime step -- so it lands inside
 * months, on boundaries, and in leap years rather than marching in step with
 * anything the algorithm divides by.
 */

#define _POSIX_C_SOURCE 200809L

#include <astra/civil.h>

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define STEP 86399u          /* one second short of a day: drifts through it */
#define SAMPLES 200000u      /* about 190 years of instants */

static void check(uint64_t seconds)
{
    AstraCivilTime civil;
    struct tm expected;
    time_t value = (time_t)seconds;

    assert(astra_civil_from_unix_seconds(seconds, &civil));
    assert(gmtime_r(&value, &expected) != NULL);
    assert(civil.year == expected.tm_year + 1900);
    assert(civil.month == expected.tm_mon + 1);
    assert(civil.day == expected.tm_mday);
    assert(civil.hour == expected.tm_hour);
    assert(civil.minute == expected.tm_min);
    assert(civil.second == expected.tm_sec);
    assert(civil.weekday == expected.tm_wday);
    assert(civil.year_day == expected.tm_yday + 1);
}

int main(void)
{
    AstraCivilTime civil;
    char text[32];

    /* The instants a calendar gets wrong, named rather than swept into. */
    check(0u);                    /* 1970-01-01T00:00:00Z, a Thursday */
    check(951782400u);            /* 2000-02-29, the leap year a century rule
                                   * would have skipped */
    check(4107542400u);           /* 2100-02-28, the century that is not one */
    check(1709164800u);           /* 2024-02-29 */
    check(1735689599u);           /* 2024-12-31T23:59:59Z */
    check(1735689600u);           /* 2025-01-01T00:00:00Z */

    for (uint64_t index = 0u; index < SAMPLES; ++index)
        check(index * STEP);

    /* Nanoseconds survive the split and do not disturb the seconds. */
    assert(astra_civil_from_unix_ns(1735689600u * 1000000000ull + 123456789u,
                                    &civil));
    assert(civil.year == 2025 && civil.month == 1 && civil.day == 1);
    assert(civil.nanosecond == 123456789u);

    assert(astra_civil_iso8601(1735689600u * 1000000000ull, text,
                               sizeof(text)) == 20u);
    assert(strcmp(text, "2025-01-01T00:00:00Z") == 0);
    /* A buffer one byte short writes nothing rather than most of a date. */
    assert(astra_civil_iso8601(0u, text, 20u) == 0u);

    assert(strcmp(astra_civil_month_name(1u), "Jan") == 0);
    assert(strcmp(astra_civil_month_name(12u), "Dec") == 0);
    assert(astra_civil_month_name(0u) == NULL);
    assert(astra_civil_month_name(13u) == NULL);

    puts("CIVIL TIME PASS");
    return 0;
}

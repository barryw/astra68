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

    /*
     * A zone is an offset applied to the instant, not to the rendered fields,
     * so the cases that matter are the ones that cross something: a day, a
     * month, a year, and a half-hour zone that is nobody's round number.
     */
    {
        static const AstraTimeZone eastern = { -4 * 3600, "EDT" };
        static const AstraTimeZone kolkata = { 5 * 3600 + 1800, "IST" };
        static const AstraTimeZone utc_zone = ASTRA_TIME_ZONE_UTC;
        AstraCivilTime local;

        /* 2026-01-01T02:30:00Z is still 2025 in New York. */
        assert(astra_civil_from_unix_ns_zone(
            UINT64_C(1767234600) * 1000000000ull, &eastern, &local));
        assert(local.year == 2025 && local.month == 12 && local.day == 31);
        assert(local.hour == 22 && local.minute == 30);
        assert(local.utc_offset == -4 * 3600);
        assert(strcmp(local.zone, "EDT") == 0);
        assert(astra_civil_from_unix_seconds_zone(
            UINT64_C(1767234600), &eastern, &local));
        assert(local.year == 2025 && local.month == 12 && local.day == 31);
        assert(local.hour == 22 && local.minute == 30);
        assert(local.nanosecond == 0u && strcmp(local.zone, "EDT") == 0);

        /* The same instant in Kolkata is the next morning, on the half hour. */
        assert(astra_civil_from_unix_ns_zone(
            UINT64_C(1767234600) * 1000000000ull, &kolkata, &local));
        assert(local.year == 2026 && local.month == 1 && local.day == 1);
        assert(local.hour == 8 && local.minute == 0);

        /* No zone is UTC, and says so rather than leaving the name empty. */
        assert(astra_civil_from_unix_ns_zone(
            UINT64_C(1767234600) * 1000000000ull, NULL, &local));
        assert(local.hour == 2 && local.minute == 30);
        assert(strcmp(local.zone, "UTC") == 0);
        assert(astra_civil_from_unix_ns_zone(0u, &utc_zone, &local));
        assert(local.year == 1970);
        /* West of Greenwich at the epoch is before it, and is refused. */
        assert(!astra_civil_from_unix_ns_zone(0u, &eastern, &local));
        assert(!astra_civil_from_unix_seconds_zone(0u, &eastern, &local));
    }

    /* The packed name the machine reports, and what an empty one means. */
    {
        AstraTimeZone zone;

        astra_civil_zone_unpack(-18000, 0x45535400u, &zone); /* "EST" */
        assert(strcmp(zone.name, "EST") == 0);
        assert(zone.utc_offset == -18000);
        astra_civil_zone_unpack(0x43455354u, 0x43455354u, &zone); /* "CEST" */
        assert(strcmp(zone.name, "CEST") == 0);
        /* Nothing reported is UTC, and the offset goes with the name. */
        astra_civil_zone_unpack(3600, 0u, &zone);
        assert(strcmp(zone.name, "UTC") == 0 && zone.utc_offset == 0);
    }

    /*
     * %Z and %z are substituted here because picolibc's tm carries no zone.
     * Everything else has to pass through untouched, including a literal
     * percent in front of a Z.
     */
    {
        static const AstraTimeZone eastern = { -4 * 3600, "EDT" };
        static const AstraTimeZone kolkata = { 5 * 3600 + 1800, "IST" };
        AstraCivilTime local;
        char expanded[64];

        assert(astra_civil_from_unix_ns_zone(
            UINT64_C(1767234600) * 1000000000ull, &eastern, &local));
        assert(astra_civil_expand_zone("%Y-%m-%d %H:%M %Z %z", &local,
                                       expanded, sizeof(expanded)) != 0u);
        assert(strcmp(expanded, "%Y-%m-%d %H:%M EDT -0400") == 0);
        assert(astra_civil_from_unix_ns_zone(
            UINT64_C(1767234600) * 1000000000ull, &kolkata, &local));
        assert(astra_civil_expand_zone("%z", &local, expanded,
                                       sizeof(expanded)) != 0u);
        assert(strcmp(expanded, "+0530") == 0);
        /* A literal percent is not a specifier, and neither is what follows. */
        assert(astra_civil_expand_zone("100%%Z", &local, expanded,
                                       sizeof(expanded)) != 0u);
        assert(strcmp(expanded, "100%%Z") == 0);
        /* A buffer that cannot hold the result writes nothing. */
        assert(astra_civil_expand_zone("%Z", &local, expanded, 3u) == 0u);
    }

    puts("CIVIL TIME PASS");
    return 0;
}

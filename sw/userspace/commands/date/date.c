/*
 * `date` -- what day the machine thinks it is, in whatever shape you asked for.
 *
 * The clock underneath is the host's, disciplined by NTP one layer down, so
 * this is not a machine that guesses the time from its own oscillator and
 * drifts. What it prints is what a file written a second later is stamped
 * with, which is the property that matters: a listing and a clock that
 * disagree are worse than neither.
 *
 *   date                    Thu Aug 20 00:24:03 EDT 2026
 *   date -u                 the same instant in UTC
 *   date -I                 2026-08-20
 *   date -Is                2026-08-20T00:24:03-04:00
 *   date -R                 Thu, 20 Aug 2026 00:24:03 -0400
 *   date +%H:%M             00:24
 *   date +%s                1787185443
 *
 * The format is strftime's, because that is what the person typing it already
 * knows: `+FORMAT` is passed through with a `struct tm` filled from the
 * machine's clock. Local time by default and UTC on request, which is the way
 * round every other machine has it.
 *
 * Local means the zone the machine reports -- an offset that already has
 * summer time decided by the layer that keeps the clock. There is no timezone
 * database here and deliberately so: a second copy of those rules is a second
 * answer that can disagree with the first.
 *
 * Setting the clock is not here. The machine reads the time from the layer
 * below, which has NTP; a `date -s` would be Astra disagreeing with the thing
 * keeping it right.
 */

#include <astra/civil.h>
#include <astra/posix.h>
#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/status.h>
#include <astra/syscall.h>

#include <stdio.h>
#include <string.h>
#include <time.h>

ASTRA_PROGRAM("date", 1, 0, 0, "Barry Walker",
              "Copyright 2026 Barry Walker");

#define NANOSECONDS_PER_SECOND 1000000000u
#define OUTPUT_MAX 256u

/* The default, and it is GNU date's: `Thu Aug 20 00:24:03 EDT 2026`. */
#define DEFAULT_FORMAT "%a %b %e %H:%M:%S %Z %Y"
#define RFC_2822_FORMAT "%a, %d %b %Y %H:%M:%S %z"

typedef enum DateForm {
    DATE_FORM_DEFAULT = 0,
    DATE_FORM_ISO_DATE,
    DATE_FORM_ISO_SECONDS,
    DATE_FORM_RFC_2822,
    DATE_FORM_EPOCH,
    DATE_FORM_CUSTOM
} DateForm;

/*
 * picolibc renders %Z and %z from the fields of `struct tm` on this build, so
 * the zone travels in the tm rather than in a global nobody set.
 */
static void
fill_tm(const AstraCivilTime *civil, struct tm *out, const char *zone_name)
{
    memset(out, 0, sizeof(*out));
    out->tm_year = (int)civil->year - 1900;
    out->tm_mon = (int)civil->month - 1;
    out->tm_mday = (int)civil->day;
    out->tm_hour = (int)civil->hour;
    out->tm_min = (int)civil->minute;
    out->tm_sec = (int)civil->second;
    out->tm_wday = (int)civil->weekday;
    out->tm_yday = (int)civil->year_day - 1;
    out->tm_isdst = -1;
#ifdef __TM_GMTOFF
    out->__TM_GMTOFF = civil->utc_offset;
#endif
#ifdef __TM_ZONE
    out->__TM_ZONE = zone_name;
#else
    (void)zone_name;
#endif
}

/*
 * `+00:00` rather than `Z`, and `-04:00` rather than nothing: an ISO instant
 * with no offset is a claim about a place it does not name.
 */
static void
offset_string(int32_t offset, char *out, size_t capacity)
{
    long total = (long)offset;
    char sign = total < 0 ? '-' : '+';
    unsigned hours;
    unsigned minutes;

    if (total < 0)
        total = -total;
    /* Bounded before printing: no zone is further than a day from UTC. */
    hours = (unsigned)((total / 3600L) % 100L);
    minutes = (unsigned)((total % 3600L) / 60L);
    (void)snprintf(out, capacity, "%c%02u:%02u", sign, hours, minutes);
}

int
astra_main(const AstraStartupInfo *startup)
{
    AstraCivilTime civil;
    AstraTimeZone zone = ASTRA_TIME_ZONE_UTC;
    AstraTimeZone utc = ASTRA_TIME_ZONE_UTC;
    struct tm rendered;
    char output[OUTPUT_MAX];
    char expanded[OUTPUT_MAX];
    char offset[12];
    uint64_t nanoseconds = 0u;
    const uint32_t *argv = NULL;
    const char *format = NULL;
    DateForm form = DATE_FORM_DEFAULT;
    int universal = 0;
    uint32_t status;

    astra_posix_start(startup);
    if (startup == NULL)
        return ASTRA_STATUS_INVALID;
    if (startup->argc != 0u && startup->argv_address != 0u)
        argv = (const uint32_t *)(uintptr_t)startup->argv_address;
    for (uint32_t index = 1u; argv != NULL && index < startup->argc;
         ++index) {
        const char *word = (const char *)(uintptr_t)argv[index];

        if (word == NULL)
            continue;
        if (word[0] == '+') {
            format = &word[1];
            form = DATE_FORM_CUSTOM;
            continue;
        }
        if (word[0] != '-' || word[1] == '\0') {
            /*
             * GNU date takes no operand either, and the reason a format with
             * a space lands here is that the shell has no quoting yet: every
             * word after the first is its own argument. A format without
             * spaces is the way to ask for one today.
             */
            (void)fprintf(stderr,
                          "date: %s: not an option or a +FORMAT. The shell "
                          "cannot quote yet, so a format must be one word.\n",
                          word);
            return ASTRA_STATUS_INVALID;
        }
        for (uint32_t at = 1u; word[at] != '\0'; ++at) {
            switch (word[at]) {
            case 'u':
                universal = 1;
                break;
            case 'i':
            case 'I':
                /* `-I` is a date; `-Is` adds the time, as GNU date has it. */
                form = word[at + 1u] == 's' ? DATE_FORM_ISO_SECONDS :
                                              DATE_FORM_ISO_DATE;
                if (word[at + 1u] == 's')
                    ++at;
                break;
            case 'R':
                form = DATE_FORM_RFC_2822;
                break;
            case 'e':
                form = DATE_FORM_EPOCH;
                break;
            case 's':
                (void)fprintf(stderr,
                              "date: the clock is read-only: it comes from the "
                              "host, which keeps it with NTP\n");
                return ASTRA_STATUS_UNSUPPORTED;
            default:
                (void)fprintf(stderr, "date: unknown option -%c\n", word[at]);
                return ASTRA_STATUS_INVALID;
            }
        }
    }

    status = astra_clock_realtime_zone(&nanoseconds, &zone);
    if (status != ASTRA_SYSCALL_OK) {
        (void)fprintf(stderr, "date: this machine has no clock\n");
        return ASTRA_STATUS_UNSUPPORTED;
    }
    if (form == DATE_FORM_EPOCH) {
        printf("%lu\n",
               (unsigned long)(nanoseconds / NANOSECONDS_PER_SECOND));
        (void)fflush(stdout);
        return 0;
    }
    if (universal)
        zone = utc;
    if (!astra_civil_from_unix_ns_zone(nanoseconds, &zone, &civil)) {
        (void)fprintf(stderr, "date: the clock reads before the epoch\n");
        return ASTRA_STATUS_INVALID;
    }
    fill_tm(&civil, &rendered, zone.name);
    offset_string(civil.utc_offset, offset, sizeof(offset));

    switch (form) {
    case DATE_FORM_ISO_DATE:
        printf("%04ld-%02u-%02u\n", (long)civil.year, (unsigned)civil.month,
               (unsigned)civil.day);
        break;
    case DATE_FORM_ISO_SECONDS:
        printf("%04ld-%02u-%02uT%02u:%02u:%02u%s\n", (long)civil.year,
               (unsigned)civil.month, (unsigned)civil.day,
               (unsigned)civil.hour, (unsigned)civil.minute,
               (unsigned)civil.second,
               universal || civil.utc_offset == 0 ? "+00:00" : offset);
        break;
    case DATE_FORM_RFC_2822:
    case DATE_FORM_DEFAULT:
    case DATE_FORM_CUSTOM: {
        const char *chosen = format;

        if (form == DATE_FORM_RFC_2822)
            chosen = RFC_2822_FORMAT;
        else if (form == DATE_FORM_DEFAULT)
            chosen = DEFAULT_FORMAT;
        if (chosen[0] == '\0') {
            printf("\n");
            break;
        }
        if (astra_civil_expand_zone(chosen, &civil, expanded,
                                    sizeof(expanded)) == 0u) {
            (void)fprintf(stderr, "date: the format does not fit in %u bytes\n",
                          (unsigned)sizeof(expanded));
            return ASTRA_STATUS_INVALID;
        }
        if (strftime(output, sizeof(output), expanded, &rendered) == 0u) {
            /*
             * strftime answers zero for "did not fit" and for "produced
             * nothing", and a format that legitimately produces nothing is a
             * format nobody typed by accident.
             */
            (void)fprintf(stderr, "date: the result does not fit in %u bytes\n",
                          (unsigned)sizeof(output));
            return ASTRA_STATUS_INVALID;
        }
        printf("%s\n", output);
        break;
    }
    case DATE_FORM_EPOCH:
        break;
    }
    (void)fflush(stdout);
    return 0;
}

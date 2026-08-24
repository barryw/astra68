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
#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/status.h>
#include <astra/stream.h>
#include <astra/syscall.h>

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

static uint32_t stdout_handle;
static uint32_t error_handle;

/* No TZ environment exists; the instant already carries its resolved zone. */
void
tzset(void)
{
}

static void
say_error(const char *text)
{
    (void)astra_print(error_handle, text);
}

static uint32_t
append_number(char *out, uint32_t at, uint64_t value, uint32_t width)
{
    char digits[20];
    uint32_t count = 0u;

    do {
        uint64_t quotient = value / 10u;

        digits[count++] = (char)('0' + value - quotient * 10u);
        value = quotient;
    } while (value != 0u);
    while (width > count) {
        out[at++] = '0';
        --width;
    }
    while (count != 0u)
        out[at++] = digits[--count];
    return at;
}

static int
emit_line(char *out, uint32_t length)
{
    out[length++] = '\n';
    return astra_stream_write_all(stdout_handle, out, length) ==
           ASTRA_SYSCALL_OK ? 0 : (int)ASTRA_STATUS_IO;
}

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
offset_string(int32_t offset, char *out)
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
    out[0] = sign;
    out[1] = (char)('0' + hours / 10u);
    out[2] = (char)('0' + hours % 10u);
    out[3] = ':';
    out[4] = (char)('0' + minutes / 10u);
    out[5] = (char)('0' + minutes % 10u);
    out[6] = '\0';
}

int
astra_main(const AstraStartupInfo *startup)
{
    const AstraStartupCapability *capabilities;
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

    if (startup == NULL || startup->capabilities_address == 0u)
        return ASTRA_STATUS_INVALID;
    capabilities = (const AstraStartupCapability *)(uintptr_t)
        startup->capabilities_address;
    for (uint32_t index = 0u; index < startup->capability_count; ++index) {
        if (astra_capability_name_equal(capabilities[index].name, "STDOUT"))
            stdout_handle = capabilities[index].handle;
        else if (astra_capability_name_equal(capabilities[index].name,
                                             "STDERR"))
            error_handle = capabilities[index].handle;
    }
    if (stdout_handle == 0u)
        return ASTRA_STATUS_ACCESS;
    if (error_handle == 0u)
        error_handle = stdout_handle;
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
             * GNU date takes no operand either. A format with a space is
             * `date +"%H %M"`, which the shell quotes into one word -- so a
             * second word arriving here is a typo and not a limitation.
             */
            say_error("date: ");
            say_error(word);
            say_error(": not an option or a +FORMAT. A format with a space "
                      "in it needs quoting.\n");
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
                say_error("date: the clock is read-only: it comes from the "
                          "host, which keeps it with NTP\n");
                return ASTRA_STATUS_UNSUPPORTED;
            default:
                say_error("date: unknown option -");
                (void)astra_stream_write_all(error_handle, &word[at], 1u);
                say_error("\n");
                return ASTRA_STATUS_INVALID;
            }
        }
    }

    status = astra_clock_realtime_zone(&nanoseconds, &zone);
    if (status != ASTRA_SYSCALL_OK) {
        say_error("date: this machine has no clock\n");
        return ASTRA_STATUS_UNSUPPORTED;
    }
    if (form == DATE_FORM_EPOCH) {
        uint32_t length = append_number(output, 0u,
            nanoseconds / NANOSECONDS_PER_SECOND, 0u);

        return emit_line(output, length);
    }
    if (universal)
        zone = utc;
    if (!astra_civil_from_unix_ns_zone(nanoseconds, &zone, &civil)) {
        say_error("date: the clock reads before the epoch\n");
        return ASTRA_STATUS_INVALID;
    }
    fill_tm(&civil, &rendered, zone.name);
    offset_string(civil.utc_offset, offset);

    switch (form) {
    case DATE_FORM_ISO_DATE:
    case DATE_FORM_ISO_SECONDS: {
        uint32_t length = append_number(output, 0u, (uint32_t)civil.year, 4u);

        output[length++] = '-';
        length = append_number(output, length, civil.month, 2u);
        output[length++] = '-';
        length = append_number(output, length, civil.day, 2u);
        if (form == DATE_FORM_ISO_SECONDS) {
            const char *zone_text = universal || civil.utc_offset == 0 ?
                "+00:00" : offset;

            output[length++] = 'T';
            length = append_number(output, length, civil.hour, 2u);
            output[length++] = ':';
            length = append_number(output, length, civil.minute, 2u);
            output[length++] = ':';
            length = append_number(output, length, civil.second, 2u);
            for (uint32_t index = 0u; zone_text[index] != '\0'; ++index)
                output[length++] = zone_text[index];
        }
        return emit_line(output, length);
    }
    case DATE_FORM_RFC_2822:
    case DATE_FORM_DEFAULT:
    case DATE_FORM_CUSTOM: {
        const char *chosen = format;
        size_t length;

        if (form == DATE_FORM_RFC_2822)
            chosen = RFC_2822_FORMAT;
        else if (form == DATE_FORM_DEFAULT)
            chosen = DEFAULT_FORMAT;
        if (chosen[0] == '\0') {
            return emit_line(output, 0u);
        }
        if (astra_civil_expand_zone(chosen, &civil, expanded,
                                    sizeof(expanded)) == 0u) {
            say_error("date: the format does not fit in ");
            (void)astra_print_u32(error_handle, sizeof(expanded));
            say_error(" bytes\n");
            return ASTRA_STATUS_INVALID;
        }
        length = strftime(output, sizeof(output), expanded, &rendered);
        if (length == 0u) {
            /*
             * strftime answers zero for "did not fit" and for "produced
             * nothing", and a format that legitimately produces nothing is a
             * format nobody typed by accident.
             */
            say_error("date: the result does not fit in ");
            (void)astra_print_u32(error_handle, sizeof(output));
            say_error(" bytes\n");
            return ASTRA_STATUS_INVALID;
        }
        return emit_line(output, (uint32_t)length);
    }
    case DATE_FORM_EPOCH:
        break;
    }
    return 0;
}

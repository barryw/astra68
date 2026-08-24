/*
 * Nanoseconds since the epoch to a date somebody can read.
 *
 * The algorithm is the days-from-civil one in reverse: shift the epoch to
 * March 1st of year 0, and the month lengths become a repeating pattern with
 * no leap day in the middle of it, so a division does what a table and a loop
 * used to. It is exact for every day this machine can hold and it has no
 * `if (february)` in it, which is where a hand-rolled calendar goes wrong.
 *
 * It lives in sw/common because the kernel prints the boot line, `ls` prints a
 * listing and `date` prints the date -- three programs on two sides of the
 * syscall boundary, and one calendar between them.
 */

#include <astra/civil.h>

#include <astra/divide.h>

#include <stddef.h>

#define SECONDS_PER_DAY 86400u
#define NANOSECONDS_PER_SECOND 1000000000u

static uint32_t two_digits(char *out, uint32_t value);

bool astra_civil_from_unix_seconds(uint64_t seconds, AstraCivilTime *civil)
{
    uint32_t rest = 0u;
    uint32_t epoch_days;
    uint32_t days;
    uint32_t era;
    uint32_t day_of_era;
    uint32_t year_of_era;
    uint32_t day_of_year;
    uint32_t shifted_month;
    uint32_t shifted_year;
    uint32_t month;
    uint32_t day;
    int32_t year;
    int leap;
    static const uint16_t cumulative[12] = {
        0u, 31u, 59u, 90u, 120u, 151u, 181u, 212u, 243u, 273u, 304u, 334u
    };

    if (civil == NULL)
        return false;
    /*
     * The one 64-bit division, and it is a helper rather than an operator
     * because this file is compiled into the kernel too -- see astra/divide.h.
     * Everything after it is 32-bit: a day number has room for two hundred
     * thousand years.
     */
    epoch_days = (uint32_t)astra_divide_u64(seconds, SECONDS_PER_DAY, &rest);

    /* Days since 0000-03-01, which is what makes the arithmetic uniform. */
    days = epoch_days + 719468u;
    era = days / 146097u;
    day_of_era = days % 146097u;
    year_of_era = (day_of_era - day_of_era / 1460u + day_of_era / 36524u -
                   day_of_era / 146096u) / 365u;
    shifted_year = era * 400u + year_of_era;
    day_of_year = day_of_era - (365u * year_of_era + year_of_era / 4u -
                                year_of_era / 100u);
    shifted_month = (5u * day_of_year + 2u) / 153u;
    day = day_of_year - (153u * shifted_month + 2u) / 5u + 1u;
    month = shifted_month < 10u ? shifted_month + 3u : shifted_month - 9u;
    year = (int32_t)(shifted_year + (month <= 2u ? 1u : 0u));
    if (year < 1970)
        return false;

    civil->year = year;
    civil->month = (uint8_t)month;
    civil->day = (uint8_t)day;
    civil->hour = (uint8_t)(rest / 3600u);
    civil->minute = (uint8_t)((rest % 3600u) / 60u);
    civil->second = (uint8_t)(rest % 60u);
    /* 1970-01-01 was a Thursday, and the week has not skipped since. */
    civil->weekday = (uint8_t)((epoch_days + 4u) % 7u);
    leap = ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0) ? 1 : 0;
    civil->year_day = (uint16_t)(cumulative[month - 1u] +
                                 ((leap != 0 && month > 2u) ? 1u : 0u) + day);
    civil->nanosecond = 0u;
    civil->utc_offset = 0;
    civil->zone[0] = 'U';
    civil->zone[1] = 'T';
    civil->zone[2] = 'C';
    civil->zone[3] = '\0';
    civil->zone[4] = '\0';
    return true;
}

void astra_civil_zone_unpack(int32_t utc_offset, uint32_t packed_name,
                             AstraTimeZone *zone)
{
    uint32_t index;

    if (zone == NULL)
        return;
    zone->utc_offset = utc_offset;
    for (index = 0u; index < 4u; ++index)
        zone->name[index] = (char)((packed_name >> (24u - 8u * index)) & 0xffu);
    zone->name[4] = '\0';
    /*
     * A machine that has a clock and no location is in UTC and says so. An
     * empty name beside a nonzero offset would print as a time nobody could
     * place.
     */
    if (zone->name[0] == '\0') {
        zone->name[0] = 'U';
        zone->name[1] = 'T';
        zone->name[2] = 'C';
        zone->name[3] = '\0';
        zone->utc_offset = 0;
    }
}

bool astra_civil_from_unix_seconds_zone(uint64_t seconds,
                                        const AstraTimeZone *zone,
                                        AstraCivilTime *civil)
{
    int32_t offset = zone != NULL ? zone->utc_offset : 0;
    uint64_t shifted = seconds;
    uint32_t index;

    if (civil == NULL)
        return false;
    /*
     * The shift is applied to the instant rather than to the rendered fields,
     * so an offset that crosses midnight, a month or a year needs no special
     * case: the calendar below has already been proved against 200,000 of
     * them. West of Greenwich before 1970-01-01 is the one instant this
     * refuses, and it refuses rather than wrapping.
     */
    if (offset >= 0) {
        if ((uint64_t)offset > UINT64_MAX - shifted)
            return false;
        shifted += (uint64_t)offset;
    } else {
        uint64_t back = (uint64_t)(-(int64_t)offset);

        if (back > seconds)
            return false;
        shifted -= back;
    }
    if (!astra_civil_from_unix_seconds(shifted, civil))
        return false;
    civil->utc_offset = offset;
    for (index = 0u; index < 5u; ++index)
        civil->zone[index] = zone != NULL ? zone->name[index] : '\0';
    if (zone == NULL) {
        civil->zone[0] = 'U';
        civil->zone[1] = 'T';
        civil->zone[2] = 'C';
        civil->zone[3] = '\0';
    }
    civil->zone[4] = '\0';
    return true;
}

bool astra_civil_from_unix_ns_zone(uint64_t nanoseconds,
                                   const AstraTimeZone *zone,
                                   AstraCivilTime *civil)
{
    uint32_t rest = 0u;
    uint64_t seconds = astra_divide_u64(nanoseconds,
                                        NANOSECONDS_PER_SECOND, &rest);

    if (!astra_civil_from_unix_seconds_zone(seconds, zone, civil))
        return false;
    civil->nanosecond = rest;
    return true;
}

bool astra_civil_from_unix_ns(uint64_t nanoseconds, AstraCivilTime *civil)
{
    uint32_t rest = 0u;
    uint64_t seconds = astra_divide_u64(nanoseconds, NANOSECONDS_PER_SECOND, &rest);

    if (!astra_civil_from_unix_seconds(seconds, civil))
        return false;
    civil->nanosecond = rest;
    return true;
}

const char *astra_civil_month_name(uint8_t month)
{
    static const char *const names[12] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };

    return month >= 1u && month <= 12u ? names[month - 1u] : NULL;
}

uint32_t astra_civil_expand_zone(const char *format,
                                 const AstraCivilTime *civil, char *out,
                                 uint32_t capacity)
{
    uint32_t written = 0u;
    uint32_t index = 0u;

    if (format == NULL || civil == NULL || out == NULL || capacity == 0u)
        return 0u;
    while (format[index] != '\0') {
        char literal[8];
        const char *insert = NULL;
        uint32_t length = 0u;

        if (format[index] == '%' && format[index + 1u] == 'Z') {
            insert = civil->zone;
            while (insert[length] != '\0')
                ++length;
            index += 2u;
        } else if (format[index] == '%' && format[index + 1u] == 'z') {
            int32_t total = civil->utc_offset;
            uint32_t hours;
            uint32_t minutes;

            literal[length++] = total < 0 ? '-' : '+';
            if (total < 0)
                total = -total;
            hours = (uint32_t)total / 3600u;
            minutes = ((uint32_t)total % 3600u) / 60u;
            length += two_digits(&literal[length], hours);
            length += two_digits(&literal[length], minutes);
            literal[length] = '\0';
            insert = literal;
            index += 2u;
        } else {
            /*
             * `%%` moves as a pair, so a literal percent before a Z is not
             * mistaken for a zone.
             */
            literal[0] = format[index++];
            length = 1u;
            if (literal[0] == '%' && format[index] != '\0')
                literal[length++] = format[index++];
            literal[length] = '\0';
            insert = literal;
        }
        if (written + length + 1u > capacity)
            return 0u;
        for (uint32_t at = 0u; at < length; ++at)
            out[written++] = insert[at];
    }
    out[written] = '\0';
    return written;
}

static uint32_t two_digits(char *out, uint32_t value)
{
    out[0] = (char)('0' + (value / 10u) % 10u);
    out[1] = (char)('0' + value % 10u);
    return 2u;
}

uint32_t astra_civil_iso8601(uint64_t nanoseconds, char *out, uint32_t capacity)
{
    AstraCivilTime civil;
    uint32_t length = 0u;
    uint32_t year;

    if (out == NULL || capacity < 21u)
        return 0u;
    if (!astra_civil_from_unix_ns(nanoseconds, &civil))
        return 0u;
    year = (uint32_t)civil.year;
    if (year > 9999u)
        return 0u;
    out[length++] = (char)('0' + (year / 1000u) % 10u);
    out[length++] = (char)('0' + (year / 100u) % 10u);
    length += two_digits(&out[length], year);
    out[length++] = '-';
    length += two_digits(&out[length], civil.month);
    out[length++] = '-';
    length += two_digits(&out[length], civil.day);
    out[length++] = 'T';
    length += two_digits(&out[length], civil.hour);
    out[length++] = ':';
    length += two_digits(&out[length], civil.minute);
    out[length++] = ':';
    length += two_digits(&out[length], civil.second);
    out[length++] = 'Z';
    out[length] = '\0';
    return length;
}

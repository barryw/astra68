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

#include <stddef.h>

#define SECONDS_PER_DAY 86400u
#define NANOSECONDS_PER_SECOND 1000000000u

/*
 * 64 by 32, without libgcc.
 *
 * The kernel links no libgcc and no C library, so `value / 86400` on a 64-bit
 * value is an undefined reference to `__udivdi3` rather than an instruction --
 * and a variable-count 64-bit shift is `__lshrdi3` for the same reason, which
 * rules out the obvious long division too. This is the same long division done
 * on two 32-bit halves with constant shifts only.
 *
 * The carry is the whole subtlety: the running remainder is always smaller
 * than the divisor, so doubling it can leave 33 bits. The bit that falls off
 * the top says the true value is at least 2^32, which is larger than any
 * divisor, so the subtraction happens -- and in wrapping 32-bit arithmetic it
 * lands on the right answer, because the 2^32 that was dropped and the 2^32
 * the subtraction borrows are the same one.
 */
static uint64_t divide_u64(uint64_t value, uint32_t divisor,
                           uint32_t *remainder)
{
    uint32_t high = (uint32_t)(value >> 32);
    uint32_t low = (uint32_t)value;
    uint32_t quotient_high = 0u;
    uint32_t quotient_low = 0u;
    uint32_t rest = 0u;

    for (uint32_t index = 0u; index < 64u; ++index) {
        uint32_t carry = rest >> 31;
        uint32_t bit = high >> 31;

        rest = (rest << 1) | bit;
        high = (high << 1) | (low >> 31);
        low <<= 1;
        quotient_high = (quotient_high << 1) | (quotient_low >> 31);
        quotient_low <<= 1;
        if (carry != 0u || rest >= divisor) {
            rest -= divisor;
            quotient_low |= 1u;
        }
    }
    if (remainder != NULL)
        *remainder = rest;
    return ((uint64_t)quotient_high << 32) | quotient_low;
}

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
     * The one 64-bit division. Everything after it is 32-bit: a day number
     * has room for two hundred thousand years.
     */
    epoch_days = (uint32_t)divide_u64(seconds, SECONDS_PER_DAY, &rest);

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
    return true;
}

bool astra_civil_from_unix_ns(uint64_t nanoseconds, AstraCivilTime *civil)
{
    uint32_t rest = 0u;
    uint64_t seconds = divide_u64(nanoseconds, NANOSECONDS_PER_SECOND, &rest);

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

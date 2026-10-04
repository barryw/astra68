#ifndef ASTRA_CIVIL_H
#define ASTRA_CIVIL_H

/**
 * @file civil.h
 * @brief Civil (calendar) time and time-zone types for rendering the
 *        machine's clock as a date a person can read.
 *
 * The machine keeps time as nanoseconds since the Unix epoch and nothing
 * else. A year, a month and a day are a rendering of that number, wanted in
 * three places -- the boot line, `ls -l`, and `date` -- and this header is
 * the one that does it, because three copies of a calendar are three
 * chances to disagree about a leap year.
 */

#include <stdbool.h>
#include <stdint.h>

/**
 * An instant, as a person reads it.
 *
 * UTC, and only UTC, unless a caller supplies an ::AstraTimeZone: there is
 * no timezone database on this machine and no configured location, so the
 * alternative to saying UTC is quietly claiming a local time that nothing
 * here knows.
 */
typedef struct AstraCivilTime {
    /** Four-digit proleptic Gregorian year; always 1970 or later. */
    int32_t year;
    /** Month of year, 1 (January) through 12 (December). */
    uint8_t month;
    /** Day of month, 1 through the month's last day. */
    uint8_t day;
    /** Hour of day, 0 through 23. */
    uint8_t hour;
    /** Minute of hour, 0 through 59. */
    uint8_t minute;
    /**
     * Second of minute, 0 through 60. A value of 60 would represent a leap
     * second; this machine's clock conversions neither produce nor refuse
     * one.
     */
    uint8_t second;
    /** Day of week, 0 (Sunday) through 6 (Saturday). */
    uint8_t weekday;
    /** Day of year, 1 through 366. */
    uint16_t year_day;
    /** Nanosecond of second, 0 through 999,999,999. */
    uint32_t nanosecond;
    /**
     * Offset in seconds east of UTC that the fields above are rendered in;
     * negative west of Greenwich.
     */
    int32_t utc_offset;
    /**
     * Zone abbreviation rendered alongside the offset, such as "UTC", "EDT"
     * or "CEST"; always NUL-terminated and never empty.
     */
    char zone[5];
} AstraCivilTime;

/**
 * A zone, as the machine reports it: an offset in force now and the name to
 * print beside it.
 *
 * There are no rules here and deliberately no timezone database -- the
 * layer that keeps the clock already applied them, including whether summer
 * time is in effect today, and a second copy of those rules is a second
 * answer that can disagree with the first.
 */
typedef struct AstraTimeZone {
    /** Offset in seconds east of UTC currently in force. */
    int32_t utc_offset;
    /** Zone abbreviation to print beside the offset, such as "UTC"; NUL-terminated. */
    char name[5];
} AstraTimeZone;

/** Initializer for an ::AstraTimeZone representing UTC: zero offset, name "UTC". */
#define ASTRA_TIME_ZONE_UTC { 0, "UTC" }

/**
 * Unpack a UTC offset and a packed zone name into an ::AstraTimeZone.
 *
 * @param utc_offset Offset in seconds east of UTC.
 * @param packed_name Up to four ASCII zone-name characters packed one per
 *     byte, most significant byte first (a 3-letter zone leaves the low
 *     byte zero).
 * @param zone Output zone; left untouched if NULL. If the first unpacked
 *     character is NUL, the name is treated as empty and this falls back to
 *     UTC with a zero offset, since a zone name is never empty.
 */
void astra_civil_zone_unpack(int32_t utc_offset, uint32_t packed_name,
                             AstraTimeZone *zone);

/**
 * Render a Unix-epoch nanosecond count as civil time in the given zone.
 *
 * The offset is applied to the instant itself, not to the rendered fields,
 * so an offset that crosses midnight, a month or a year needs no special
 * case.
 *
 * @param nanoseconds Nanoseconds since the Unix epoch.
 * @param zone Zone to render in; NULL means UTC, which is what a machine
 *     that has not been told where it is must say.
 * @param civil Output civil time.
 * @return true with civil filled in; false if civil is NULL, or if applying
 *     the zone's offset would carry the instant outside the range a 64-bit
 *     second count can represent (in practice, before the Unix epoch).
 */
bool astra_civil_from_unix_ns_zone(uint64_t nanoseconds,
                                   const AstraTimeZone *zone,
                                   AstraCivilTime *civil);

/**
 * The same zoned conversion as ::astra_civil_from_unix_ns_zone, for a caller
 * that already has whole seconds rather than nanoseconds.
 *
 * @param seconds Seconds since the Unix epoch.
 * @param zone Zone to render in; NULL means UTC.
 * @param civil Output civil time; its nanosecond field is left as 0.
 * @return true with civil filled in; false if civil is NULL, or if applying
 *     the zone's offset would carry the instant outside the range a 64-bit
 *     second count can represent.
 */
bool astra_civil_from_unix_seconds_zone(uint64_t seconds,
                                        const AstraTimeZone *zone,
                                        AstraCivilTime *civil);

/**
 * Split nanoseconds since the Unix epoch into civil-time fields, in UTC.
 *
 * @param nanoseconds Nanoseconds since the Unix epoch.
 * @param civil Output civil time.
 * @return true with civil filled in; false if civil is NULL.
 */
bool astra_civil_from_unix_ns(uint64_t nanoseconds, AstraCivilTime *civil);

/**
 * The same conversion as ::astra_civil_from_unix_ns, for a caller that
 * already has whole seconds.
 *
 * @param seconds Seconds since the Unix epoch.
 * @param civil Output civil time; its nanosecond field is left as 0.
 * @return true with civil filled in; false if civil is NULL.
 */
bool astra_civil_from_unix_seconds(uint64_t seconds, AstraCivilTime *civil);

/**
 * Look up the three-letter English abbreviation for a month.
 *
 * @param month Month number, 1 (January) through 12 (December).
 * @return A pointer to a static string such as "Jan" through "Dec", or NULL
 *     if month is outside 1-12.
 */
const char *astra_civil_month_name(uint8_t month);

/**
 * Expand `%Z` and `%z` in a strftime-style format string, copying
 * everything else through unchanged.
 *
 * picolibc's `struct tm` carries no zone -- its `%Z` reads a global that
 * only `tzset()` and a `TZ` environment variable can fill, and this machine
 * has neither. The zone it does have travels in the instant, so the two
 * specifiers that need it are substituted here and everything else is left
 * for strftime to render.
 *
 * @param format NUL-terminated strftime-style format string.
 * @param civil Civil time supplying the zone name and offset to substitute.
 * @param out Output buffer.
 * @param capacity Size of out in bytes, including room for the terminating
 *     NUL.
 * @return The number of characters written, not including the terminating
 *     NUL; 0 if format, civil, or out is NULL, capacity is 0, or the
 *     expansion does not fit.
 */
uint32_t astra_civil_expand_zone(const char *format,
                                 const AstraCivilTime *civil, char *out,
                                 uint32_t capacity);

/**
 * Render a Unix-epoch nanosecond count as a UTC `YYYY-MM-DDTHH:MM:SSZ`
 * string.
 *
 * @param nanoseconds Nanoseconds since the Unix epoch.
 * @param out Output buffer; needs at least 21 bytes.
 * @param capacity Size of out in bytes.
 * @return The number of characters written, not including the terminating
 *     NUL; 0 if out is NULL, capacity is less than 21, the year exceeds
 *     9999, or the instant cannot be rendered.
 */
uint32_t astra_civil_iso8601(uint64_t nanoseconds, char *out, uint32_t capacity);

#endif

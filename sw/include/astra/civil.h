#ifndef ASTRA_CIVIL_H
#define ASTRA_CIVIL_H

#include <stdbool.h>
#include <stdint.h>

/*
 * An instant, as a person reads it.
 *
 * The machine keeps time as nanoseconds since the Unix epoch and nothing else.
 * A year, a month and a day are a rendering of that number, wanted in three
 * places -- the boot line, `ls -l`, and `date` -- and this is the one that
 * does it, because three copies of a calendar are three chances to disagree
 * about a leap year.
 *
 * UTC, and only UTC. There is no timezone database on this machine and no
 * configured location, so the alternative to saying UTC is quietly claiming a
 * local time that nothing here knows.
 */
typedef struct AstraCivilTime {
    int32_t year;        /* 1970 and up */
    uint8_t month;       /* 1-12 */
    uint8_t day;         /* 1-31 */
    uint8_t hour;        /* 0-23 */
    uint8_t minute;      /* 0-59 */
    uint8_t second;      /* 0-60, a leap second is not modelled but not refused */
    uint8_t weekday;     /* 0 = Sunday */
    uint16_t year_day;   /* 1-366 */
    uint32_t nanosecond;
    int32_t utc_offset;  /* seconds east of UTC these fields are rendered in */
    char zone[5];        /* "UTC", "EDT", "CEST"; never empty */
} AstraCivilTime;

/*
 * A zone, as the machine reports it: an offset in force now and the name to
 * print beside it. There are no rules here and there is deliberately no
 * timezone database -- the layer that keeps the clock already applied them,
 * including whether summer time is in effect today, and a second copy of
 * those rules is a second answer that can disagree with the first.
 */
typedef struct AstraTimeZone {
    int32_t utc_offset;
    char name[5];
} AstraTimeZone;

#define ASTRA_TIME_ZONE_UTC { 0, "UTC" }

/* Unpacks the four packed characters the machine reports. UTC when empty. */
void astra_civil_zone_unpack(int32_t utc_offset, uint32_t packed_name,
                             AstraTimeZone *zone);

/*
 * The same instant, rendered where the machine is standing. A NULL zone means
 * UTC, which is what a machine that has not been told where it is must say.
 */
bool astra_civil_from_unix_ns_zone(uint64_t nanoseconds,
                                   const AstraTimeZone *zone,
                                   AstraCivilTime *civil);

/*
 * Splits nanoseconds since the epoch into the fields above. False for an
 * instant before the epoch, which this machine has no way to hold and no
 * reason to render.
 */
bool astra_civil_from_unix_ns(uint64_t nanoseconds, AstraCivilTime *civil);

/* The same, from whole seconds. */
bool astra_civil_from_unix_seconds(uint64_t seconds, AstraCivilTime *civil);

/* "Jan".."Dec" for month 1-12, and NULL for anything else. */
const char *astra_civil_month_name(uint8_t month);

/*
 * Expands `%Z` and `%z` in a strftime format, and copies the rest untouched.
 *
 * picolibc's `struct tm` carries no zone -- its `%Z` reads a global that only
 * `tzset()` and a `TZ` environment variable can fill, and this machine has
 * neither. The zone it does have travels in the instant, so the two specifiers
 * that need it are substituted here and everything else is left for strftime
 * to render.
 *
 * Returns the characters written, or 0 if the result did not fit.
 */
uint32_t astra_civil_expand_zone(const char *format,
                                 const AstraCivilTime *civil, char *out,
                                 uint32_t capacity);

/*
 * `YYYY-MM-DDTHH:MM:SSZ` into `out`, which needs 21 bytes. Returns the number
 * of characters written, or 0 if the buffer is too small or the instant is
 * one this cannot render.
 */
uint32_t astra_civil_iso8601(uint64_t nanoseconds, char *out, uint32_t capacity);

#endif

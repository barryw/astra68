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
} AstraCivilTime;

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
 * `YYYY-MM-DDTHH:MM:SSZ` into `out`, which needs 21 bytes. Returns the number
 * of characters written, or 0 if the buffer is too small or the instant is
 * one this cannot render.
 */
uint32_t astra_civil_iso8601(uint64_t nanoseconds, char *out, uint32_t capacity);

#endif

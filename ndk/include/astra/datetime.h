#ifndef ASTRA_NDK_DATETIME_H
#define ASTRA_NDK_DATETIME_H

/**
 * @file datetime.h
 * @brief The date and time, for an application.
 *
 * An application asks the machine what time it is and gets one answer: the
 * instant, and the zone the machine is standing in. They arrive together
 * because they belong to one moment -- a program that asked separately could
 * straddle a summer-time change and render an hour that never happened.
 *
 * There is no timezone database here. The layer that keeps the clock -- a
 * Linux host, disciplined by NTP -- has already applied the rules, including
 * whether summer time is in effect today, and reports the offset in force with
 * the name to print beside it. A second copy of those rules on this side would
 * be a second answer that can disagree with the first.
 *
 * A machine that does not know the date says so: every call answers false and
 * nothing here invents 1970. An application that shows a clock should show
 * nothing rather than a wrong time.
 */

#include <astra/civil.h>
#include <astra/types.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** The instant, the zone, and the rendered fields, from one reading. */
typedef struct AstraDateTime {
    uint64_t unix_nanoseconds;  /**< since the epoch, UTC */
    AstraTimeZone zone;         /**< offset in force now, and its name */
    AstraCivilTime local;       /**< the instant rendered in that zone */
    AstraCivilTime utc;         /**< the same instant in UTC */
} AstraDateTime;

/**
 * Reads the machine's clock.
 *
 * @param out receives the instant, the zone and both renderings.
 * @return true when the machine knows the date; false when it has no clock,
 *         leaving @p out untouched.
 */
bool astra_datetime_now(AstraDateTime *out);

/**
 * Renders an instant with strftime's format, in the zone it carries.
 *
 * @param civil an instant from astra_datetime_now, local or utc.
 * @param format strftime format; NULL means "%Y-%m-%d %H:%M:%S %Z".
 * @param out buffer to write into.
 * @param capacity size of @p out in bytes.
 * @return characters written, or 0 if the result did not fit.
 */
size_t astra_datetime_format(const AstraCivilTime *civil, const char *format,
                             char *out, size_t capacity);

/** Seconds since the epoch, for a program that wants the number. */
bool astra_datetime_unix_seconds(uint64_t *seconds);

#endif

/*
 * `date` -- what day the machine thinks it is.
 *
 * The clock underneath is the host's, disciplined by NTP one layer down, so
 * this is not a machine that guesses the time from its own oscillator and
 * drifts. What it prints is what a file written a second later is stamped
 * with, which is the property that matters: a listing and a clock that
 * disagree are worse than neither.
 *
 * UTC, always, and it says so. There is no timezone database on this machine
 * and nothing has told it where it is, so a local time would be a claim
 * nothing here can support.
 *
 *   date        Thu 2026-08-20 00:06:24 UTC
 *   date -i     2026-08-20T00:06:24Z
 *   date -e     1787184384
 *
 * A machine whose clock is not set says so and exits nonzero, rather than
 * printing 1970 as though that were the answer.
 */

#include <astra/civil.h>
#include <astra/posix.h>
#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/status.h>
#include <astra/syscall.h>

#include <stdio.h>

ASTRA_PROGRAM("date", 1, 0, 0, "Barry Walker",
              "Copyright 2026 Barry Walker");

#define NANOSECONDS_PER_SECOND 1000000000u

static const char *const weekday_name[7] = {
    "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
};

int
astra_main(const AstraStartupInfo *startup)
{
    AstraCivilTime civil;
    char iso[24];
    uint64_t nanoseconds = 0u;
    const uint32_t *argv = NULL;
    int iso_form = 0;
    int epoch_form = 0;
    uint32_t status;

    astra_posix_start(startup);
    if (startup == NULL)
        return ASTRA_STATUS_INVALID;
    if (startup->argc != 0u && startup->argv_address != 0u)
        argv = (const uint32_t *)(uintptr_t)startup->argv_address;
    for (uint32_t index = 1u; argv != NULL && index < startup->argc;
         ++index) {
        const char *word = (const char *)(uintptr_t)argv[index];

        if (word == NULL || word[0] != '-' || word[1] == '\0') {
            (void)fprintf(stderr, "date: takes no operand\n");
            return ASTRA_STATUS_INVALID;
        }
        for (uint32_t at = 1u; word[at] != '\0'; ++at) {
            if (word[at] == 'i') {
                iso_form = 1;
            } else if (word[at] == 'e') {
                epoch_form = 1;
            } else {
                (void)fprintf(stderr, "date: unknown option -%c\n", word[at]);
                return ASTRA_STATUS_INVALID;
            }
        }
    }

    status = astra_clock_realtime(&nanoseconds);
    if (status != ASTRA_SYSCALL_OK) {
        (void)fprintf(stderr, "date: this machine has no clock\n");
        return ASTRA_STATUS_UNSUPPORTED;
    }
    if (epoch_form) {
        printf("%lu\n",
               (unsigned long)(nanoseconds / NANOSECONDS_PER_SECOND));
        (void)fflush(stdout);
        return 0;
    }
    if (iso_form) {
        if (astra_civil_iso8601(nanoseconds, iso, sizeof(iso)) == 0u) {
            (void)fprintf(stderr, "date: %lu is not a date this can render\n",
                          (unsigned long)(nanoseconds /
                                          NANOSECONDS_PER_SECOND));
            return ASTRA_STATUS_INVALID;
        }
        printf("%s\n", iso);
        (void)fflush(stdout);
        return 0;
    }
    if (!astra_civil_from_unix_ns(nanoseconds, &civil)) {
        (void)fprintf(stderr, "date: the clock reads before the epoch\n");
        return ASTRA_STATUS_INVALID;
    }
    printf("%s %04ld-%02u-%02u %02u:%02u:%02u UTC\n",
           weekday_name[civil.weekday % 7u], (long)civil.year,
           (unsigned)civil.month, (unsigned)civil.day, (unsigned)civil.hour,
           (unsigned)civil.minute, (unsigned)civil.second);
    (void)fflush(stdout);
    return 0;
}

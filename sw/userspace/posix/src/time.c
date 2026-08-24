/*
 * What POSIX means by "what time is it".
 *
 * picolibc's `time`, `ctime` and `strftime` all end up at `gettimeofday`, and
 * its own is a stub that answers ENOSYS. This is the real one: the machine's
 * wall clock, which is the host's clock, which is what NTP keeps right.
 *
 * `clock_gettime` is here too because the two clocks are not the same thing
 * and a program that measures an interval must not use the wall clock: it
 * moves when the machine's time is corrected, so a duration measured across a
 * correction is wrong by the correction. CLOCK_MONOTONIC is the one to
 * measure with, and it is the cycle counter this machine has always had.
 *
 * A machine with no clock answers EPERM-free ENOSYS rather than 1970: the
 * caller asked a question this machine cannot answer, and inventing an answer
 * is how a file ends up stamped with the epoch.
 */

/*
 * picolibc hides clockid_t and CLOCK_MONOTONIC behind the POSIX visibility
 * macros, and this file is the POSIX layer.
 */
#define _POSIX_C_SOURCE 200809L
#define _POSIX_MONOTONIC_CLOCK 200809L

#include <astra/posix.h>
#include <astra/posix_descriptor.h>

#include <astra/runtime.h>
#include <astra/syscall.h>

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/time.h>
#include <sys/times.h>
#include <time.h>

#define NANOSECONDS_PER_SECOND 1000000000u

/*
 * Keeps this library's clocks in the link ahead of picolibc's weak stubs, for
 * the same reason `sbrk` needs it in console.c: inside `--start-group` the
 * first definition that satisfies the reference wins, and picolibc's answers
 * ENOSYS.
 */
typedef int (*PosixTimeOfDayFn)(struct timeval *, void *);
static const PosixTimeOfDayFn posix_keep_time __attribute__((used)) =
    gettimeofday;

static int
split(uint64_t nanoseconds, long *seconds, uint32_t *rest)
{
    *seconds = (long)(nanoseconds / NANOSECONDS_PER_SECOND);
    *rest = (uint32_t)(nanoseconds % NANOSECONDS_PER_SECOND);
    return 0;
}

int
gettimeofday(struct timeval *value, void *timezone)
{
    uint64_t nanoseconds = 0u;
    uint32_t rest = 0u;
    long seconds = 0;

    (void)timezone;
    if (value == NULL) {
        errno = EFAULT;
        return -1;
    }
    if (astra_clock_realtime(&nanoseconds) != ASTRA_SYSCALL_OK) {
        errno = ENOSYS;
        return -1;
    }
    (void)split(nanoseconds, &seconds, &rest);
    value->tv_sec = (time_t)seconds;
    value->tv_usec = (suseconds_t)(rest / 1000u);
    return 0;
}

int
clock_gettime(clockid_t clock, struct timespec *value)
{
    uint64_t nanoseconds = 0u;
    uint32_t rest = 0u;
    long seconds = 0;

    if (value == NULL) {
        errno = EFAULT;
        return -1;
    }
    switch (clock) {
    case CLOCK_REALTIME:
        if (astra_clock_realtime(&nanoseconds) != ASTRA_SYSCALL_OK) {
            errno = ENOSYS;
            return -1;
        }
        break;
    case CLOCK_MONOTONIC:
        nanoseconds = astra_clock_monotonic();
        break;
    default:
        errno = EINVAL;
        return -1;
    }
    (void)split(nanoseconds, &seconds, &rest);
    value->tv_sec = (time_t)seconds;
    value->tv_nsec = (long)rest;
    return 0;
}

clock_t
times(struct tms *value)
{
    const AstraStartupInfo *startup = astra_posix_startup();
    AstraProcessInfo info = {0};
    uint64_t monotonic;

    if (value == NULL || startup == NULL || startup->process_handle == 0u) {
        errno = EFAULT;
        return (clock_t)-1;
    }
    info.size = sizeof(info);
    if (astra_process_info(startup->process_handle, &info) !=
        ASTRA_SYSCALL_OK) {
        errno = EIO;
        return (clock_t)-1;
    }
    value->tms_utime = (clock_t)(info.runtime_ns / 1000u);
    value->tms_stime = 0;
    value->tms_cutime = 0;
    value->tms_cstime = 0;
    monotonic = astra_clock_monotonic();
    return (clock_t)(monotonic / 1000u);
}

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

#include <astra/clock.h>
#include <astra/runtime.h>
#include <astra/syscall.h>

#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/time.h>
#include <sys/times.h>
#include <time.h>
#include <unistd.h>

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

static int
timeval_to_nanoseconds(const struct timeval *value, uint64_t *nanoseconds)
{
    uint64_t seconds;

    if (value->tv_sec < 0 || value->tv_usec < 0 ||
        value->tv_usec >= 1000000) {
        errno = EINVAL;
        return -1;
    }
    seconds = (uint64_t)value->tv_sec;
    if (seconds > (UINT64_MAX - (uint64_t)value->tv_usec * 1000u) /
                      NANOSECONDS_PER_SECOND) {
        errno = EINVAL;
        return -1;
    }
    *nanoseconds = seconds * NANOSECONDS_PER_SECOND +
                   (uint64_t)value->tv_usec * 1000u;
    return 0;
}

static void
nanoseconds_to_timeval(uint64_t nanoseconds, struct timeval *value)
{
    value->tv_sec = (time_t)(nanoseconds / NANOSECONDS_PER_SECOND);
    value->tv_usec =
        (suseconds_t)((nanoseconds % NANOSECONDS_PER_SECOND) / 1000u);
}

int
setitimer(int which, const struct itimerval *restrict value,
          struct itimerval *restrict previous)
{
    uint64_t delay;
    uint64_t interval;
    uint64_t old_delay;
    uint64_t old_interval;

    if (which != ITIMER_REAL || value == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (timeval_to_nanoseconds(&value->it_value, &delay) != 0 ||
        timeval_to_nanoseconds(&value->it_interval, &interval) != 0)
        return -1;
    if (astra_rt_interval_timer(delay, interval, &old_delay,
                                &old_interval) != ASTRA_SYSCALL_OK) {
        errno = EIO;
        return -1;
    }
    if (previous != NULL) {
        nanoseconds_to_timeval(old_delay, &previous->it_value);
        nanoseconds_to_timeval(old_interval, &previous->it_interval);
    }
    return 0;
}

int
getitimer(int which, struct itimerval *value)
{
    uint64_t delay;
    uint64_t interval;

    if (which != ITIMER_REAL || value == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (astra_rt_interval_timer_get(&delay, &interval) != ASTRA_SYSCALL_OK) {
        errno = EIO;
        return -1;
    }
    nanoseconds_to_timeval(delay, &value->it_value);
    nanoseconds_to_timeval(interval, &value->it_interval);
    return 0;
}

unsigned int
alarm(unsigned int seconds)
{
    struct itimerval timer = {0};
    struct itimerval previous = {0};
    unsigned int remaining;

    timer.it_value.tv_sec = (time_t)seconds;
    if (setitimer(ITIMER_REAL, &timer, &previous) != 0)
        return 0u;
    remaining = (unsigned int)previous.it_value.tv_sec;
    if (previous.it_value.tv_usec != 0 && remaining != UINT_MAX)
        ++remaining;
    return remaining;
}

int
nanosleep(const struct timespec *request, struct timespec *remaining)
{
    uint64_t duration;
    uint64_t started;
    uint64_t elapsed;
    uint32_t generation;

    if (request == NULL) {
        errno = EFAULT;
        return -1;
    }
    if (request->tv_sec < 0 || request->tv_nsec < 0 ||
        request->tv_nsec >= (long)NANOSECONDS_PER_SECOND ||
        (uint64_t)request->tv_sec >
            (UINT64_MAX - (uint64_t)request->tv_nsec) /
                NANOSECONDS_PER_SECOND) {
        errno = EINVAL;
        return -1;
    }
    duration = (uint64_t)request->tv_sec * NANOSECONDS_PER_SECOND +
               (uint64_t)request->tv_nsec;
    if (duration == 0u) {
        if (remaining != NULL)
            remaining->tv_sec = remaining->tv_nsec = 0;
        return 0;
    }
    started = astra_clock_monotonic();
    generation = astra_posix_signal_generation();
    for (;;) {
        uint32_t status = astra_rt_thread_sleep(
            duration, ASTRA_THREAD_SLEEP_RELATIVE, 0u, NULL);

        elapsed = astra_clock_monotonic() - started;
        if (status == ASTRA_SYSCALL_TIMED_OUT || elapsed >= duration) {
            if (remaining != NULL)
                remaining->tv_sec = remaining->tv_nsec = 0;
            return 0;
        }
        if (status == ASTRA_SYSCALL_CANCELLED &&
            astra_posix_signal_generation() == generation) {
            duration -= elapsed;
            started += elapsed;
            continue;
        }
        if (remaining != NULL) {
            uint64_t rest = duration - elapsed;

            remaining->tv_sec = (time_t)(rest / NANOSECONDS_PER_SECOND);
            remaining->tv_nsec =
                (long)(rest % NANOSECONDS_PER_SECOND);
        }
        errno = status == ASTRA_SYSCALL_CANCELLED ? EINTR : EIO;
        return -1;
    }
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

int
clock_settime(clockid_t clock, const struct timespec *value)
{
    const AstraStartupCapability *capability;
    uint64_t nanoseconds;

    if (clock != CLOCK_REALTIME || value == NULL || value->tv_sec < 0 ||
        value->tv_nsec < 0 || value->tv_nsec >= (long)NANOSECONDS_PER_SECOND) {
        errno = EINVAL;
        return -1;
    }
    if ((uint64_t)value->tv_sec >
        (UINT64_MAX - (uint64_t)value->tv_nsec) /
            NANOSECONDS_PER_SECOND) {
        errno = EINVAL;
        return -1;
    }
    capability = astra_startup_capability(astra_posix_startup(),
                                           ASTRA_CAPABILITY_CLOCK);
    if (capability == NULL) {
        errno = EPERM;
        return -1;
    }
    nanoseconds = (uint64_t)value->tv_sec * NANOSECONDS_PER_SECOND +
                  (uint64_t)value->tv_nsec;
    if (astra_clock_set(capability->handle, nanoseconds) != ASTRA_SYSCALL_OK) {
        errno = EIO;
        return -1;
    }
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

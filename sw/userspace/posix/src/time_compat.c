#define _DEFAULT_SOURCE

/* Legacy time interfaces expressed through Astra's canonical timer APIs. */

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/time.h>
#include <sys/timeb.h>
#include <unistd.h>

#define MICROSECONDS_PER_SECOND UINT64_C(1000000)

int
ftime(struct timeb *value)
{
    struct timeval now;

    if (value == NULL) {
        errno = EFAULT;
        return -1;
    }
    if (gettimeofday(&now, NULL) != 0)
        return -1;
    value->time = now.tv_sec;
    value->millitm = (unsigned short)(now.tv_usec / 1000);
    value->timezone = 0;
    value->dstflag = 0;
    return 0;
}

useconds_t
ualarm(useconds_t microseconds, useconds_t interval)
{
    struct itimerval timer = {0};
    struct itimerval previous = {0};
    uint64_t remaining;
    useconds_t maximum = (useconds_t)-1;

    timer.it_value.tv_sec =
        (time_t)((uint64_t)microseconds / MICROSECONDS_PER_SECOND);
    timer.it_value.tv_usec =
        (suseconds_t)((uint64_t)microseconds % MICROSECONDS_PER_SECOND);
    timer.it_interval.tv_sec =
        (time_t)((uint64_t)interval / MICROSECONDS_PER_SECOND);
    timer.it_interval.tv_usec =
        (suseconds_t)((uint64_t)interval % MICROSECONDS_PER_SECOND);
    if (setitimer(ITIMER_REAL, &timer, &previous) != 0)
        return maximum;
    if (previous.it_value.tv_sec < 0 || previous.it_value.tv_usec < 0)
        return 0u;
    remaining = (uint64_t)previous.it_value.tv_sec *
                    MICROSECONDS_PER_SECOND +
                (uint64_t)previous.it_value.tv_usec;
    return remaining > (uint64_t)maximum ? maximum : (useconds_t)remaining;
}

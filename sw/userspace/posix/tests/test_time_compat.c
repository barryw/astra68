#define _DEFAULT_SOURCE

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <sys/time.h>
#include <sys/timeb.h>
#include <unistd.h>

int astra_test_ftime(struct timeb *value);
useconds_t astra_test_ualarm(useconds_t microseconds, useconds_t interval);

struct timeval wall_clock;
struct itimerval supplied_timer;
struct itimerval previous_timer;
int time_error;
int timer_error;

int
__wrap_gettimeofday(struct timeval *value, void *timezone)
{
    (void)timezone;
    if (time_error != 0) {
        errno = time_error;
        return -1;
    }
    *value = wall_clock;
    return 0;
}

int
__wrap_setitimer(int which, const struct itimerval *restrict value,
                 struct itimerval *restrict previous)
{
    assert(which == ITIMER_REAL);
    supplied_timer = *value;
    if (timer_error != 0) {
        errno = timer_error;
        return -1;
    }
    *previous = previous_timer;
    return 0;
}

int
main(void)
{
    struct timeb value;

    wall_clock.tv_sec = 1234;
    wall_clock.tv_usec = 567890;
    assert(astra_test_ftime(&value) == 0);
    assert(value.time == 1234 && value.millitm == 567u);
    assert(value.timezone == 0 && value.dstflag == 0);

    time_error = EIO;
    assert(astra_test_ftime(&value) == -1 && errno == EIO);
    time_error = 0;

    previous_timer.it_value.tv_sec = 2;
    previous_timer.it_value.tv_usec = 3456;
    assert(astra_test_ualarm(3500001u, 2000002u) == 2003456u);
    assert(supplied_timer.it_value.tv_sec == 3);
    assert(supplied_timer.it_value.tv_usec == 500001);
    assert(supplied_timer.it_interval.tv_sec == 2);
    assert(supplied_timer.it_interval.tv_usec == 2);

    timer_error = EINVAL;
    assert(astra_test_ualarm(1u, 0u) == (useconds_t)-1);
    assert(errno == EINVAL);
    return 0;
}

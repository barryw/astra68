#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/stream.h>

ASTRA_PROGRAM("timer", 1, 0, 0, "Your Name", "Copyright 2026 Your Name");

enum { TIMER_PERIOD_NS = 20000000u, TIMER_TICKS = 5u };

/* Sleep on a one-shot timer re-armed for absolute deadlines, so a late
   wake-up never pushes the following ticks later. */
static uint32_t run_ticks(uint32_t timer)
{
    uint64_t deadline = astra_clock_monotonic() + TIMER_PERIOD_NS;

    for (uint32_t tick = 0u; tick < TIMER_TICKS; ++tick) {
        uint32_t status = astra_rt_timer_set(timer, deadline, 0);

        if (status != ASTRA_SYSCALL_OK)
            return status;
        status = astra_wait_one_restart(timer, ASTRA_DEADLINE_FOREVER, 0);
        if (status != ASTRA_SYSCALL_OK)
            return status;
        if (astra_clock_monotonic() < deadline)
            return ASTRA_SYSCALL_INVALID_ARGUMENT;
        deadline += TIMER_PERIOD_NS;
    }
    return ASTRA_SYSCALL_OK;
}

/* A cancelled timer never fires: waiting past its deadline times out. */
static uint32_t check_cancel(uint32_t timer)
{
    uint64_t deadline = astra_clock_monotonic() + TIMER_PERIOD_NS;
    uint32_t status = astra_rt_timer_set(timer, deadline, 0);

    if (status == ASTRA_SYSCALL_OK)
        status = astra_rt_timer_cancel(timer, 0);
    if (status == ASTRA_SYSCALL_OK)
        status = astra_wait_one_restart(timer, deadline + TIMER_PERIOD_NS, 0);
    return status == ASTRA_SYSCALL_TIMED_OUT ? ASTRA_SYSCALL_OK :
                                                ASTRA_SYSCALL_INVALID_ARGUMENT;
}

int astra_main(const AstraStartupInfo *startup)
{
    const AstraStartupCapability *output =
        astra_startup_capability(startup, "STDOUT");
    uint32_t timer;
    uint32_t status;

    if (output == 0)
        return 1;
    status = astra_rt_timer_create(ASTRA_RIGHT_WAIT | ASTRA_RIGHT_ADMINISTER,
                                   &timer);
    if (status != ASTRA_SYSCALL_OK)
        return 2;
    status = run_ticks(timer);
    if (status == ASTRA_SYSCALL_OK)
        status = check_cancel(timer);
    (void)astra_close(timer);
    if (status != ASTRA_SYSCALL_OK)
        return 3;
    (void)astra_print(output->handle, "timer: 5 ticks on deadline, "
                                      "cancelled timer stayed quiet\n");
    return 0;
}

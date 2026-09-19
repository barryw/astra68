#define _GNU_SOURCE 1

#include <astra/process.h>
#include <astra/runtime.h>
#include <astra/syscall.h>

#include <assert.h>
#include <errno.h>
#include <sched.h>
#include <stdint.h>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>

uint32_t priority = ASTRA_PROCESS_PRIORITY_NORMAL;
uint32_t yielded;

uint32_t
astra_query_abi(uint32_t *abi_version, uint32_t *process_handle,
                uint32_t *thread_handle)
{
    (void)abi_version;
    (void)thread_handle;
    *process_handle = 7u;
    return ASTRA_SYSCALL_OK;
}

uint32_t
astra_process_info(uint32_t handle, AstraProcessInfo *info)
{
    assert(handle == 7u && info->size == sizeof(*info));
    info->id = 42u;
    info->default_priority = (uint8_t)priority;
    return ASTRA_SYSCALL_OK;
}

uint32_t
astra_process_priority(uint32_t handle, uint32_t value,
                       uint32_t *previous_priority)
{
    assert(handle == 7u);
    if (previous_priority != NULL)
        *previous_priority = priority;
    priority = value;
    return ASTRA_SYSCALL_OK;
}

uint32_t
astra_yield(void)
{
    ++yielded;
    return ASTRA_SYSCALL_OK;
}

int
main(void)
{
    struct sched_param parameters;
    struct timespec interval;

    assert(getpriority(PRIO_PROCESS, 0) == 0);
    assert(setpriority(PRIO_PROCESS, 0, 3) == 0);
    assert(priority == ASTRA_PROCESS_PRIORITY_NORMAL - 3u);
    assert(nice(-2) == 1);
    assert(priority == ASTRA_PROCESS_PRIORITY_NORMAL - 1u);

    assert(sched_get_priority_min(SCHED_RR) ==
           (int)ASTRA_PROCESS_PRIORITY_MIN);
    assert(sched_get_priority_max(SCHED_RR) ==
           (int)ASTRA_PROCESS_PRIORITY_MAX);
    errno = 0;
    assert(sched_get_priority_min(SCHED_FIFO) == -1 && errno == EINVAL);
    assert(sched_getscheduler(0) == SCHED_RR);
    assert(sched_getparam(42, &parameters) == 0 &&
           parameters.sched_priority == (int)priority);
    parameters.sched_priority = 20;
    assert(sched_setparam(0, &parameters) == 0 && priority == 20u);
    assert(sched_setscheduler(42, SCHED_RR, &parameters) == 0);
    errno = 0;
    assert(sched_setscheduler(0, SCHED_FIFO, &parameters) == -1 &&
           errno == EINVAL);
    errno = 0;
    assert(sched_getparam(41, &parameters) == -1 && errno == ESRCH);
    assert(sched_rr_get_interval(0, &interval) == 0 &&
           interval.tv_sec == 0 &&
           interval.tv_nsec == ASTRA_PROCESS_QUANTUM_NS);
    assert(sched_yield() == 0 && yielded == 1u);
    assert(sched_getcpu() == 0);
    return 0;
}

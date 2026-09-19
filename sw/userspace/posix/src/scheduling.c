#define _XOPEN_SOURCE 700

#include <astra/process.h>
#include <astra/runtime.h>
#include <astra/syscall.h>

#include <errno.h>
#include <sched.h>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>

static int
current_process(uint32_t *handle, AstraProcessInfo *info)
{
    uint32_t status;

    status = astra_query_abi(NULL, handle, NULL);
    if (status == ASTRA_SYSCALL_OK) {
        info->size = sizeof(*info);
        status = astra_process_info(*handle, info);
    }
    if (status == ASTRA_SYSCALL_OK)
        return 0;
    errno = status == ASTRA_SYSCALL_ACCESS_DENIED ? EPERM : EIO;
    return -1;
}

int
getpriority(int which, id_t who)
{
    AstraProcessInfo info = {0};
    uint32_t handle = 0u;

    if (which != PRIO_PROCESS) {
        errno = EINVAL;
        return -1;
    }
    if (current_process(&handle, &info) != 0)
        return -1;
    if (who != 0 && who != (id_t)info.id) {
        errno = ESRCH;
        return -1;
    }
    return (int)ASTRA_PROCESS_PRIORITY_NORMAL -
           (int)info.default_priority;
}

int
setpriority(int which, id_t who, int value)
{
    AstraProcessInfo info = {0};
    uint32_t handle = 0u;
    uint32_t priority;
    uint32_t status;

    if (which != PRIO_PROCESS) {
        errno = EINVAL;
        return -1;
    }
    if (current_process(&handle, &info) != 0)
        return -1;
    if (who != 0 && who != (id_t)info.id) {
        errno = ESRCH;
        return -1;
    }
    if (value < ASTRA_PROCESS_NICE_MIN)
        value = ASTRA_PROCESS_NICE_MIN;
    if (value > ASTRA_PROCESS_NICE_MAX)
        value = ASTRA_PROCESS_NICE_MAX;
    priority = (uint32_t)((int)ASTRA_PROCESS_PRIORITY_NORMAL - value);
    status = astra_process_priority(handle, priority, NULL);
    if (status == ASTRA_SYSCALL_OK)
        return 0;
    errno = status == ASTRA_SYSCALL_ACCESS_DENIED ? EPERM :
            status == ASTRA_SYSCALL_INVALID_ARGUMENT ? EINVAL : EIO;
    return -1;
}

int
nice(int increment)
{
    int value;

    errno = 0;
    value = getpriority(PRIO_PROCESS, 0);
    if (value == -1 && errno != 0)
        return -1;
    if (increment > 0 && value > ASTRA_PROCESS_NICE_MAX - increment)
        value = ASTRA_PROCESS_NICE_MAX;
    else if (increment < 0 && value < ASTRA_PROCESS_NICE_MIN - increment)
        value = ASTRA_PROCESS_NICE_MIN;
    else
        value += increment;
    if (setpriority(PRIO_PROCESS, 0, value) != 0)
        return -1;
    return value;
}

static int
scheduler_process(pid_t process, uint32_t *handle, AstraProcessInfo *info)
{
    if (current_process(handle, info) != 0)
        return -1;
    if (process != 0 && process != (pid_t)info->id) {
        errno = ESRCH;
        return -1;
    }
    return 0;
}

int
sched_get_priority_max(int policy)
{
    if (policy == SCHED_RR)
        return (int)ASTRA_PROCESS_PRIORITY_MAX;
    errno = EINVAL;
    return -1;
}

int
sched_get_priority_min(int policy)
{
    if (policy == SCHED_RR)
        return (int)ASTRA_PROCESS_PRIORITY_MIN;
    errno = EINVAL;
    return -1;
}

int
sched_getparam(pid_t process, struct sched_param *parameters)
{
    AstraProcessInfo info = {0};
    uint32_t handle = 0u;

    if (parameters == NULL) {
        errno = EFAULT;
        return -1;
    }
    if (scheduler_process(process, &handle, &info) != 0)
        return -1;
    parameters->sched_priority = (int)info.default_priority;
    return 0;
}

int
sched_setparam(pid_t process, const struct sched_param *parameters)
{
    AstraProcessInfo info = {0};
    uint32_t handle = 0u;
    uint32_t status;

    if (parameters == NULL) {
        errno = EFAULT;
        return -1;
    }
    if (parameters->sched_priority < (int)ASTRA_PROCESS_PRIORITY_MIN ||
        parameters->sched_priority > (int)ASTRA_PROCESS_PRIORITY_MAX) {
        errno = EINVAL;
        return -1;
    }
    if (scheduler_process(process, &handle, &info) != 0)
        return -1;
    status = astra_process_priority(
        handle, (uint32_t)parameters->sched_priority, NULL);
    if (status == ASTRA_SYSCALL_OK)
        return 0;
    errno = status == ASTRA_SYSCALL_ACCESS_DENIED ? EPERM :
            status == ASTRA_SYSCALL_INVALID_ARGUMENT ? EINVAL : EIO;
    return -1;
}

int
sched_getscheduler(pid_t process)
{
    AstraProcessInfo info = {0};
    uint32_t handle = 0u;

    return scheduler_process(process, &handle, &info) == 0 ? SCHED_RR : -1;
}

int
sched_setscheduler(pid_t process, int policy,
                   const struct sched_param *parameters)
{
    if (policy != SCHED_RR) {
        errno = EINVAL;
        return -1;
    }
    if (sched_setparam(process, parameters) != 0)
        return -1;
    return 0;
}

int
sched_rr_get_interval(pid_t process, struct timespec *interval)
{
    AstraProcessInfo info = {0};
    uint32_t handle = 0u;

    if (interval == NULL) {
        errno = EFAULT;
        return -1;
    }
    if (scheduler_process(process, &handle, &info) != 0)
        return -1;
    interval->tv_sec = 0;
    interval->tv_nsec = ASTRA_PROCESS_QUANTUM_NS;
    return 0;
}

int
sched_yield(void)
{
    uint32_t status = astra_yield();

    if (status == ASTRA_SYSCALL_OK)
        return 0;
    errno = EIO;
    return -1;
}

int
sched_getcpu(void)
{
    return 0;
}

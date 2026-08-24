#define _XOPEN_SOURCE 700

#include <astra/process.h>
#include <astra/runtime.h>
#include <astra/syscall.h>

#include <errno.h>
#include <sys/resource.h>
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

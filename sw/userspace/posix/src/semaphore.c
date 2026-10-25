#include <semaphore.h>

#include <astra/object_abi.h>
#include <astra/runtime.h>
#include <astra/syscall.h>

#include <errno.h>
#include <stdint.h>

static int failure(uint32_t status)
{
    switch (status) {
    case ASTRA_SYSCALL_INVALID_ARGUMENT:
    case ASTRA_SYSCALL_INVALID_HANDLE: errno = EINVAL; break;
    case ASTRA_SYSCALL_OUT_OF_MEMORY: errno = ENOMEM; break;
    case ASTRA_SYSCALL_RESOURCE_LIMIT: errno = EAGAIN; break;
    case ASTRA_SYSCALL_ACCESS_DENIED: errno = EACCES; break;
    case ASTRA_SYSCALL_CANCELLED: errno = EINTR; break;
    default: errno = EIO; break;
    }
    return -1;
}

int sem_init(sem_t *sem, int pshared, unsigned int value)
{
    uint32_t handle;
    uint32_t status;

    if (sem == NULL || value > SEM_VALUE_MAX) {
        errno = EINVAL;
        return -1;
    }
    if (pshared != 0) {
        errno = ENOSYS;
        return -1;
    }
    status = astra_rt_semaphore_create(value, SEM_VALUE_MAX,
        ASTRA_RIGHT_WAIT | ASTRA_RIGHT_SIGNAL, &handle);
    if (status != ASTRA_SYSCALL_OK)
        return failure(status);
    sem->handle = handle;
    return 0;
}

int sem_destroy(sem_t *sem)
{
    uint32_t status;

    if (sem == NULL || sem->handle == 0u) {
        errno = EINVAL;
        return -1;
    }
    status = astra_close(sem->handle);
    if (status != ASTRA_SYSCALL_OK)
        return failure(status);
    sem->handle = 0u;
    return 0;
}

int sem_post(sem_t *sem)
{
    uint32_t status;

    if (sem == NULL || sem->handle == 0u) {
        errno = EINVAL;
        return -1;
    }
    status = astra_rt_signal(sem->handle, 1u, NULL);
    if (status == ASTRA_SYSCALL_RESOURCE_LIMIT) {
        errno = EOVERFLOW;
        return -1;
    }
    return status == ASTRA_SYSCALL_OK ? 0 : failure(status);
}

static int wait_for(sem_t *sem, uint64_t deadline, int timed)
{
    uint32_t status;

    if (sem == NULL || sem->handle == 0u) {
        errno = EINVAL;
        return -1;
    }
    status = astra_wait_one(sem->handle, deadline, NULL);
    if (timed && status == ASTRA_SYSCALL_TIMED_OUT) {
        errno = EAGAIN;
        return -1;
    }
    return status == ASTRA_SYSCALL_OK ? 0 : failure(status);
}

int sem_wait(sem_t *sem)
{
    return wait_for(sem, ASTRA_DEADLINE_FOREVER, 0);
}

int sem_trywait(sem_t *sem)
{
    return wait_for(sem, astra_clock_monotonic(), 1);
}

#include <semaphore.h>

#include <astra/object_abi.h>
#include <astra/runtime.h>
#include <astra/syscall.h>

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>

static uint32_t count;
static uint32_t next_wait_status;
static uint32_t closed;

uint32_t astra_rt_semaphore_create(uint32_t initial, uint32_t maximum,
                                   uint32_t rights, uint32_t *handle)
{
    assert(maximum == SEM_VALUE_MAX);
    assert(rights == (ASTRA_RIGHT_WAIT | ASTRA_RIGHT_SIGNAL));
    count = initial;
    *handle = 42u;
    return ASTRA_SYSCALL_OK;
}

uint32_t astra_rt_signal(uint32_t handle, uint32_t amount, uint32_t *woken)
{
    assert(handle == 42u && amount == 1u && woken == NULL);
    if (count == SEM_VALUE_MAX)
        return ASTRA_SYSCALL_RESOURCE_LIMIT;
    ++count;
    return ASTRA_SYSCALL_OK;
}

uint32_t astra_wait_one(uint32_t handle, uint64_t deadline, uint32_t *detail)
{
    assert(handle == 42u && detail == NULL);
    assert(deadline == ASTRA_DEADLINE_FOREVER || deadline == 99u);
    if (next_wait_status != ASTRA_SYSCALL_OK) {
        uint32_t status = next_wait_status;
        next_wait_status = ASTRA_SYSCALL_OK;
        return status;
    }
    if (count == 0u)
        return ASTRA_SYSCALL_TIMED_OUT;
    --count;
    return ASTRA_SYSCALL_OK;
}

uint64_t astra_clock_monotonic(void) { return 99u; }

uint32_t astra_close(uint32_t handle)
{
    assert(handle == 42u);
    ++closed;
    return ASTRA_SYSCALL_OK;
}

int main(void)
{
    sem_t sem = {0};

    assert(sem_init(NULL, 0, 0) == -1 && errno == EINVAL);
    assert(sem_init(&sem, 1, 0) == -1 && errno == ENOSYS);
    assert(sem_init(&sem, 0, SEM_VALUE_MAX + 1u) == -1 && errno == EINVAL);
    assert(sem_init(&sem, 0, 0) == 0 && sem.handle == 42u);
    assert(sem_trywait(&sem) == -1 && errno == EAGAIN);
    assert(sem_post(&sem) == 0 && count == 1u);
    assert(sem_wait(&sem) == 0 && count == 0u);
    next_wait_status = ASTRA_SYSCALL_CANCELLED;
    assert(sem_wait(&sem) == -1 && errno == EINTR);
    count = SEM_VALUE_MAX;
    assert(sem_post(&sem) == -1 && errno == EOVERFLOW);
    assert(sem_destroy(&sem) == 0 && sem.handle == 0u && closed == 1u);
    assert(sem_wait(&sem) == -1 && errno == EINVAL);
    puts("astra posix semaphore: PASS");
    return 0;
}

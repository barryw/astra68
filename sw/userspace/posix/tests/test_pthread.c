#include <pthread.h>

#include <astra/process.h>
#include <astra/runtime.h>
#include <astra/syscall.h>

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>

static uint32_t wake_count;
static uint32_t once_calls;

uint32_t astra_query_abi(uint32_t *abi, uint32_t *process, uint32_t *thread)
{
    if (abi != NULL)
        *abi = ASTRA_SYSCALL_ABI_VERSION;
    if (process != NULL)
        *process = 3u;
    if (thread != NULL)
        *thread = 7u;
    return ASTRA_SYSCALL_OK;
}

uint32_t astra_futex_wait(volatile uint32_t *address, uint32_t expected,
                          uint64_t deadline)
{
    (void)address;
    (void)expected;
    (void)deadline;
    return ASTRA_SYSCALL_WOULD_BLOCK;
}

uint32_t astra_futex_wake(volatile uint32_t *address, uint32_t count,
                          uint32_t *woken)
{
    (void)address;
    wake_count = count;
    if (woken != NULL)
        *woken = 0u;
    return ASTRA_SYSCALL_OK;
}

uint64_t astra_clock_monotonic(void) { return 10u; }
uint32_t astra_clock_realtime(uint64_t *value)
{
    *value = 20u;
    return ASTRA_SYSCALL_OK;
}
uint32_t astra_close(uint32_t handle) { (void)handle; return ASTRA_SYSCALL_OK; }
uint32_t astra_process_info(uint32_t handle, AstraProcessInfo *info)
{
    (void)handle;
    info->default_priority = ASTRA_PROCESS_PRIORITY_NORMAL;
    return ASTRA_SYSCALL_OK;
}
uint32_t astra_rt_thread_create(const AstraThreadStart *start,
                                uint32_t priority, uint32_t rights,
                                uint32_t *handle, uint32_t *id)
{
    (void)start; (void)priority; (void)rights; (void)id;
    *handle = 9u;
    return ASTRA_SYSCALL_OK;
}
void astra_thread_exit(uint32_t status) { (void)status; abort(); }
uint32_t astra_wait_one(uint32_t handle, uint64_t deadline, uint32_t *detail)
{
    (void)handle; (void)deadline;
    *detail = 0u;
    return ASTRA_SYSCALL_OK;
}

static void initialize_once(void) { ++once_calls; }

int main(void)
{
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutexattr_t attr;
    pthread_cond_t condition = PTHREAD_COND_INITIALIZER;
    pthread_rwlock_t rwlock = PTHREAD_RWLOCK_INITIALIZER;
    pthread_once_t once = PTHREAD_ONCE_INIT;
    pthread_key_t key;
    int value = 42;

    assert(pthread_mutex_lock(&mutex) == 0);
    assert(pthread_mutex_trylock(&mutex) != 0);
    assert(pthread_mutex_unlock(&mutex) == 0);
    assert(pthread_mutexattr_init(&attr) == 0);
    assert(pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE) == 0);
    assert(pthread_mutex_init(&mutex, &attr) == 0);
    assert(pthread_mutex_lock(&mutex) == 0);
    assert(pthread_mutex_lock(&mutex) == 0);
    assert(mutex.depth == 2u);
    assert(pthread_mutex_unlock(&mutex) == 0);
    assert(pthread_mutex_unlock(&mutex) == 0);
    assert(pthread_once(&once, initialize_once) == 0);
    assert(pthread_once(&once, initialize_once) == 0);
    assert(once_calls == 1u);
    assert(pthread_cond_signal(&condition) == 0 && wake_count == 1u);
    assert(pthread_cond_broadcast(&condition) == 0 &&
           wake_count == UINT32_MAX);
    assert(pthread_rwlock_rdlock(&rwlock) == 0);
    assert(pthread_rwlock_trywrlock(&rwlock) == EBUSY);
    assert(pthread_rwlock_unlock(&rwlock) == 0);
    assert(pthread_rwlock_wrlock(&rwlock) == 0);
    assert(pthread_rwlock_tryrdlock(&rwlock) == EBUSY);
    assert(pthread_rwlock_unlock(&rwlock) == 0);
    assert(pthread_rwlock_destroy(&rwlock) == 0);
    assert(pthread_key_create(&key, NULL) == 0);
    assert(pthread_setspecific(key, &value) == 0);
    assert(pthread_getspecific(key) == &value);
    assert(pthread_key_delete(key) == 0);
    return 0;
}

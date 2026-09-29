#include <pthread.h>

#include <astra/process.h>
#include <astra/runtime.h>
#include <astra/syscall.h>

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>

#include "../src/thread_internal.h"

static uint32_t wake_count;
static uint32_t mutex_wait_scenario;
static uint32_t mutex_wait_calls;
static uint32_t once_calls;
static uint32_t created_thread_rights;
static uint8_t mock_thread_priority = ASTRA_PROCESS_PRIORITY_NORMAL;
static uint32_t thread_queries;

uint32_t astra_query_abi(uint32_t *abi, uint32_t *process, uint32_t *thread)
{
    if (thread != NULL)
        ++thread_queries;
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
    if (mutex_wait_scenario != 0u) {
        assert(expected == 2u && deadline == ASTRA_DEADLINE_FOREVER);
        assert(++mutex_wait_calls <= mutex_wait_scenario);
        if (mutex_wait_calls == mutex_wait_scenario) {
            __atomic_store_n(address, 0u, __ATOMIC_RELEASE);
            return ASTRA_SYSCALL_OK;
        }
    }
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
uint32_t astra_thread_info(uint32_t handle, AstraThreadInfo *info)
{
    if (handle != 7u && handle != 9u)
        return ASTRA_SYSCALL_INVALID_HANDLE;
    info->base_priority = mock_thread_priority;
    return ASTRA_SYSCALL_OK;
}
uint32_t astra_thread_priority(uint32_t handle, uint32_t priority,
                               uint32_t *previous_priority)
{
    if (handle != 7u && handle != 9u)
        return ASTRA_SYSCALL_INVALID_HANDLE;
    if (priority > ASTRA_PROCESS_PRIORITY_NORMAL)
        return ASTRA_SYSCALL_ACCESS_DENIED;
    if (previous_priority != NULL)
        *previous_priority = mock_thread_priority;
    mock_thread_priority = (uint8_t)priority;
    return ASTRA_SYSCALL_OK;
}
uint32_t astra_rt_thread_create(const AstraThreadStart *start,
                                uint32_t priority, uint32_t rights,
                                uint32_t *handle, uint32_t *id)
{
    (void)start; (void)priority; (void)id;
    created_thread_rights = rights;
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
static void *thread_start(void *argument) { return argument; }

int main(void)
{
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutexattr_t attr;
    pthread_cond_t condition = PTHREAD_COND_INITIALIZER;
    pthread_rwlock_t rwlock = PTHREAD_RWLOCK_INITIALIZER;
    pthread_once_t once = PTHREAD_ONCE_INIT;
    pthread_key_t key;
    pthread_attr_t thread_attr;
    size_t stack_size;
    pthread_t thread;
    struct sched_param scheduling;
    int scheduling_policy;
    sigset_t requested_mask;
    sigset_t previous_mask;
    int value = 42;

    /* A thread's identity is asked of the kernel once, not per lock. */
    assert(pthread_self() == 7u && pthread_self() == 7u);
    assert(pthread_mutex_lock(&mutex) == 0 &&
           pthread_mutex_unlock(&mutex) == 0);
    assert(thread_queries == 1u);
    astra_posix_thread_after_fork_child();
    assert(pthread_self() == 7u && thread_queries == 2u);

    assert(sigemptyset(&requested_mask) == 0);
    assert(sigaddset(&requested_mask, SIGINT) == 0);
    assert(pthread_sigmask(SIG_BLOCK, &requested_mask, &previous_mask) == 0);
    assert(pthread_sigmask(SIG_SETMASK, NULL, &requested_mask) == 0);
    assert(sigismember(&requested_mask, SIGINT) == 1);
    assert(pthread_sigmask(-1, &requested_mask, NULL) == EINVAL);
    assert(pthread_sigmask(SIG_SETMASK, NULL, &requested_mask) == 0);
    assert(sigismember(&requested_mask, SIGINT) == 1);
    assert(pthread_sigmask(SIG_SETMASK, &previous_mask, NULL) == 0);

    assert(pthread_attr_init(&thread_attr) == 0);
    assert(pthread_attr_getstacksize(&thread_attr, &stack_size) == 0);
    assert(stack_size == ASTRA_THREAD_STACK_BYTES_MAX);
    assert(pthread_attr_setstacksize(&thread_attr, PTHREAD_STACK_MIN) == 0);
    assert(pthread_attr_getstacksize(&thread_attr, &stack_size) == 0);
    assert(stack_size == PTHREAD_STACK_MIN);
    assert(pthread_attr_setstacksize(&thread_attr, 0u) == EINVAL);
    assert(pthread_attr_setstacksize(&thread_attr,
                                    ASTRA_THREAD_STACK_BYTES_MAX + 1u) == EINVAL);
    assert(pthread_attr_setstacksize(NULL, PTHREAD_STACK_MIN) == EINVAL);
    assert(pthread_attr_getstacksize(&thread_attr, NULL) == EINVAL);
    assert(pthread_create(&thread, &thread_attr, thread_start, NULL) == 0);
    assert((created_thread_rights & ASTRA_RIGHT_ADMINISTER) != 0u);
    thread_attr.stack_size = 0u;
    assert(pthread_create(&thread, &thread_attr, thread_start, NULL) == EINVAL);

    assert(pthread_getschedparam(9u, &scheduling_policy, &scheduling) == 0);
    assert(scheduling_policy == SCHED_RR &&
           scheduling.sched_priority == ASTRA_PROCESS_PRIORITY_NORMAL);
    scheduling.sched_priority = 12;
    assert(pthread_setschedparam(9u, SCHED_RR, &scheduling) == 0);
    assert(mock_thread_priority == 12u);
    assert(pthread_getschedparam(9u, &scheduling_policy, &scheduling) == 0 &&
           scheduling.sched_priority == 12);
    scheduling.sched_priority = ASTRA_PROCESS_PRIORITY_NORMAL + 1;
    assert(pthread_setschedparam(9u, SCHED_RR, &scheduling) == EPERM);
    assert(mock_thread_priority == 12u);
    assert(pthread_setschedparam(9u, SCHED_OTHER, &scheduling) == EINVAL);
    assert(pthread_getschedparam(999u, &scheduling_policy, &scheduling) == ESRCH);
    assert(pthread_setschedparam(999u, SCHED_RR, &scheduling) == ESRCH);

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
    assert(pthread_mutex_init(&mutex, NULL) == 0);
    for (uint32_t scenario = 1u; scenario <= 2u; ++scenario) {
        mutex.state = 1u;
        mutex.owner = 9u;
        mutex.depth = 1u;
        mutex_wait_scenario = scenario;
        mutex_wait_calls = 0u;
        assert(pthread_mutex_lock(&mutex) == 0);
        assert(mutex_wait_calls == scenario && mutex.owner == 7u &&
               mutex.depth == 1u && mutex.state == 2u);
        mutex_wait_scenario = 0u;
        assert(pthread_mutex_unlock(&mutex) == 0);
        assert(mutex.state == 0u);
    }
    assert(pthread_mutex_lock(NULL) == EINVAL);
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

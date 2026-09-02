#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE 1

#include <pthread.h>

#include <astra/process.h>
#include <astra/runtime.h>
#include <astra/syscall.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#ifndef ASTRA_PTHREAD_NO_LIBC_LOCKS
#include <sys/lock.h>
#endif

typedef struct ThreadStart {
    AstraThreadStart astra;
    void *(*function)(void *);
    void *argument;
    uint8_t detached;
} ThreadStart;

typedef struct SpecificKey {
    struct SpecificKey *next;
    void (*destructor)(void *);
    uint32_t references;
    volatile uint8_t active;
} SpecificKey;

typedef struct SpecificValue {
    struct SpecificValue *next;
    SpecificKey *key;
    const void *value;
} SpecificValue;

static pthread_mutex_t key_mutex = PTHREAD_MUTEX_INITIALIZER;
static SpecificKey *keys;
static _Thread_local SpecificValue *specific_values;

static bool mutex_type_valid(int type)
{
    return type == PTHREAD_MUTEX_NORMAL || type == PTHREAD_MUTEX_ERRORCHECK ||
           type == PTHREAD_MUTEX_RECURSIVE;
}

static int syscall_error(uint32_t status)
{
    switch (status) {
    case ASTRA_SYSCALL_OK: return 0;
    case ASTRA_SYSCALL_TIMED_OUT: return ETIMEDOUT;
    case ASTRA_SYSCALL_OUT_OF_MEMORY: return ENOMEM;
    case ASTRA_SYSCALL_RESOURCE_LIMIT: return EAGAIN;
    case ASTRA_SYSCALL_INVALID_ARGUMENT: return EINVAL;
    case ASTRA_SYSCALL_INVALID_HANDLE: return ESRCH;
    default: return EIO;
    }
}

static uint64_t timespec_ns(const struct timespec *time)
{
    return (uint64_t)time->tv_sec * UINT64_C(1000000000) +
           (uint32_t)time->tv_nsec;
}

static int condition_deadline(const pthread_cond_t *condition,
                              const struct timespec *abstime,
                              uint64_t *deadline)
{
    uint64_t absolute;

    if (abstime == NULL || deadline == NULL || abstime->tv_sec < 0 ||
        abstime->tv_nsec < 0 || abstime->tv_nsec >= 1000000000L)
        return EINVAL;
    if ((uint64_t)abstime->tv_sec > UINT64_MAX / UINT64_C(1000000000))
        return EINVAL;
    absolute = timespec_ns(abstime);
    if (condition->clock_id == CLOCK_MONOTONIC) {
        *deadline = absolute;
        return 0;
    }
    if (condition->clock_id == CLOCK_REALTIME) {
        uint64_t realtime;
        uint64_t monotonic = astra_clock_monotonic();
        uint64_t delta;

        if (astra_clock_realtime(&realtime) != ASTRA_SYSCALL_OK)
            return EINVAL;
        if (absolute <= realtime) {
            *deadline = monotonic;
            return 0;
        }
        delta = absolute - realtime;
        *deadline = UINT64_MAX - monotonic < delta ? UINT64_MAX :
                                                     monotonic + delta;
        return 0;
    }
    return EINVAL;
}

pthread_t pthread_self(void)
{
    uint32_t thread = 0u;

    return astra_query_abi(NULL, NULL, &thread) == ASTRA_SYSCALL_OK ? thread :
                                                                      0u;
}

int pthread_equal(pthread_t left, pthread_t right)
{
    return left == right;
}

int pthread_attr_init(pthread_attr_t *attr)
{
    if (attr == NULL)
        return EINVAL;
    attr->detach_state = PTHREAD_CREATE_JOINABLE;
    return 0;
}

int pthread_attr_destroy(pthread_attr_t *attr)
{
    return attr == NULL ? EINVAL : 0;
}

int pthread_attr_getdetachstate(const pthread_attr_t *attr, int *state)
{
    if (attr == NULL || state == NULL)
        return EINVAL;
    *state = attr->detach_state;
    return 0;
}

int pthread_attr_setdetachstate(pthread_attr_t *attr, int state)
{
    if (attr == NULL || (state != PTHREAD_CREATE_JOINABLE &&
                         state != PTHREAD_CREATE_DETACHED))
        return EINVAL;
    attr->detach_state = state;
    return 0;
}

static void specific_destroy(void)
{
    for (uint32_t pass = 0u; pass < PTHREAD_DESTRUCTOR_ITERATIONS; ++pass) {
        bool called = false;

        for (SpecificValue *value = specific_values; value != NULL;
             value = value->next) {
            void *stored = (void *)value->value;

            if (stored != NULL &&
                __atomic_load_n(&value->key->active, __ATOMIC_ACQUIRE) != 0u &&
                value->key->destructor != NULL) {
                value->value = NULL;
                value->key->destructor(stored);
                called = true;
            }
        }
        if (!called)
            break;
    }
    while (specific_values != NULL) {
        SpecificValue *value = specific_values;

        specific_values = value->next;
        pthread_mutex_lock(&key_mutex);
        if (--value->key->references == 0u)
            free(value->key);
        pthread_mutex_unlock(&key_mutex);
        free(value);
    }
}

void pthread_exit(void *result)
{
    specific_destroy();
    astra_thread_exit((uint32_t)(uintptr_t)result);
}

static void thread_entry(uint32_t argument)
{
    ThreadStart *start = (ThreadStart *)(uintptr_t)argument;
    void *(*function)(void *) = start->function;
    void *value = start->argument;
    bool detached = start->detached != 0u;

    free(start);
    if (detached)
        (void)astra_close(pthread_self());
    pthread_exit(function(value));
}

/* libstdc++'s static-link proxy takes these addresses through an intentionally
 * argument-less function type.  Keep that link-only call opaque to LTO. */
__attribute__((noinline))
int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                   void *(*start)(void *), void *argument)
{
    ThreadStart *record;
    AstraProcessInfo info = {0};
    uint32_t process;
    uint32_t status;
    uint32_t rights = ASTRA_RIGHT_READ | ASTRA_RIGHT_WAIT;
    int detached = attr == NULL ? PTHREAD_CREATE_JOINABLE :
                                  attr->detach_state;

    if (thread == NULL || start == NULL ||
        (detached != PTHREAD_CREATE_JOINABLE &&
         detached != PTHREAD_CREATE_DETACHED))
        return EINVAL;
    record = malloc(sizeof(*record));
    if (record == NULL)
        return ENOMEM;
    if (astra_query_abi(NULL, &process, NULL) != ASTRA_SYSCALL_OK) {
        free(record);
        return EIO;
    }
    info.size = sizeof(info);
    if (astra_process_info(process, &info) != ASTRA_SYSCALL_OK) {
        free(record);
        return EIO;
    }
    record->astra.entry = thread_entry;
    record->astra.argument = (uint32_t)(uintptr_t)record;
    record->function = start;
    record->argument = argument;
    record->detached = (uint8_t)detached;
    status = astra_rt_thread_create(&record->astra, info.default_priority,
                                    rights, thread, NULL);
    if (status != ASTRA_SYSCALL_OK) {
        free(record);
        return syscall_error(status);
    }
    return 0;
}

__attribute__((noinline))
int pthread_join(pthread_t thread, void **result)
{
    uint32_t value;
    uint32_t status;

    if (thread == 0u)
        return ESRCH;
    if (thread == pthread_self())
        return EDEADLK;
    status = astra_wait_one(thread, ASTRA_DEADLINE_FOREVER, &value);
    if (status != ASTRA_SYSCALL_OK)
        return syscall_error(status);
    status = astra_close(thread);
    if (status != ASTRA_SYSCALL_OK)
        return syscall_error(status);
    if (result != NULL)
        *result = (void *)(uintptr_t)value;
    return 0;
}

int pthread_detach(pthread_t thread)
{
    return syscall_error(astra_close(thread));
}

int pthread_mutexattr_init(pthread_mutexattr_t *attr)
{
    if (attr == NULL)
        return EINVAL;
    attr->type = PTHREAD_MUTEX_DEFAULT;
    return 0;
}

int pthread_mutexattr_destroy(pthread_mutexattr_t *attr)
{
    return attr == NULL ? EINVAL : 0;
}

int pthread_mutexattr_gettype(const pthread_mutexattr_t *attr, int *type)
{
    if (attr == NULL || type == NULL)
        return EINVAL;
    *type = attr->type;
    return 0;
}

int pthread_mutexattr_settype(pthread_mutexattr_t *attr, int type)
{
    if (attr == NULL || !mutex_type_valid(type))
        return EINVAL;
    attr->type = type;
    return 0;
}

int pthread_mutex_init(pthread_mutex_t *mutex,
                       const pthread_mutexattr_t *attr)
{
    int type = attr == NULL ? PTHREAD_MUTEX_DEFAULT : attr->type;

    if (mutex == NULL || !mutex_type_valid(type))
        return EINVAL;
    *mutex = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
    mutex->type = (uint8_t)type;
    return 0;
}

int pthread_mutex_destroy(pthread_mutex_t *mutex)
{
    if (mutex == NULL)
        return EINVAL;
    return __atomic_load_n(&mutex->state, __ATOMIC_RELAXED) == 0u ? 0 : EBUSY;
}

int pthread_mutex_trylock(pthread_mutex_t *mutex)
{
    uint32_t expected = 0u;
    pthread_t self;

    if (mutex == NULL || !mutex_type_valid(mutex->type))
        return EINVAL;
    self = pthread_self();
    if (mutex->owner == self && mutex->state != 0u) {
        if (mutex->type == PTHREAD_MUTEX_RECURSIVE) {
            if (mutex->depth == UINT32_MAX)
                return EAGAIN;
            ++mutex->depth;
            return 0;
        }
        return EBUSY;
    }
    if (!__atomic_compare_exchange_n(&mutex->state, &expected, 1u, false,
                                     __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
        return EBUSY;
    mutex->owner = self;
    mutex->depth = 1u;
    return 0;
}

int pthread_mutex_lock(pthread_mutex_t *mutex)
{
    uint32_t expected = 0u;
    pthread_t self;

    if (mutex == NULL || !mutex_type_valid(mutex->type))
        return EINVAL;
    self = pthread_self();
    if (mutex->owner == self && mutex->state != 0u) {
        if (mutex->type == PTHREAD_MUTEX_RECURSIVE) {
            if (mutex->depth == UINT32_MAX)
                return EAGAIN;
            ++mutex->depth;
            return 0;
        }
        if (mutex->type == PTHREAD_MUTEX_ERRORCHECK)
            return EDEADLK;
    }
    if (!__atomic_compare_exchange_n(&mutex->state, &expected, 1u, false,
                                     __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
        uint32_t state = expected;

        for (;;) {
            if (state != 2u)
                state = __atomic_exchange_n(&mutex->state, 2u,
                                            __ATOMIC_ACQUIRE);
            if (state == 0u)
                break;
            (void)astra_futex_wait(&mutex->state, 2u,
                                   ASTRA_DEADLINE_FOREVER);
            state = __atomic_exchange_n(&mutex->state, 2u,
                                        __ATOMIC_ACQUIRE);
        }
    }
    mutex->owner = self;
    mutex->depth = 1u;
    return 0;
}

int pthread_mutex_unlock(pthread_mutex_t *mutex)
{
    pthread_t self;

    if (mutex == NULL || !mutex_type_valid(mutex->type))
        return EINVAL;
    self = pthread_self();
    if (mutex->state == 0u || mutex->owner != self)
        return EPERM;
    if (mutex->type == PTHREAD_MUTEX_RECURSIVE && mutex->depth > 1u) {
        --mutex->depth;
        return 0;
    }
    mutex->owner = 0u;
    mutex->depth = 0u;
    if (__atomic_fetch_sub(&mutex->state, 1u, __ATOMIC_RELEASE) != 1u) {
        __atomic_store_n(&mutex->state, 0u, __ATOMIC_RELEASE);
        (void)astra_futex_wake(&mutex->state, 1u, NULL);
    }
    return 0;
}

int pthread_condattr_init(pthread_condattr_t *attr)
{
    if (attr == NULL)
        return EINVAL;
    attr->clock_id = CLOCK_REALTIME;
    return 0;
}

int pthread_condattr_destroy(pthread_condattr_t *attr)
{
    return attr == NULL ? EINVAL : 0;
}

int pthread_condattr_getclock(const pthread_condattr_t *attr, int *clock_id)
{
    if (attr == NULL || clock_id == NULL)
        return EINVAL;
    *clock_id = attr->clock_id;
    return 0;
}

int pthread_condattr_setclock(pthread_condattr_t *attr, int clock_id)
{
    if (attr == NULL || (clock_id != CLOCK_REALTIME &&
                         clock_id != CLOCK_MONOTONIC))
        return EINVAL;
    attr->clock_id = clock_id;
    return 0;
}

int pthread_cond_init(pthread_cond_t *condition,
                      const pthread_condattr_t *attr)
{
    if (condition == NULL)
        return EINVAL;
    condition->sequence = 0u;
    condition->waiters = 0u;
    condition->clock_id = attr == NULL ? CLOCK_REALTIME : attr->clock_id;
    return condition->clock_id == CLOCK_REALTIME ||
           condition->clock_id == CLOCK_MONOTONIC ? 0 : EINVAL;
}

int pthread_cond_destroy(pthread_cond_t *condition)
{
    if (condition == NULL)
        return EINVAL;
    return __atomic_load_n(&condition->waiters, __ATOMIC_RELAXED) == 0u ? 0 :
                                                                            EBUSY;
}

static int condition_wait(pthread_cond_t *condition, pthread_mutex_t *mutex,
                          uint64_t deadline)
{
    uint32_t sequence;
    uint32_t status;
    int unlock_status;
    int lock_status;

    if (condition == NULL || mutex == NULL)
        return EINVAL;
    sequence = __atomic_load_n(&condition->sequence, __ATOMIC_ACQUIRE);
    __atomic_add_fetch(&condition->waiters, 1u, __ATOMIC_RELAXED);
    unlock_status = pthread_mutex_unlock(mutex);
    if (unlock_status != 0) {
        __atomic_sub_fetch(&condition->waiters, 1u, __ATOMIC_RELAXED);
        return unlock_status;
    }
    status = astra_futex_wait(&condition->sequence, sequence, deadline);
    __atomic_sub_fetch(&condition->waiters, 1u, __ATOMIC_RELAXED);
    lock_status = pthread_mutex_lock(mutex);
    if (lock_status != 0)
        return lock_status;
    return status == ASTRA_SYSCALL_OK || status == ASTRA_SYSCALL_WOULD_BLOCK ?
               0 : syscall_error(status);
}

int pthread_cond_wait(pthread_cond_t *condition, pthread_mutex_t *mutex)
{
    return condition_wait(condition, mutex, ASTRA_DEADLINE_FOREVER);
}

int pthread_cond_timedwait(pthread_cond_t *condition, pthread_mutex_t *mutex,
                           const struct timespec *abstime)
{
    uint64_t deadline;
    int status = condition_deadline(condition, abstime, &deadline);

    return status == 0 ? condition_wait(condition, mutex, deadline) : status;
}

int pthread_cond_signal(pthread_cond_t *condition)
{
    if (condition == NULL)
        return EINVAL;
    __atomic_add_fetch(&condition->sequence, 1u, __ATOMIC_RELEASE);
    return syscall_error(astra_futex_wake(&condition->sequence, 1u, NULL));
}

int pthread_cond_broadcast(pthread_cond_t *condition)
{
    if (condition == NULL)
        return EINVAL;
    __atomic_add_fetch(&condition->sequence, 1u, __ATOMIC_RELEASE);
    return syscall_error(astra_futex_wake(&condition->sequence, UINT32_MAX,
                                          NULL));
}

int pthread_rwlockattr_init(pthread_rwlockattr_t *attr)
{
    if (attr == NULL)
        return EINVAL;
    attr->process_shared = PTHREAD_PROCESS_PRIVATE;
    return 0;
}

int pthread_rwlockattr_destroy(pthread_rwlockattr_t *attr)
{
    return attr == NULL ? EINVAL : 0;
}

int pthread_rwlockattr_getpshared(const pthread_rwlockattr_t *attr,
                                 int *process_shared)
{
    if (attr == NULL || process_shared == NULL)
        return EINVAL;
    *process_shared = attr->process_shared;
    return 0;
}

int pthread_rwlockattr_setpshared(pthread_rwlockattr_t *attr,
                                 int process_shared)
{
    if (attr == NULL || (process_shared != PTHREAD_PROCESS_PRIVATE &&
                         process_shared != PTHREAD_PROCESS_SHARED))
        return EINVAL;
    if (process_shared == PTHREAD_PROCESS_SHARED)
        return ENOTSUP;
    attr->process_shared = process_shared;
    return 0;
}

int pthread_rwlock_init(pthread_rwlock_t *rwlock,
                        const pthread_rwlockattr_t *attr)
{
    if (rwlock == NULL)
        return EINVAL;
    if (attr != NULL && attr->process_shared != PTHREAD_PROCESS_PRIVATE)
        return attr->process_shared == PTHREAD_PROCESS_SHARED ? ENOTSUP :
                                                                EINVAL;
    *rwlock = (pthread_rwlock_t)PTHREAD_RWLOCK_INITIALIZER;
    return 0;
}

int pthread_rwlock_destroy(pthread_rwlock_t *rwlock)
{
    if (rwlock == NULL)
        return EINVAL;
    return rwlock->writer == 0u && rwlock->active_readers == 0u &&
           rwlock->waiting_writers == 0u ? 0 : EBUSY;
}

int pthread_rwlock_rdlock(pthread_rwlock_t *rwlock)
{
    int status;

    if (rwlock == NULL)
        return EINVAL;
    status = pthread_mutex_lock(&rwlock->mutex);
    if (status != 0)
        return status;
    while (rwlock->writer != 0u || rwlock->waiting_writers != 0u) {
        status = pthread_cond_wait(&rwlock->readers, &rwlock->mutex);
        if (status != 0) {
            (void)pthread_mutex_unlock(&rwlock->mutex);
            return status;
        }
    }
    if (rwlock->active_readers == UINT32_MAX)
        status = EAGAIN;
    else
        ++rwlock->active_readers;
    (void)pthread_mutex_unlock(&rwlock->mutex);
    return status;
}

int pthread_rwlock_tryrdlock(pthread_rwlock_t *rwlock)
{
    int status;

    if (rwlock == NULL)
        return EINVAL;
    status = pthread_mutex_lock(&rwlock->mutex);
    if (status != 0)
        return status;
    if (rwlock->writer != 0u || rwlock->waiting_writers != 0u)
        status = EBUSY;
    else if (rwlock->active_readers == UINT32_MAX)
        status = EAGAIN;
    else
        ++rwlock->active_readers;
    (void)pthread_mutex_unlock(&rwlock->mutex);
    return status;
}

int pthread_rwlock_wrlock(pthread_rwlock_t *rwlock)
{
    int status;

    if (rwlock == NULL)
        return EINVAL;
    status = pthread_mutex_lock(&rwlock->mutex);
    if (status != 0)
        return status;
    ++rwlock->waiting_writers;
    while (rwlock->writer != 0u || rwlock->active_readers != 0u) {
        status = pthread_cond_wait(&rwlock->writers, &rwlock->mutex);
        if (status != 0) {
            --rwlock->waiting_writers;
            (void)pthread_mutex_unlock(&rwlock->mutex);
            return status;
        }
    }
    --rwlock->waiting_writers;
    rwlock->writer = pthread_self();
    (void)pthread_mutex_unlock(&rwlock->mutex);
    return 0;
}

int pthread_rwlock_trywrlock(pthread_rwlock_t *rwlock)
{
    int status;

    if (rwlock == NULL)
        return EINVAL;
    status = pthread_mutex_lock(&rwlock->mutex);
    if (status != 0)
        return status;
    if (rwlock->writer != 0u || rwlock->active_readers != 0u)
        status = EBUSY;
    else
        rwlock->writer = pthread_self();
    (void)pthread_mutex_unlock(&rwlock->mutex);
    return status;
}

int pthread_rwlock_unlock(pthread_rwlock_t *rwlock)
{
    int status;

    if (rwlock == NULL)
        return EINVAL;
    status = pthread_mutex_lock(&rwlock->mutex);
    if (status != 0)
        return status;
    if (rwlock->writer != 0u) {
        if (rwlock->writer != pthread_self())
            status = EPERM;
        else
            rwlock->writer = 0u;
    } else if (rwlock->active_readers != 0u) {
        --rwlock->active_readers;
    } else {
        status = EPERM;
    }
    if (status == 0 && rwlock->writer == 0u &&
        rwlock->active_readers == 0u) {
        if (rwlock->waiting_writers != 0u)
            (void)pthread_cond_signal(&rwlock->writers);
        else
            (void)pthread_cond_broadcast(&rwlock->readers);
    }
    (void)pthread_mutex_unlock(&rwlock->mutex);
    return status;
}

int pthread_once(pthread_once_t *once, void (*initialize)(void))
{
    uint32_t expected = 0u;

    if (once == NULL || initialize == NULL)
        return EINVAL;
    if (__atomic_load_n(once, __ATOMIC_ACQUIRE) == 2u)
        return 0;
    if (__atomic_compare_exchange_n(once, &expected, 1u, false,
                                    __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
        initialize();
        __atomic_store_n(once, 2u, __ATOMIC_RELEASE);
        (void)astra_futex_wake(once, UINT32_MAX, NULL);
        return 0;
    }
    while (__atomic_load_n(once, __ATOMIC_ACQUIRE) != 2u)
        (void)astra_futex_wait(once, 1u, ASTRA_DEADLINE_FOREVER);
    return 0;
}

int pthread_key_create(pthread_key_t *key, void (*destructor)(void *))
{
    SpecificKey *created;

    if (key == NULL)
        return EINVAL;
    created = malloc(sizeof(*created));
    if (created == NULL)
        return EAGAIN;
    created->destructor = destructor;
    created->references = 1u;
    created->active = 1u;
    pthread_mutex_lock(&key_mutex);
    created->next = keys;
    keys = created;
    pthread_mutex_unlock(&key_mutex);
    *key = (pthread_key_t)(uintptr_t)created;
    return 0;
}

int pthread_key_delete(pthread_key_t key)
{
    SpecificKey *deleted = (SpecificKey *)(uintptr_t)key;
    SpecificKey **link;

    if (deleted == NULL)
        return EINVAL;
    pthread_mutex_lock(&key_mutex);
    for (link = &keys; *link != NULL && *link != deleted;
         link = &(*link)->next) {
    }
    if (*link == NULL || deleted->active == 0u) {
        pthread_mutex_unlock(&key_mutex);
        return EINVAL;
    }
    *link = deleted->next;
    __atomic_store_n(&deleted->active, 0u, __ATOMIC_RELEASE);
    if (--deleted->references == 0u)
        free(deleted);
    pthread_mutex_unlock(&key_mutex);
    return 0;
}

int pthread_setspecific(pthread_key_t key, const void *value)
{
    SpecificKey *selected = (SpecificKey *)(uintptr_t)key;
    SpecificValue *entry;

    if (selected == NULL ||
        __atomic_load_n(&selected->active, __ATOMIC_ACQUIRE) == 0u)
        return EINVAL;
    for (entry = specific_values; entry != NULL; entry = entry->next) {
        if (entry->key == selected) {
            entry->value = value;
            return 0;
        }
    }
    if (value == NULL)
        return 0;
    entry = malloc(sizeof(*entry));
    if (entry == NULL)
        return ENOMEM;
    pthread_mutex_lock(&key_mutex);
    if (__atomic_load_n(&selected->active, __ATOMIC_ACQUIRE) == 0u) {
        pthread_mutex_unlock(&key_mutex);
        free(entry);
        return EINVAL;
    }
    ++selected->references;
    pthread_mutex_unlock(&key_mutex);
    entry->key = selected;
    entry->value = value;
    entry->next = specific_values;
    specific_values = entry;
    return 0;
}

void *pthread_getspecific(pthread_key_t key)
{
    SpecificKey *selected = (SpecificKey *)(uintptr_t)key;

    if (selected == NULL ||
        __atomic_load_n(&selected->active, __ATOMIC_ACQUIRE) == 0u)
        return NULL;
    for (SpecificValue *entry = specific_values; entry != NULL;
         entry = entry->next) {
        if (entry->key == selected)
            return (void *)entry->value;
    }
    return NULL;
}

#ifndef ASTRA_PTHREAD_NO_LIBC_LOCKS
struct __lock {
    pthread_mutex_t mutex;
};

#define ASTRA_LIBC_LOCK_INITIALIZER \
    {PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP}

struct __lock __lock___libc_recursive_mutex = ASTRA_LIBC_LOCK_INITIALIZER;
struct __lock __lock___sfp_recursive_mutex = ASTRA_LIBC_LOCK_INITIALIZER;
struct __lock __lock___atexit_recursive_mutex = ASTRA_LIBC_LOCK_INITIALIZER;
struct __lock __lock___at_quick_exit_mutex = ASTRA_LIBC_LOCK_INITIALIZER;
struct __lock __lock___malloc_recursive_mutex = ASTRA_LIBC_LOCK_INITIALIZER;
struct __lock __lock___env_recursive_mutex = ASTRA_LIBC_LOCK_INITIALIZER;
struct __lock __lock___tz_mutex = ASTRA_LIBC_LOCK_INITIALIZER;
struct __lock __lock___arc4random_mutex = ASTRA_LIBC_LOCK_INITIALIZER;

void __retarget_lock_init(_LOCK_T *lock)
{
    struct __lock *created = malloc(sizeof(*created));

    if (created == NULL)
        abort();
    (void)pthread_mutex_init(&created->mutex, NULL);
    *lock = created;
}

void __retarget_lock_init_recursive(_LOCK_T *lock)
{
    struct __lock *created = malloc(sizeof(*created));
    pthread_mutexattr_t attr;

    if (created == NULL)
        abort();
    (void)pthread_mutexattr_init(&attr);
    (void)pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    (void)pthread_mutex_init(&created->mutex, &attr);
    *lock = created;
}

void __retarget_lock_close(_LOCK_T lock)
{
    free(lock);
}

void __retarget_lock_close_recursive(_LOCK_T lock)
{
    free(lock);
}

void __retarget_lock_acquire(_LOCK_T lock)
{
    if (pthread_mutex_lock(&lock->mutex) != 0)
        abort();
}

void __retarget_lock_acquire_recursive(_LOCK_T lock)
{
    __retarget_lock_acquire(lock);
}

void __retarget_lock_release(_LOCK_T lock)
{
    if (pthread_mutex_unlock(&lock->mutex) != 0)
        abort();
}

void __retarget_lock_release_recursive(_LOCK_T lock)
{
    __retarget_lock_release(lock);
}
#endif

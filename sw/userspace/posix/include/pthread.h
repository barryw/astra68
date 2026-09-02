#ifndef ASTRA_PTHREAD_H
#define ASTRA_PTHREAD_H

#include <stddef.h>
#include <stdint.h>
#include <sched.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t pthread_t;
typedef uintptr_t pthread_key_t;

typedef struct pthread_attr_t {
    int detach_state;
} pthread_attr_t;

typedef struct pthread_mutexattr_t {
    int type;
} pthread_mutexattr_t;

typedef struct pthread_condattr_t {
    int clock_id;
} pthread_condattr_t;

typedef struct pthread_mutex_t {
    volatile uint32_t state;
    uint32_t owner;
    uint32_t depth;
    uint8_t type;
    uint8_t reserved[3];
} pthread_mutex_t;

typedef struct pthread_cond_t {
    volatile uint32_t sequence;
    volatile uint32_t waiters;
    int clock_id;
} pthread_cond_t;

typedef struct pthread_rwlock_t {
    pthread_mutex_t mutex;
    pthread_cond_t readers;
    pthread_cond_t writers;
    uint32_t active_readers;
    uint32_t waiting_writers;
    uint32_t writer;
} pthread_rwlock_t;

typedef struct pthread_rwlockattr_t {
    int process_shared;
} pthread_rwlockattr_t;

typedef volatile uint32_t pthread_once_t;

#define PTHREAD_CREATE_JOINABLE 0
#define PTHREAD_CREATE_DETACHED 1
#define PTHREAD_MUTEX_NORMAL 0
#define PTHREAD_MUTEX_ERRORCHECK 1
#define PTHREAD_MUTEX_RECURSIVE 2
#define PTHREAD_MUTEX_DEFAULT PTHREAD_MUTEX_NORMAL
#define PTHREAD_PROCESS_PRIVATE 0
#define PTHREAD_PROCESS_SHARED 1
#define PTHREAD_MUTEX_INITIALIZER {0u, 0u, 0u, PTHREAD_MUTEX_NORMAL, {0u, 0u, 0u}}
#define PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP \
    {0u, 0u, 0u, PTHREAD_MUTEX_RECURSIVE, {0u, 0u, 0u}}
#define PTHREAD_COND_INITIALIZER {0u, 0u, CLOCK_REALTIME}
#define PTHREAD_RWLOCK_INITIALIZER \
    {PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, \
     PTHREAD_COND_INITIALIZER, 0u, 0u, 0u}
#define PTHREAD_ONCE_INIT 0u
#define PTHREAD_DESTRUCTOR_ITERATIONS 4

int pthread_attr_init(pthread_attr_t *attr);
int pthread_attr_destroy(pthread_attr_t *attr);
int pthread_attr_getdetachstate(const pthread_attr_t *attr, int *state);
int pthread_attr_setdetachstate(pthread_attr_t *attr, int state);
int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                   void *(*start)(void *), void *argument);
int pthread_join(pthread_t thread, void **result);
int pthread_detach(pthread_t thread);
pthread_t pthread_self(void);
int pthread_equal(pthread_t left, pthread_t right);
void pthread_exit(void *result) __attribute__((noreturn));

int pthread_mutexattr_init(pthread_mutexattr_t *attr);
int pthread_mutexattr_destroy(pthread_mutexattr_t *attr);
int pthread_mutexattr_gettype(const pthread_mutexattr_t *attr, int *type);
int pthread_mutexattr_settype(pthread_mutexattr_t *attr, int type);
int pthread_mutex_init(pthread_mutex_t *mutex,
                       const pthread_mutexattr_t *attr);
int pthread_mutex_destroy(pthread_mutex_t *mutex);
int pthread_mutex_lock(pthread_mutex_t *mutex);
int pthread_mutex_trylock(pthread_mutex_t *mutex);
int pthread_mutex_unlock(pthread_mutex_t *mutex);

int pthread_condattr_init(pthread_condattr_t *attr);
int pthread_condattr_destroy(pthread_condattr_t *attr);
int pthread_condattr_getclock(const pthread_condattr_t *attr, int *clock_id);
int pthread_condattr_setclock(pthread_condattr_t *attr, int clock_id);
int pthread_cond_init(pthread_cond_t *condition,
                      const pthread_condattr_t *attr);
int pthread_cond_destroy(pthread_cond_t *condition);
int pthread_cond_wait(pthread_cond_t *condition, pthread_mutex_t *mutex);
int pthread_cond_timedwait(pthread_cond_t *condition, pthread_mutex_t *mutex,
                           const struct timespec *abstime);
int pthread_cond_signal(pthread_cond_t *condition);
int pthread_cond_broadcast(pthread_cond_t *condition);

int pthread_rwlockattr_init(pthread_rwlockattr_t *attr);
int pthread_rwlockattr_destroy(pthread_rwlockattr_t *attr);
int pthread_rwlockattr_getpshared(const pthread_rwlockattr_t *attr,
                                 int *process_shared);
int pthread_rwlockattr_setpshared(pthread_rwlockattr_t *attr,
                                 int process_shared);
int pthread_rwlock_init(pthread_rwlock_t *rwlock,
                        const pthread_rwlockattr_t *attr);
int pthread_rwlock_destroy(pthread_rwlock_t *rwlock);
int pthread_rwlock_rdlock(pthread_rwlock_t *rwlock);
int pthread_rwlock_tryrdlock(pthread_rwlock_t *rwlock);
int pthread_rwlock_wrlock(pthread_rwlock_t *rwlock);
int pthread_rwlock_trywrlock(pthread_rwlock_t *rwlock);
int pthread_rwlock_unlock(pthread_rwlock_t *rwlock);

int pthread_once(pthread_once_t *once, void (*initialize)(void));
int pthread_key_create(pthread_key_t *key, void (*destructor)(void *));
int pthread_key_delete(pthread_key_t key);
int pthread_setspecific(pthread_key_t key, const void *value);
void *pthread_getspecific(pthread_key_t key);

#ifdef __cplusplus
}
#endif

#endif

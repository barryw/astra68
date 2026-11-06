#include <astra/runtime.h>

#include <assert.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

#define MOCK_MUTEX_MAX 128u

typedef struct MockMutex {
    volatile uint32_t *word;
    pthread_mutex_t lock;
} MockMutex;

static MockMutex mock_mutexes[MOCK_MUTEX_MAX];
static pthread_mutex_t mock_mutex_registry = PTHREAD_MUTEX_INITIALIZER;

static pthread_mutex_t *mock_mutex_for(volatile uint32_t *word)
{
    pthread_mutex_t *result = NULL;

    /* Host pointers do not fit the native 32-bit syscall argument. */
    assert(word != NULL && ((uintptr_t)word & 3u) == 0u);
    assert(pthread_mutex_lock(&mock_mutex_registry) == 0);
    for (uint32_t index = 0u; index < MOCK_MUTEX_MAX; ++index) {
        if (mock_mutexes[index].word == word) {
            result = &mock_mutexes[index].lock;
            break;
        }
        if (mock_mutexes[index].word == NULL) {
            assert(pthread_mutex_init(&mock_mutexes[index].lock, NULL) == 0);
            mock_mutexes[index].word = word;
            result = &mock_mutexes[index].lock;
            break;
        }
    }
    assert(pthread_mutex_unlock(&mock_mutex_registry) == 0);
    assert(result != NULL);
    return result;
}

uint32_t astra_vfs_test_mutex_lock(volatile uint32_t *word)
{
    assert(pthread_mutex_lock(mock_mutex_for(word)) == 0);
    __atomic_store_n(word, 1u, __ATOMIC_RELEASE);
    return ASTRA_SYSCALL_OK;
}

uint32_t astra_vfs_test_mutex_unlock(volatile uint32_t *word)
{
    __atomic_store_n(word, 0u, __ATOMIC_RELEASE);
    assert(pthread_mutex_unlock(mock_mutex_for(word)) == 0);
    return ASTRA_SYSCALL_OK;
}

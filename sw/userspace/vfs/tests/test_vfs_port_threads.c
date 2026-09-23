#include <astra/runtime.h>
#include <astra/vfs_port_transport.h>

#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static _Thread_local uint32_t current_thread;
static uint8_t live[64];
static pthread_mutex_t gate_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t gate_changed = PTHREAD_COND_INITIALIZER;
static uint32_t gate_arrived;
static _Alignas(4) volatile uint32_t contention_lock;
static pthread_mutex_t futex_gate = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t futex_changed = PTHREAD_COND_INITIALIZER;
static uint32_t futex_waiters;
static uint32_t contention_acquired;

uint32_t astra_log_failure(const char *operation, uint32_t status)
{
    (void)operation;
    return status;
}

uint32_t astra_query_abi(uint32_t *abi, uint32_t *process, uint32_t *thread)
{
    if (abi != NULL)
        *abi = 1u;
    if (process != NULL)
        *process = 1u;
    if (thread != NULL)
        *thread = current_thread;
    return ASTRA_SYSCALL_OK;
}

uint32_t astra_wait_one(uint32_t handle, uint64_t deadline, uint32_t *detail)
{
    (void)deadline;
    (void)detail;
    return handle < sizeof(live) && live[handle] != 0u ?
        ASTRA_SYSCALL_WOULD_BLOCK : ASTRA_SYSCALL_CLOSED;
}

uint32_t astra_futex_wait(volatile uint32_t *address, uint32_t expected,
                          uint64_t deadline)
{
    (void)deadline;
    if (address == &contention_lock) {
        assert(pthread_mutex_lock(&futex_gate) == 0);
        if (__atomic_load_n(address, __ATOMIC_ACQUIRE) != expected) {
            assert(pthread_mutex_unlock(&futex_gate) == 0);
            return ASTRA_SYSCALL_WOULD_BLOCK;
        }
        ++futex_waiters;
        assert(pthread_cond_broadcast(&futex_changed) == 0);
        while (__atomic_load_n(address, __ATOMIC_ACQUIRE) == expected)
            assert(pthread_cond_wait(&futex_changed, &futex_gate) == 0);
        assert(pthread_mutex_unlock(&futex_gate) == 0);
        return ASTRA_SYSCALL_OK;
    }
    return ASTRA_SYSCALL_WOULD_BLOCK;
}

uint32_t astra_futex_wake(volatile uint32_t *address, uint32_t count,
                          uint32_t *woken)
{
    (void)count;
    if (address == &contention_lock) {
        assert(pthread_mutex_lock(&futex_gate) == 0);
        assert(pthread_cond_broadcast(&futex_changed) == 0);
        assert(pthread_mutex_unlock(&futex_gate) == 0);
    }
    if (woken != NULL)
        *woken = 0u;
    return ASTRA_SYSCALL_OK;
}

uint32_t astra_rt_area_unmap(void *address)
{
    (void)address;
    return ASTRA_SYSCALL_OK;
}

uint32_t astra_close(uint32_t handle)
{
    (void)handle;
    return ASTRA_SYSCALL_OK;
}

void astra_assert_failed(const char *file, unsigned int line,
                         const char *expression)
{
    fprintf(stderr, "%s:%u: %s\n", file, line, expression);
    abort();
}

static void *contended_state_lock(void *unused)
{
    (void)unused;
    assert(astra_vfs_state_lock_acquire((void *)&contention_lock));
    contention_acquired = 1u;
    astra_vfs_state_lock_release((void *)&contention_lock);
    return NULL;
}

typedef struct ThreadCall {
    AstraVfsClient *client;
    uint32_t thread;
    uint32_t value;
    AstraVfsCallState *state;
} ThreadCall;

static void *call_thread(void *context)
{
    ThreadCall *call = context;

    current_thread = call->thread;
    live[current_thread] = 1u;
    call->state = astra_vfs_port_call_acquire(call->client);
    call->state->request.file = call->value;
    assert(pthread_mutex_lock(&gate_lock) == 0);
    ++gate_arrived;
    assert(pthread_cond_broadcast(&gate_changed) == 0);
    while (gate_arrived != 2u)
        assert(pthread_cond_wait(&gate_changed, &gate_lock) == 0);
    assert(pthread_mutex_unlock(&gate_lock) == 0);
    assert(call->state->request.file == call->value);
    return NULL;
}

int main(void)
{
    volatile uint32_t state_lock = 0u;
    volatile uint32_t sequence = 0u;
    AstraVfsClient client = {0};
    AstraVfsPortThreadState states[ASTRA_PROCESS_THREAD_SLOT_COUNT];
    ThreadCall first = {.client = &client, .thread = 1u, .value = 0x1111u};
    ThreadCall second = {.client = &client, .thread = 2u, .value = 0x2222u};
    pthread_t first_thread;
    pthread_t second_thread;
    pthread_t contender;

    assert(astra_vfs_state_lock_acquire((void *)&state_lock));
    assert(state_lock == 1u);
    assert(astra_vfs_state_futex_wait((void *)&state_lock, &sequence, 0u));
    assert(state_lock == 1u);
    astra_vfs_state_lock_release((void *)&state_lock);
    assert(state_lock == 0u);
    assert(!astra_vfs_state_lock_acquire(NULL));
    assert(!astra_vfs_state_futex_wait(NULL, &sequence, 0u));
    assert(!astra_vfs_state_futex_wait((void *)&state_lock, NULL, 0u));
    assert(astra_mutex_lock(
               (volatile uint32_t *)((uintptr_t)&state_lock + 2u)) ==
           ASTRA_SYSCALL_INVALID_ARGUMENT);
    assert(astra_mutex_unlock(
               (volatile uint32_t *)((uintptr_t)&state_lock + 2u)) ==
           ASTRA_SYSCALL_INVALID_ARGUMENT);
    assert(astra_vfs_state_lock_acquire((void *)&contention_lock));
    assert(pthread_create(&contender, NULL, contended_state_lock, NULL) == 0);
    assert(pthread_mutex_lock(&futex_gate) == 0);
    while (futex_waiters == 0u)
        assert(pthread_cond_wait(&futex_changed, &futex_gate) == 0);
    assert(pthread_mutex_unlock(&futex_gate) == 0);
    assert(contention_lock == 2u && contention_acquired == 0u);
    astra_vfs_state_lock_release((void *)&contention_lock);
    assert(pthread_join(contender, NULL) == 0);
    assert(contention_acquired == 1u && contention_lock == 0u);

    assert(astra_vfs_port_set_thread_storage(
        &client, states, ASTRA_PROCESS_THREAD_SLOT_COUNT));
    assert(pthread_create(&first_thread, NULL, call_thread, &first) == 0);
    assert(pthread_create(&second_thread, NULL, call_thread, &second) == 0);
    assert(pthread_join(first_thread, NULL) == 0);
    assert(pthread_join(second_thread, NULL) == 0);
    assert(first.state != second.state);

    for (uint32_t thread = 3u;
         thread <= ASTRA_PROCESS_THREAD_SLOT_COUNT; ++thread) {
        current_thread = thread;
        live[thread] = 1u;
        assert(astra_vfs_port_call_acquire(&client) != NULL);
    }
    live[1] = 0u;
    current_thread = ASTRA_PROCESS_THREAD_SLOT_COUNT + 1u;
    live[current_thread] = 1u;
    assert(astra_vfs_port_call_acquire(&client) == first.state);
    puts("VFS embedded per-thread state tests passed");
    return 0;
}

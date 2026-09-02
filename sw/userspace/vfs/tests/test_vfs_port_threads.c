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
    (void)address;
    (void)expected;
    (void)deadline;
    return ASTRA_SYSCALL_WOULD_BLOCK;
}

uint32_t astra_futex_wake(volatile uint32_t *address, uint32_t count,
                          uint32_t *woken)
{
    (void)address;
    (void)count;
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
    AstraVfsClient client = {0};
    AstraVfsPortThreadState states[ASTRA_PROCESS_THREAD_COUNT_MAX];
    ThreadCall first = {.client = &client, .thread = 1u, .value = 0x1111u};
    ThreadCall second = {.client = &client, .thread = 2u, .value = 0x2222u};
    pthread_t first_thread;
    pthread_t second_thread;

    assert(astra_vfs_port_set_thread_storage(
        &client, states, ASTRA_PROCESS_THREAD_COUNT_MAX));
    assert(pthread_create(&first_thread, NULL, call_thread, &first) == 0);
    assert(pthread_create(&second_thread, NULL, call_thread, &second) == 0);
    assert(pthread_join(first_thread, NULL) == 0);
    assert(pthread_join(second_thread, NULL) == 0);
    assert(first.state != second.state);

    for (uint32_t thread = 3u;
         thread <= ASTRA_PROCESS_THREAD_COUNT_MAX; ++thread) {
        current_thread = thread;
        live[thread] = 1u;
        assert(astra_vfs_port_call_acquire(&client) != NULL);
    }
    live[1] = 0u;
    current_thread = ASTRA_PROCESS_THREAD_COUNT_MAX + 1u;
    live[current_thread] = 1u;
    assert(astra_vfs_port_call_acquire(&client) == first.state);
    puts("VFS embedded per-thread state tests passed");
    return 0;
}

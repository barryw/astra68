#include <astra/runtime.h>
#include <astra/syscall.h>

#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <stddef.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TEST_ARENA_BYTES (64u * 4096u)

/* Deliberately declared here first: the test defines the allocator contract
 * before the public NDK surface and implementation are extended. */
void *astra_runtime_allocate_aligned(size_t alignment, size_t size);

static _Alignas(4096) uint8_t arena[TEST_ARENA_BYTES];
static uint32_t reserve_calls;
static uint32_t decommit_calls;
static uint32_t policy_calls;
static int permit_growth;
static atomic_uint futex_wait_calls;
static atomic_uint policy_entered;
static atomic_uint policy_active;
static atomic_uint policy_overlap;

uint32_t
astra_futex_wait(volatile uint32_t *address, uint32_t expected,
                 uint64_t deadline_ns)
{
    (void)deadline_ns;
    (void)atomic_fetch_add(&futex_wait_calls, 1u);
    while (__atomic_load_n(address, __ATOMIC_ACQUIRE) == expected)
        sched_yield();
    return ASTRA_SYSCALL_OK;
}

uint32_t
astra_futex_wake(volatile uint32_t *address, uint32_t count, uint32_t *woken)
{
    (void)address;
    if (woken != NULL)
        *woken = count != 0u ? 1u : 0u;
    return ASTRA_SYSCALL_OK;
}

uint32_t
astra_rt_private_reserve_largest(uint32_t minimum_byte_size,
                                 uint32_t permissions, void **address,
                                 uint32_t *mapped_span)
{
    assert(minimum_byte_size == 1u);
    assert(permissions ==
           (ASTRA_VM_PRIVATE_READ | ASTRA_VM_PRIVATE_WRITE));
    ++reserve_calls;
    *address = arena;
    *mapped_span = sizeof(arena);
    return ASTRA_SYSCALL_OK;
}

uint32_t
astra_rt_private_decommit(void *address, uint32_t byte_size,
                          uint32_t *released_pages)
{
    assert(address >= (void *)arena);
    assert((uint8_t *)address + byte_size <= arena + sizeof(arena));
    ++decommit_calls;
    *released_pages = byte_size / 4096u;
    return ASTRA_SYSCALL_OK;
}

static int
growth_policy(uint32_t current_bytes, uint32_t growth_bytes, void *context)
{
    assert(context == &permit_growth);
    assert(growth_bytes != 0u);
    (void)current_bytes;
    ++policy_calls;
    return permit_growth;
}

static int
serialized_growth_policy(uint32_t current_bytes, uint32_t growth_bytes,
                         void *context)
{
    unsigned call = atomic_fetch_add(&policy_entered, 1u);

    (void)current_bytes;
    (void)growth_bytes;
    (void)context;
    if (atomic_fetch_add(&policy_active, 1u) != 0u)
        atomic_store(&policy_overlap, 1u);
    if (call == 0u) {
        while (atomic_load(&futex_wait_calls) == 0u)
            sched_yield();
    }
    (void)atomic_fetch_sub(&policy_active, 1u);
    return 1;
}

static void *
grow_heap(void *context)
{
    void **result = context;

    *result = astra_runtime_sbrk(4096);
    return NULL;
}

int
main(void)
{
    uint8_t *small = astra_runtime_allocate(23u);
    uint8_t *zeroed = astra_runtime_callocate(17u, 3u);
    void *large = astra_runtime_allocate(5000u);
    void *aligned64 = astra_runtime_allocate_aligned(64u, 129u);
    void *aligned4096 = astra_runtime_allocate_aligned(4096u, 4097u);
    uint32_t before;
    pthread_t first_thread;
    pthread_t second_thread;
    void *first_growth = NULL;
    void *second_growth = NULL;

    assert(small != NULL);
    assert(zeroed != NULL);
    assert(large != NULL);
    assert(aligned64 != NULL && ((uintptr_t)aligned64 & 63u) == 0u);
    assert(aligned4096 != NULL &&
           ((uintptr_t)aligned4096 & 4095u) == 0u);
    assert(astra_runtime_allocation_size(aligned64) >= 129u);
    assert(astra_runtime_allocation_size(aligned4096) >= 4097u);
    assert(astra_runtime_allocate_aligned(0u, 1u) == NULL);
    assert(astra_runtime_allocate_aligned(3u, 1u) == NULL);
    assert(reserve_calls == 1u);
    assert(astra_runtime_allocation_size(small) >= 23u);
    for (uint32_t index = 0u; index < 51u; ++index)
        assert(zeroed[index] == 0u);
    (void)memset(small, 0x5au, 23u);
    small = astra_runtime_reallocate(small, 100u);
    assert(small != NULL);
    for (uint32_t index = 0u; index < 23u; ++index)
        assert(small[index] == 0x5au);

    before = astra_runtime_allocation_span();
    astra_runtime_set_growth_policy(growth_policy, &permit_growth);
    permit_growth = 0;
    assert(astra_runtime_sbrk(4096) == NULL);
    assert(astra_runtime_allocation_span() == before);
    permit_growth = 1;
    assert(astra_runtime_sbrk(4096) != NULL);
    assert(astra_runtime_allocation_span() == before + 4096u);
    assert(policy_calls == 2u);

    astra_runtime_set_growth_policy(serialized_growth_policy, NULL);
    assert(pthread_create(&first_thread, NULL, grow_heap, &first_growth) == 0);
    while (atomic_load(&policy_entered) == 0u)
        sched_yield();
    assert(pthread_create(&second_thread, NULL, grow_heap, &second_growth) == 0);
    assert(pthread_join(first_thread, NULL) == 0);
    assert(pthread_join(second_thread, NULL) == 0);
    assert(first_growth != NULL && second_growth != NULL);
    assert(atomic_load(&futex_wait_calls) != 0u);
    assert(atomic_load(&policy_overlap) == 0u);
    astra_runtime_set_growth_policy(NULL, NULL);

    astra_runtime_deallocate(small);
    astra_runtime_deallocate(zeroed);
    astra_runtime_deallocate(large);
    astra_runtime_deallocate(aligned64);
    astra_runtime_deallocate(aligned4096);
    assert(decommit_calls != 0u);
    assert(astra_runtime_callocate((size_t)-1, 2u) == NULL);
    puts("ASTRA RUNTIME ALLOCATOR PASS");
    return 0;
}

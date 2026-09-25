#include <assert.h>
#include <stdint.h>

static int test_cas(volatile uint32_t *state, uint32_t *expected,
                    uint32_t desired, int weak, int success, int failure);
static uint32_t __attribute__((unused))
test_exchange(volatile uint32_t *state, uint32_t desired, int order);

#define __atomic_compare_exchange_n test_cas
#define __atomic_exchange_n test_exchange
#include <astra/runtime.h>
#undef __atomic_compare_exchange_n
#undef __atomic_exchange_n

enum { NORMAL, LOCK_RETRY, UNLOCK_RETRY };
static int scenario;
static unsigned cas_calls;
static unsigned wait_calls;
static unsigned wake_calls;

static int test_cas(volatile uint32_t *state, uint32_t *expected,
                    uint32_t desired, int weak, int success, int failure)
{
    uint32_t actual;

    (void)weak;
    (void)success;
    (void)failure;
    ++cas_calls;
    if (scenario == LOCK_RETRY && cas_calls == 2u) {
        assert(*state == 1u && desired == 2u);
        *state = 0u; /* Owner released between load and CAS. */
    } else if (scenario == UNLOCK_RETRY && cas_calls == 1u) {
        assert(*state == 1u && desired == 0u);
        *state = 2u; /* Waiter marked the lock contended. */
    }
    actual = *state;
    if (actual == *expected) {
        *state = desired;
        return 1;
    }
    *expected = actual;
    return 0;
}

static uint32_t test_exchange(volatile uint32_t *state, uint32_t desired,
                              int order)
{
    uint32_t previous = *state;

    (void)order;
    /* Models the target compiler's stale return after an internal CAS retry.
     * A correct mutex implementation must not rely on this operation. */
    if (scenario == LOCK_RETRY && desired == 2u) {
        *state = 2u;
        return 1u;
    }
    if (scenario == UNLOCK_RETRY && desired == 0u) {
        *state = 0u;
        return 1u;
    }
    *state = desired;
    return previous;
}

uint32_t astra_futex_wait(volatile uint32_t *state, uint32_t expected,
                          uint64_t deadline)
{
    (void)state;
    (void)expected;
    (void)deadline;
    ++wait_calls;
    return ASTRA_SYSCALL_INVALID_ARGUMENT;
}

uint32_t astra_futex_wake(volatile uint32_t *state, uint32_t count,
                          uint32_t *woken)
{
    (void)state;
    assert(count == 1u && woken == NULL);
    ++wake_calls;
    return ASTRA_SYSCALL_OK;
}

int main(void)
{
    volatile uint32_t state = 0u;

    assert(astra_mutex_lock(&state) == ASTRA_SYSCALL_OK);
    assert(state == 1u && wait_calls == 0u);
    assert(astra_mutex_unlock(&state) == ASTRA_SYSCALL_OK);
    assert(state == 0u && wake_calls == 0u);

    scenario = LOCK_RETRY;
    state = 1u;
    cas_calls = 0u;
    assert(astra_mutex_lock(&state) == ASTRA_SYSCALL_OK);
    assert(state == 2u && cas_calls == 3u && wait_calls == 0u);

    scenario = UNLOCK_RETRY;
    state = 1u;
    cas_calls = 0u;
    assert(astra_mutex_unlock(&state) == ASTRA_SYSCALL_OK);
    assert(state == 0u && cas_calls == 2u && wake_calls == 1u);
    return 0;
}

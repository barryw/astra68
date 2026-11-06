#include <assert.h>
#include <stdint.h>

static int test_cas(volatile uint32_t *state, uint32_t *expected,
                    uint32_t desired, int weak, int success, int failure);

#define __atomic_compare_exchange_n test_cas
#include <astra/runtime.h>
#undef __atomic_compare_exchange_n

static volatile uint32_t *mutex_word;
static uint32_t mutex_calls;
static uint32_t cas_calls;
static uint32_t self_id = 42u;
static uint32_t wait_result = ASTRA_SYSCALL_OK;
static int race_before_cas;

static int test_cas(volatile uint32_t *state, uint32_t *expected,
                    uint32_t desired, int weak, int success, int failure)
{
    (void)weak;
    (void)success;
    (void)failure;
    ++cas_calls;
    if (race_before_cas) {
        *state = 17u;
        race_before_cas = 0;
    }
    if (*state != *expected) {
        *expected = *state;
        return 0;
    }
    *state = desired;
    return 1;
}

uint32_t astra_current_thread_id(uint32_t *id)
{
    *id = self_id;
    return ASTRA_SYSCALL_OK;
}

void astra_syscall5(uint32_t number, uint32_t argument0, uint32_t argument1,
                    uint32_t argument2, uint32_t argument3,
                    uint32_t argument4, AstraSyscallResult *result)
{
    (void)argument0;
    (void)argument1;
    (void)argument2;
    (void)argument3;
    (void)argument4;
    ++mutex_calls;
    result->status = wait_result;
    if (number == ASTRA_SYSCALL_MUTEX_WAIT) {
        if (wait_result == ASTRA_SYSCALL_OK)
            *mutex_word = self_id;
    } else {
        assert(number == ASTRA_SYSCALL_MUTEX_UNLOCK);
        assert(*mutex_word == self_id);
        if (wait_result == ASTRA_SYSCALL_OK)
            *mutex_word = 0u;
    }
}

int main(void)
{
    volatile uint32_t state = 0u;

    mutex_word = &state;
    assert(astra_mutex_trylock(&state) == ASTRA_SYSCALL_OK);
    assert(state == self_id && mutex_calls == 0u);
    assert(astra_mutex_trylock(&state) == ASTRA_SYSCALL_WOULD_BLOCK);
    assert(astra_mutex_unlock(&state) == ASTRA_SYSCALL_OK);
    assert(state == 0u && mutex_calls == 1u);

    state = 17u;
    assert(astra_mutex_lock(&state) == ASTRA_SYSCALL_OK);
    assert(state == self_id && mutex_calls == 2u);
    assert(astra_mutex_unlock(&state) == ASTRA_SYSCALL_OK);

    state = 0u;
    race_before_cas = 1;
    assert(astra_mutex_lock(&state) == ASTRA_SYSCALL_OK);
    assert(state == self_id && cas_calls >= 4u && mutex_calls == 4u);

    state = 17u;
    wait_result = ASTRA_SYSCALL_PEER_DEAD;
    assert(astra_mutex_lock(&state) == ASTRA_SYSCALL_PEER_DEAD);
    assert(state == 17u);
    assert(astra_mutex_trylock(NULL) == ASTRA_SYSCALL_INVALID_ARGUMENT);
    assert(astra_mutex_unlock(NULL) == ASTRA_SYSCALL_INVALID_ARGUMENT);
    return 0;
}

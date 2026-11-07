#include <astra/runtime.h>
#include <assert.h>

/* Host tests exercise native atomic locking without a sleeping thread. */
uint32_t astra_current_thread_id(uint32_t *id)
{
    /* Zero leaves single-threaded fixture locks uncontended across calls. */
    *id = 0u;
    return ASTRA_SYSCALL_OK;
}

void astra_syscall5(uint32_t number, uint32_t argument0, uint32_t argument1,
                    uint32_t argument2, uint32_t argument3,
                    uint32_t argument4, AstraSyscallResult *result)
{
    (void)argument0; (void)argument1; (void)argument2;
    (void)argument3; (void)argument4;
    assert(number == ASTRA_SYSCALL_MUTEX_UNLOCK);
    result->status = ASTRA_SYSCALL_OK;
}

uint32_t astra_futex_wait(volatile uint32_t *address, uint32_t expected,
                          uint64_t deadline)
{
    (void)address; (void)expected; (void)deadline;
    assert(!"unexpected mutex contention in single-threaded host test");
    return ASTRA_SYSCALL_UNSUPPORTED;
}

uint32_t astra_futex_wake(volatile uint32_t *address, uint32_t count,
                          uint32_t *woken)
{
    (void)address; (void)count; (void)woken;
    assert(!"unexpected mutex waiter in single-threaded host test");
    return ASTRA_SYSCALL_UNSUPPORTED;
}

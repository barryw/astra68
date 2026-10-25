#include <astra/runtime.h>
#include <assert.h>

/* Host tests exercise native atomic locking without a sleeping thread. */
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

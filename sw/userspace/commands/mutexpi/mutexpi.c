/* Disposable native mutex ownership and priority-inversion gate. */
#include <astra/program.h>
#include <astra/runtime.h>

#include <stdint.h>
#include <stdio.h>

ASTRA_PROGRAM("mutexpi", 1, 0, 0, "Barry Walker",
              "Copyright 2026 Barry Walker");

static volatile uint32_t mutex_word;
static volatile uint32_t release_owner;
static volatile uint32_t stop_hog;
static volatile uint32_t owner_result = 1u;
static uint32_t ready_event;

static void owner(uint32_t unused)
{
    (void)unused;
    if (astra_mutex_lock(&mutex_word) != ASTRA_SYSCALL_OK)
        return;
    if (astra_rt_signal(ready_event, 1u, NULL) != ASTRA_SYSCALL_OK)
        return;
    while (release_owner == 0u)
        __asm__ volatile("" ::: "memory");
    owner_result = astra_mutex_unlock(&mutex_word);
}

static void hog(uint32_t unused)
{
    uint64_t deadline = astra_clock_monotonic() + UINT64_C(2000000000);

    (void)unused;
    while (stop_hog == 0u && astra_clock_monotonic() < deadline)
        __asm__ volatile("" ::: "memory");
}

int main(int argc, char **argv)
{
    const uint32_t rights = ASTRA_RIGHT_READ | ASTRA_RIGHT_WAIT |
                            ASTRA_RIGHT_ADMINISTER;
    AstraThreadStart owner_start = {owner, 0u};
    AstraThreadStart hog_start = {hog, 0u};
    uint32_t owner_handle = 0u;
    uint32_t hog_handle = 0u;
    uint32_t thread_id;
    uint64_t started;
    uint64_t elapsed;
    uint32_t status;
    int passed;

    (void)argc;
    (void)argv;
    if (astra_rt_event_create(0u, ASTRA_RIGHT_WAIT | ASTRA_RIGHT_SIGNAL,
                              &ready_event) != ASTRA_SYSCALL_OK ||
        astra_rt_thread_create(&owner_start, 5u, rights,
                               &owner_handle, &thread_id) != ASTRA_SYSCALL_OK ||
        astra_wait_one(ready_event, ASTRA_DEADLINE_FOREVER, NULL) !=
            ASTRA_SYSCALL_OK ||
        astra_rt_thread_create(&hog_start, 12u, rights,
                               &hog_handle, &thread_id) != ASTRA_SYSCALL_OK) {
        puts("mutexpi: setup FAIL");
        return 1;
    }
    release_owner = 1u;
    started = astra_clock_monotonic();
    status = astra_mutex_lock(&mutex_word);
    elapsed = astra_clock_monotonic() - started;
    if (status == ASTRA_SYSCALL_OK)
        status = astra_mutex_unlock(&mutex_word);
    stop_hog = 1u;
    passed = status == ASTRA_SYSCALL_OK &&
             elapsed < UINT64_C(500000000) &&
             astra_wait_one(owner_handle,
                            astra_clock_monotonic() + UINT64_C(2000000000),
                            NULL) == ASTRA_SYSCALL_OK &&
             owner_result == ASTRA_SYSCALL_OK && mutex_word == 0u;
    (void)astra_wait_one(hog_handle,
                         astra_clock_monotonic() + UINT64_C(2000000000), NULL);
    (void)astra_close(hog_handle);
    (void)astra_close(owner_handle);
    (void)astra_close(ready_event);
    printf("mutexpi: %s elapsed_us=%lu status=%lu\n",
           passed ? "PASS" : "FAIL", (unsigned long)(elapsed / 1000u),
           (unsigned long)status);
    return passed ? 0 : 1;
}

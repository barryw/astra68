#define _POSIX_C_SOURCE 200809L

#include <astra/posix_descriptor.h>
#include <astra/runtime.h>
#include <astra/syscall.h>

#include <assert.h>
#include <errno.h>
#include <setjmp.h>
#include <signal.h>
#include <stdint.h>

static jmp_buf escaped;
static void (*trampoline)(int);
static uint32_t terminated_handle;
static uint32_t terminated_signal;
static uint32_t suspended_handle;
static uint32_t signalled_handle;
static uint32_t signalled_number;
static uint32_t signal_result = ASTRA_SYSCALL_OK;
static int handled;
static int returned;
static uint32_t configured_blocked;
static uint32_t sleep_flags;
static uint32_t sleep_mask;
static const AstraStartupInfo startup = {
    .magic = ASTRA_STARTUP_MAGIC,
    .abi_version = ASTRA_STARTUP_ABI_VERSION,
    .program_entry = 0x00100000u,
    .header_size = ASTRA_STARTUP_INFO_SIZE,
    .total_size = ASTRA_STARTUP_INFO_SIZE,
    .syscall_abi_version = ASTRA_SYSCALL_ABI_VERSION,
    .process_handle = 6u,
};

const AstraStartupInfo *
astra_posix_startup(void)
{
    return &startup;
}

uint32_t
astra_rt_signal_configure(void (*entry)(int), void *stack_top,
                          uint32_t blocked, uint32_t *pending,
                          uint32_t *previous)
{
    (void)stack_top;
    trampoline = entry;
    if (pending != NULL)
        *pending = 0u;
    if (previous != NULL)
        *previous = configured_blocked;
    configured_blocked = blocked;
    return ASTRA_SYSCALL_OK;
}

uint32_t
astra_rt_thread_sleep(uint64_t deadline, uint32_t flags,
                      uint32_t signal_mask, uint32_t *previous)
{
    assert(deadline == ASTRA_DEADLINE_FOREVER);
    sleep_flags = flags;
    sleep_mask = signal_mask;
    if (previous != NULL)
        *previous = configured_blocked;
    return ASTRA_SYSCALL_IO_ERROR;
}

uint32_t
astra_process_terminate(uint32_t handle, uint32_t signal_number)
{
    terminated_handle = handle;
    terminated_signal = signal_number;
    longjmp(escaped, 1);
}

uint32_t
astra_process_suspend(uint32_t handle)
{
    suspended_handle = handle;
    return ASTRA_SYSCALL_OK;
}

uint32_t
astra_process_signal(uint32_t handle, uint32_t signal_number)
{
    signalled_handle = handle;
    signalled_number = signal_number;
    return signal_result;
}

uint32_t
astra_rt_signal_return(void)
{
    returned = 1;
    longjmp(escaped, 2);
}

static void
handler(int signal_number)
{
    assert(signal_number == SIGUSR1);
    ++handled;
}

int
main(void)
{
    struct sigaction action = {0};
    sigset_t suspend_mask = (sigset_t)1u << SIGUSR1;
    sigset_t set;
    sigset_t other;
    sigset_t result;

    assert(sigemptyset(&set) == 0 && set == 0u);
    assert(sigisemptyset(&set) == 1);
    assert(sigaddset(&set, SIGUSR1) == 0);
    assert(sigismember(&set, SIGUSR1) == 1);
    assert(sigismember(&set, SIGUSR2) == 0);
    assert(sigdelset(&set, SIGUSR1) == 0 && set == 0u);
    assert(sigfillset(&set) == 0 && (set & 1u) == 0u);
    assert(sigismember(&set, SIGUSR2) == 1);

    other = (sigset_t)1u << SIGUSR1;
    assert(sigandset(&result, &set, &other) == 0 && result == other);
    assert(sigorset(&result, &other, &suspend_mask) == 0 && result == other);
    assert(signotset(&result, &other) == 0);
    assert((result & 1u) == 0u && sigismember(&result, SIGUSR1) == 0);
    assert(sigisemptyset(&result) == 0);

    errno = 0;
    assert(sigaddset(&set, 0) == -1 && errno == EINVAL);
    errno = 0;
    assert(sigdelset(&set, 32) == -1 && errno == EINVAL);
    errno = 0;
    assert(sigismember(NULL, SIGUSR1) == -1 && errno == EFAULT);
    errno = 0;
    assert(sigemptyset(NULL) == -1 && errno == EFAULT);
    errno = 0;
    assert(sigandset(&result, NULL, &other) == -1 && errno == EFAULT);

    assert(sigprocmask(SIG_SETMASK, NULL, NULL) == 0);
    assert(trampoline != NULL);
    if (setjmp(escaped) == 0)
        trampoline(SIGTERM);
    assert(terminated_handle == startup.process_handle);
    assert(terminated_signal == SIGTERM);

    if (setjmp(escaped) == 0)
        trampoline(SIGTSTP);
    assert(suspended_handle == startup.process_handle && returned == 1);

    returned = 0;
    if (setjmp(escaped) == 0)
        trampoline(SIGWINCH);
    assert(returned == 1);

    action.sa_handler = handler;
    assert(sigaction(SIGUSR1, &action, NULL) == 0);
    if (setjmp(escaped) == 0)
        trampoline(SIGUSR1);
    assert(handled == 1 && returned == 1);

    errno = 0;
    assert(sigsuspend(&suspend_mask) == -1 && errno == EIO);
    assert(sleep_flags == ASTRA_THREAD_SLEEP_REPLACE_SIGNAL_MASK);
    assert(sleep_mask == suspend_mask);
    assert(configured_blocked == 0u);

    errno = 0;
    assert(pause() == -1 && errno == EIO);
    assert(sleep_mask == 0u && configured_blocked == 0u);

    assert(raise(SIGUSR2) == 0);
    assert(signalled_handle == startup.process_handle);
    assert(signalled_number == SIGUSR2);
    errno = 0;
    assert(raise(0) == -1 && errno == EINVAL);
    signal_result = ASTRA_SYSCALL_ACCESS_DENIED;
    errno = 0;
    assert(raise(SIGUSR2) == -1 && errno == EPERM);
    return 0;
}

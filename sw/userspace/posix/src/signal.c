#define _POSIX_C_SOURCE 200809L

#include <astra/runtime.h>
#include <astra/syscall.h>

#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <unistd.h>

#define ASTRA_POSIX_SIGNAL_COUNT 32u
#define ASTRA_POSIX_SIGNAL_STACK_BYTES 4096u

static struct sigaction actions[ASTRA_POSIX_SIGNAL_COUNT];
static sigset_t blocked;
static _Alignas(16) unsigned char signal_stack[ASTRA_POSIX_SIGNAL_STACK_BYTES];

static void astra_posix_signal_trampoline(int signal_number);

static int
configure(sigset_t mask, sigset_t *pending, sigset_t *previous)
{
    uint32_t kernel_pending = 0u;
    uint32_t kernel_previous = 0u;

    if (astra_rt_signal_configure(
            astra_posix_signal_trampoline,
            signal_stack + sizeof(signal_stack), (uint32_t)mask,
            &kernel_pending, &kernel_previous) != ASTRA_SYSCALL_OK) {
        errno = EIO;
        return -1;
    }
    if (pending != NULL)
        *pending = (sigset_t)kernel_pending;
    if (previous != NULL)
        *previous = (sigset_t)kernel_previous;
    return 0;
}

static void
astra_posix_signal_trampoline(int signal_number)
{
    struct sigaction action;
    sigset_t saved;
    sigset_t handler_mask;

    if (signal_number <= 0 ||
        signal_number >= (int)ASTRA_POSIX_SIGNAL_COUNT)
        _exit(128);
    action = actions[signal_number];
    if (action.sa_handler == SIG_IGN)
        goto returned;
    if (action.sa_handler == SIG_DFL)
        _exit(128 + signal_number);
    if ((action.sa_flags & SA_RESETHAND) != 0)
        actions[signal_number].sa_handler = SIG_DFL;
    saved = blocked;
    handler_mask = blocked | action.sa_mask;
    if ((action.sa_flags & SA_NODEFER) == 0)
        handler_mask |= (sigset_t)1u << signal_number;
    blocked = handler_mask;
    if (configure(blocked, NULL, NULL) != 0)
        _exit(128 + signal_number);
    if ((action.sa_flags & SA_SIGINFO) != 0)
        action.sa_sigaction(signal_number, NULL, NULL);
    else
        action.sa_handler(signal_number);
    blocked = saved;
    if (configure(blocked, NULL, NULL) != 0)
        _exit(128 + signal_number);

returned:
    (void)astra_rt_signal_return();
    _exit(128 + signal_number);
}

int
sigaction(int signal_number, const struct sigaction *restrict action,
          struct sigaction *restrict previous)
{
    if (signal_number <= 0 ||
        signal_number >= (int)ASTRA_POSIX_SIGNAL_COUNT ||
        signal_number == SIGKILL || signal_number == SIGSTOP) {
        errno = EINVAL;
        return -1;
    }
    if (previous != NULL)
        *previous = actions[signal_number];
    if (action != NULL)
        actions[signal_number] = *action;
    return configure(blocked, NULL, NULL);
}

int
sigpending(sigset_t *set)
{
    if (set == NULL) {
        errno = EFAULT;
        return -1;
    }
    return configure(blocked, set, NULL);
}

int
sigprocmask(int how, const sigset_t *restrict set,
            sigset_t *restrict previous)
{
    sigset_t replacement = blocked;

    if (set != NULL) {
        switch (how) {
        case SIG_BLOCK:
            replacement |= *set;
            break;
        case SIG_UNBLOCK:
            replacement &= ~*set;
            break;
        case SIG_SETMASK:
            replacement = *set;
            break;
        default:
            errno = EINVAL;
            return -1;
        }
    }
    replacement &= ~(((sigset_t)1u << SIGKILL) |
                     ((sigset_t)1u << SIGSTOP));
    if (configure(replacement, NULL, previous) != 0)
        return -1;
    blocked = replacement;
    return 0;
}

_sig_func_ptr
signal(int signal_number, _sig_func_ptr handler)
{
    struct sigaction action = {0};
    struct sigaction previous;

    action.sa_handler = handler;
    if (sigaction(signal_number, &action, &previous) != 0)
        return SIG_ERR;
    return previous.sa_handler;
}

#ifndef ASTRA_POSIX_TEST_SIGNAL_H
#define ASTRA_POSIX_TEST_SIGNAL_H

#include <stdint.h>

typedef uint32_t sigset_t;
typedef int sig_atomic_t;
typedef void (*_sig_func_ptr)(int);

#define SIG_DFL ((_sig_func_ptr)0)
#define SIG_IGN ((_sig_func_ptr)1)
#define SIG_ERR ((_sig_func_ptr)-1)

#define SIGINT 2
#define SIGQUIT 3
#define SIGKILL 9
#define SIGUSR1 10
#define SIGALRM 14
#define SIGURG 16
#define SIGSTOP 17
#define SIGTSTP 18
#define SIGCONT 19
#define SIGCHLD 20
#define SIGTTIN 21
#define SIGTTOU 22
#define SIGWINCH 28
#define SIGTERM 15

#define SIG_BLOCK 0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2

#define SA_NODEFER (1u << 0)
#define SA_RESETHAND (1u << 1)
#define SA_SIGINFO (1u << 2)

struct sigaction {
    union {
        _sig_func_ptr handler;
        void (*action)(int, void *, void *);
    } callback;
    sigset_t sa_mask;
    uint32_t sa_flags;
};

#define sa_handler callback.handler
#define sa_sigaction callback.action

int sigaction(int signal_number, const struct sigaction *restrict action,
              struct sigaction *restrict previous);
int sigprocmask(int how, const sigset_t *restrict set,
                sigset_t *restrict previous);
int sigsuspend(const sigset_t *mask);
_sig_func_ptr signal(int signal_number, _sig_func_ptr handler);

#endif

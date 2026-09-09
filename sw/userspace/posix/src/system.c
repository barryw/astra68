/* POSIX system(), using zsh through the ordinary fork/exec/wait path. */

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

int system(const char *command)
{
    static const char shell[] = "/commands/zsh";
    struct sigaction ignore = {0};
    struct sigaction old_interrupt;
    struct sigaction old_quit;
    sigset_t block;
    sigset_t old_mask;
    pid_t child;
    pid_t waited;
    int wait_status = -1;
    int result;
    int saved_errno;

    if (command == NULL)
        return access(shell, X_OK) == 0;
    ignore.sa_handler = SIG_IGN;
    if (sigemptyset(&ignore.sa_mask) != 0 ||
        sigaction(SIGINT, &ignore, &old_interrupt) != 0)
        return -1;
    if (sigaction(SIGQUIT, &ignore, &old_quit) != 0) {
        saved_errno = errno;
        (void)sigaction(SIGINT, &old_interrupt, NULL);
        errno = saved_errno;
        return -1;
    }
    if (sigemptyset(&block) != 0 || sigaddset(&block, SIGCHLD) != 0 ||
        sigprocmask(SIG_BLOCK, &block, &old_mask) != 0) {
        saved_errno = errno;
        (void)sigaction(SIGQUIT, &old_quit, NULL);
        (void)sigaction(SIGINT, &old_interrupt, NULL);
        errno = saved_errno;
        return -1;
    }
    child = fork();
    if (child == 0) {
        char *const argv[] = {(char *)"zsh", (char *)"-c",
                              (char *)command, NULL};

        (void)sigaction(SIGINT, &old_interrupt, NULL);
        (void)sigaction(SIGQUIT, &old_quit, NULL);
        (void)sigprocmask(SIG_SETMASK, &old_mask, NULL);
        (void)execve(shell, argv, environ);
        _exit(127);
    }
    if (child < 0) {
        result = -1;
    } else {
        do {
            waited = waitpid(child, &wait_status, 0);
        } while (waited < 0 && errno == EINTR);
        result = waited < 0 ? -1 : wait_status;
    }
    saved_errno = errno;
    (void)sigprocmask(SIG_SETMASK, &old_mask, NULL);
    (void)sigaction(SIGQUIT, &old_quit, NULL);
    (void)sigaction(SIGINT, &old_interrupt, NULL);
    errno = saved_errno;
    return result;
}

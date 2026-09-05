#include <astra/runtime.h>
#include <astra/posix_descriptor.h>
#include <astra/status.h>
#include <astra/syscall.h>

#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct AstraPosixChild {
    struct AstraPosixChild *next;
    uint32_t handle;
    pid_t pid;
} AstraPosixChild;

static AstraPosixChild *children;

static void
discard_inherited_children(void)
{
    while (children != NULL) {
        AstraPosixChild *child = children;

        children = child->next;
        free(child);
    }
}

pid_t
fork(void)
{
    AstraPosixChild *child;
    uint32_t handle;
    uint32_t process_id;
    uint32_t result;

    if (!astra_posix_file_fork_ready()) {
        errno = ENOTSUP;
        return (pid_t)-1;
    }
    child = malloc(sizeof(*child));
    if (child == NULL) {
        errno = ENOMEM;
        return (pid_t)-1;
    }
    result = astra_process_clone(&handle, &process_id);
    if (result != ASTRA_SYSCALL_OK) {
        (void)astra_log_failure("fork clone", result);
        free(child);
        errno = result == ASTRA_SYSCALL_OUT_OF_MEMORY ||
                        result == ASTRA_SYSCALL_RESOURCE_LIMIT ?
            ENOMEM : EIO;
        return (pid_t)-1;
    }
    if (process_id == 0u) {
        discard_inherited_children();
        free(child);
        if (astra_posix_file_after_fork_child() != 0) {
            static const char message[] =
                "fork: child filesystem reinitialization failed\n";

            (void)astra_log_failure("fork filesystem reinitialization",
                                    (uint32_t)errno);
            ssize_t ignored = write(STDERR_FILENO, message,
                                    sizeof(message) - 1u);
            (void)ignored;
            _exit(127);
        }
        astra_posix_socket_after_fork_child();
        return (pid_t)0;
    }
    if (handle == 0u) {
        free(child);
        errno = EIO;
        return (pid_t)-1;
    }
    child->handle = handle;
    child->pid = (pid_t)process_id;
    child->next = children;
    children = child;
    return child->pid;
}

static int
posix_wait_status(uint32_t astra_status, uint32_t wait_result)
{
    if (wait_result == ASTRA_SYSCALL_PEER_DEAD ||
        (astra_status & ASTRA_STATUS_VERDICT) != 0u)
        return SIGSEGV;
    return (int)((astra_status & 0xffu) << 8);
}

static pid_t
reap_child(AstraPosixChild *child, AstraPosixChild *previous,
           int *status, uint32_t astra_status, uint32_t wait_result)
{
    pid_t pid = child->pid;

    if (astra_close(child->handle) != ASTRA_SYSCALL_OK) {
        errno = EIO;
        return (pid_t)-1;
    }
    if (previous == NULL)
        children = child->next;
    else
        previous->next = child->next;
    free(child);
    if (status != NULL)
        *status = posix_wait_status(astra_status, wait_result);
    return pid;
}

pid_t
waitpid(pid_t pid, int *status, int options)
{
    if ((options & ~WNOHANG) != 0 || pid == 0 || pid < (pid_t)-1) {
        errno = EINVAL;
        return (pid_t)-1;
    }
    for (;;) {
        AstraPosixChild *previous = NULL;
        AstraPosixChild *child = children;
        int found = 0;

        while (child != NULL) {
            uint32_t astra_status = 0u;
            uint32_t wait_result;

            if (pid != (pid_t)-1 && child->pid != pid) {
                previous = child;
                child = child->next;
                continue;
            }
            found = 1;
            wait_result = astra_process_wait(
                child->handle,
                pid == (pid_t)-1 || (options & WNOHANG) != 0 ?
                    0u : ASTRA_DEADLINE_FOREVER,
                &astra_status);
            if (wait_result == ASTRA_SYSCALL_OK ||
                wait_result == ASTRA_SYSCALL_PEER_DEAD)
                return reap_child(child, previous, status, astra_status,
                                  wait_result);
            if (wait_result != ASTRA_SYSCALL_TIMED_OUT) {
                errno = wait_result == ASTRA_SYSCALL_CANCELLED ? EINTR : EIO;
                return (pid_t)-1;
            }
            previous = child;
            child = child->next;
        }
        if (!found) {
            errno = ECHILD;
            return (pid_t)-1;
        }
        if ((options & WNOHANG) != 0)
            return (pid_t)0;
        if (pid != (pid_t)-1)
            continue;
        if (astra_yield() != ASTRA_SYSCALL_OK) {
            errno = EIO;
            return (pid_t)-1;
        }
    }
}

pid_t
wait(int *status)
{
    return waitpid((pid_t)-1, status, 0);
}

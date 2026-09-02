#include <astra/status.h>
#include <astra/syscall.h>

#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>

static uint32_t clone_handle = 7u;
static uint32_t clone_pid = 123u;
static uint32_t clone_result = ASTRA_SYSCALL_OK;
static uint32_t logged_status;
static uint32_t wait_result = ASTRA_SYSCALL_TIMED_OUT;
static uint32_t child_status;
static uint32_t closed_handle;
static uint32_t after_fork_child_calls;
static uint32_t file_after_fork_child_calls;

int
astra_posix_file_fork_ready(void)
{
    return 1;
}

int
astra_posix_file_after_fork_child(void)
{
    ++file_after_fork_child_calls;
    return 0;
}

void
astra_posix_socket_after_fork_child(void)
{
    ++after_fork_child_calls;
}

uint32_t
astra_process_clone(uint32_t *handle, uint32_t *pid)
{
    *handle = clone_handle;
    *pid = clone_pid;
    return clone_result;
}

uint32_t
astra_log_failure(const char *operation, uint32_t status)
{
    assert(operation != NULL);
    logged_status = status;
    return ASTRA_SYSCALL_OK;
}

uint32_t
astra_process_wait(uint32_t handle, uint64_t deadline, uint32_t *status)
{
    (void)handle;
    (void)deadline;
    *status = child_status;
    return wait_result;
}

uint32_t
astra_close(uint32_t handle)
{
    closed_handle = handle;
    return ASTRA_SYSCALL_OK;
}

uint32_t
astra_yield(void)
{
    return ASTRA_SYSCALL_OK;
}

int
main(void)
{
    int status;
    pid_t pid;

    assert(fork() == (pid_t)123);
    assert(waitpid((pid_t)999, &status, WNOHANG) == (pid_t)-1);
    assert(errno == ECHILD);
    assert(waitpid((pid_t)123, &status, WNOHANG) == 0);
    wait_result = ASTRA_SYSCALL_OK;
    child_status = 42u;
    assert(waitpid((pid_t)123, &status, 0) == (pid_t)123);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 42);
    assert(closed_handle == 7u);

    clone_pid = 124u;
    clone_handle = 8u;
    assert(fork() == (pid_t)124);
    wait_result = ASTRA_SYSCALL_PEER_DEAD;
    child_status = ASTRA_STATUS_FAULTED;
    pid = wait(&status);
    assert(pid == (pid_t)124 && WIFSIGNALED(status));
    assert(WTERMSIG(status) == SIGSEGV && closed_handle == 8u);

    clone_pid = 0u;
    clone_handle = 0u;
    assert(fork() == 0);
    assert(file_after_fork_child_calls == 1u);
    assert(after_fork_child_calls == 1u);
    assert(wait(NULL) == (pid_t)-1 && errno == ECHILD);
    assert(waitpid((pid_t)-2, NULL, 0) == (pid_t)-1 && errno == EINVAL);

    clone_result = ASTRA_SYSCALL_RESOURCE_LIMIT;
    assert(fork() == (pid_t)-1 && errno == ENOMEM);
    assert(logged_status == ASTRA_SYSCALL_RESOURCE_LIMIT);
    puts("ASTRA POSIX PROCESS PASS");
    return 0;
}

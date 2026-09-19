#define _GNU_SOURCE 1

#include <astra/status.h>
#include <astra/syscall.h>
#include <astra/runtime.h>

#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

static uint32_t clone_handle = 7u;
static uint32_t clone_pid = 123u;
static uint32_t clone_result = ASTRA_SYSCALL_OK;
static uint32_t clone_calls;
static int file_fork_ready = 1;
static uint32_t obsolete_descriptor_gate_calls;
static uint32_t logged_status;
static uint32_t wait_result = ASTRA_SYSCALL_TIMED_OUT;
static uint32_t child_status;
static uint32_t closed_handle;
static uint32_t after_fork_child_calls;
static uint32_t file_after_fork_child_calls;
static uint32_t registered_handle;
static uint32_t registered_pid;
static uint32_t resumed_handle;
static uint32_t registration_order;
static uint32_t resume_order;
static uint32_t operation_order;
static int32_t signalled_selector;
static int32_t signalled_group;
static uint32_t signalled_number;
AstraPosixProcessReply process_reply;
static const AstraStartupInfo startup = {
    .process_handle = 5u,
};
static const AstraStartupCapability posix_capability = {
    .handle = 6u,
};

const AstraStartupInfo *
astra_posix_startup(void)
{
    return &startup;
}

const AstraStartupCapability *
astra_startup_capability(const AstraStartupInfo *startup, const char *name)
{
    assert(startup != NULL && name != NULL);
    return &posix_capability;
}

uint32_t
astra_posix_process_register(uint32_t service, uint32_t process_handle,
                             uint32_t process_id, uint32_t flags)
{
    assert(service == 6u && flags == 0u);
    registered_handle = process_handle;
    registered_pid = process_id;
    registration_order = ++operation_order;
    return ASTRA_STATUS_OK;
}

uint32_t
astra_posix_process_signal(uint32_t service, int32_t selector,
                           uint32_t signal_number)
{
    assert(service == 6u);
    signalled_selector = selector;
    signalled_number = signal_number;
    return ASTRA_STATUS_OK;
}

uint32_t
astra_posix_process_signal_group(uint32_t service, int32_t group,
                                 uint32_t signal_number)
{
    assert(service == 6u);
    signalled_group = group;
    signalled_number = signal_number;
    return ASTRA_STATUS_OK;
}

uint32_t
astra_posix_process_setpgid(uint32_t service, int32_t process, int32_t group)
{
    assert(service == 6u);
    process_reply.process = process;
    process_reply.group = group;
    return ASTRA_STATUS_OK;
}

uint32_t
astra_posix_process_setsid(uint32_t service, AstraPosixProcessReply *reply)
{
    assert(service == 6u && reply != NULL);
    *reply = process_reply;
    return ASTRA_STATUS_OK;
}

uint32_t
astra_posix_process_query(uint32_t service, int32_t process,
                          AstraPosixProcessReply *reply)
{
    assert(service == 6u && reply != NULL);
    *reply = process_reply;
    if (process != 0)
        reply->process = process;
    return ASTRA_STATUS_OK;
}

uint32_t
astra_process_terminate(uint32_t handle, uint32_t reason)
{
    (void)handle;
    (void)reason;
    return ASTRA_SYSCALL_OK;
}

uint32_t
astra_process_resume(uint32_t handle)
{
    resumed_handle = handle;
    resume_order = ++operation_order;
    return ASTRA_SYSCALL_OK;
}
int
astra_posix_file_fork_ready(void)
{
    return file_fork_ready;
}

int
astra_posix_descriptor_fork_ready(void)
{
    ++obsolete_descriptor_gate_calls;
    return 0;
}

int
astra_posix_file_after_fork_child(void)
{
    ++file_after_fork_child_calls;
    return 0;
}

int
astra_posix_socket_after_fork_child(void)
{
    ++after_fork_child_calls;
    return 0;
}

uint32_t
astra_process_clone(uint32_t *handle, uint32_t *pid)
{
    ++clone_calls;
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
astra_process_info(uint32_t handle, AstraProcessInfo *info)
{
    assert(info != NULL && info->size == sizeof(*info));
    info->runtime_ns = handle == 5u ? UINT64_C(1234567890) :
                                     (uint64_t)handle * UINT64_C(1000000);
    info->peak_resident_frames = handle == 5u ? 12u : handle + 1u;
    return ASTRA_SYSCALL_OK;
}

uint32_t
astra_current_thread_handle(uint32_t *handle)
{
    assert(handle != NULL);
    *handle = 10u;
    return ASTRA_SYSCALL_OK;
}

uint32_t
astra_thread_info(uint32_t handle, AstraThreadInfo *info)
{
    assert(handle == 10u && info != NULL && info->size == sizeof(*info));
    info->runtime_ns = UINT64_C(987654321);
    return ASTRA_SYSCALL_OK;
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
    struct rusage usage;
    int status;
    pid_t pid;

    file_fork_ready = 0;
    assert(fork() == (pid_t)-1 && errno == ENOTSUP);
    assert(clone_calls == 0u);
    file_fork_ready = 1;
    logged_status = UINT32_MAX;
    assert(fork() == (pid_t)123);
    assert(clone_calls == 1u);
    assert(obsolete_descriptor_gate_calls == 0u);
    assert(logged_status == UINT32_MAX);
    assert(registered_handle == 7u && registered_pid == 123u);
    assert(resumed_handle == 7u && registration_order < resume_order);
    assert(kill((pid_t)-123, SIGTERM) == 0);
    assert(signalled_selector == -123 && signalled_number == SIGTERM);
    assert(killpg((pid_t)1, SIGTERM) == 0);
    assert(signalled_group == 1 && signalled_number == SIGTERM);
    assert(setpgid(123, 123) == 0);
    assert(process_reply.process == 123 && process_reply.group == 123);
    assert(setpgrp() == 0);
    assert(process_reply.process == 0 && process_reply.group == 0);
    process_reply = (AstraPosixProcessReply){
        .process = 100, .parent = 50, .group = 100, .session = 100,
    };
    assert(getpid() == (pid_t)100 && getppid() == (pid_t)50);
    assert(getpgrp() == (pid_t)100 && getpgid(123) == (pid_t)100);
    assert(getsid(0) == (pid_t)100 && setsid() == (pid_t)100);
    assert(waitpid((pid_t)999, &status, WNOHANG) == (pid_t)-1);
    assert(errno == ECHILD);
    assert(waitpid((pid_t)123, &status,
                   WNOHANG | WUNTRACED | WCONTINUED) == 0);
    wait_result = ASTRA_SYSCALL_OK;
    child_status = 42u;
    assert(wait3(&status, 0, &usage) == (pid_t)123);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 42);
    assert(usage.ru_utime.tv_sec == 0 && usage.ru_utime.tv_usec == 7000);
    assert(usage.ru_maxrss == 32);
    assert(closed_handle == 7u);
    assert(getrusage(RUSAGE_CHILDREN, &usage) == 0);
    assert(usage.ru_utime.tv_sec == 0 && usage.ru_utime.tv_usec == 7000);
    assert(usage.ru_maxrss == 32);
    assert(getrusage(RUSAGE_SELF, &usage) == 0);
    assert(usage.ru_utime.tv_sec == 1 && usage.ru_utime.tv_usec == 234567);
    assert(usage.ru_maxrss == 48);
    assert(getrusage(RUSAGE_THREAD, &usage) == 0);
    assert(usage.ru_utime.tv_sec == 0 && usage.ru_utime.tv_usec == 987654);
    assert(usage.ru_maxrss == 48);

    clone_pid = 124u;
    clone_handle = 8u;
    assert(fork() == (pid_t)124);
    wait_result = ASTRA_SYSCALL_PEER_DEAD;
    child_status = ASTRA_STATUS_SIGNALLED(SIGTERM);
    pid = wait(&status);
    assert(pid == (pid_t)124 && WIFSIGNALED(status));
    assert(WTERMSIG(status) == SIGTERM && closed_handle == 8u);

    clone_pid = 125u;
    clone_handle = 9u;
    assert(fork() == (pid_t)125);
    child_status = ASTRA_STATUS_FAULTED;
    pid = wait(&status);
    assert(pid == (pid_t)125 && WIFSIGNALED(status));
    assert(WTERMSIG(status) == SIGSEGV && closed_handle == 9u);

    clone_pid = 0u;
    clone_handle = 0u;
    assert(fork() == 0);
    assert(file_after_fork_child_calls == 1u);
    assert(after_fork_child_calls == 1u);
    assert(getrusage(RUSAGE_CHILDREN, &usage) == 0);
    assert(usage.ru_utime.tv_sec == 0 && usage.ru_utime.tv_usec == 0);
    assert(wait(NULL) == (pid_t)-1 && errno == ECHILD);
    assert(waitpid((pid_t)-2, NULL, 0) == (pid_t)-1 && errno == EINVAL);

    clone_result = ASTRA_SYSCALL_RESOURCE_LIMIT;
    assert(fork() == (pid_t)-1 && errno == ENOMEM);
    assert(logged_status == ASTRA_SYSCALL_RESOURCE_LIMIT);
    clone_result = ASTRA_SYSCALL_INVALID_ARGUMENT;
    assert(fork() == (pid_t)-1 && errno == ENOTSUP);
    puts("ASTRA POSIX PROCESS PASS");
    return 0;
}

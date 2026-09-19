#define _GNU_SOURCE 1

#include <astra/runtime.h>
#include <astra/limits.h>
#include <astra/posix_descriptor.h>
#include <astra/status.h>
#include <astra/syscall.h>

#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct AstraPosixChild {
    struct AstraPosixChild *next;
    uint32_t handle;
    pid_t pid;
} AstraPosixChild;

static AstraPosixChild *children;
static uint64_t children_runtime_ns;
static uint32_t children_peak_resident_frames;

#if defined(__GLIBC__)
typedef __rusage_who_t AstraRusageWho;
#else
typedef int AstraRusageWho;
#endif

uint32_t
astra_posix_process_service(void)
{
    const AstraStartupCapability *capability = astra_startup_capability(
        astra_posix_startup(), ASTRA_CAPABILITY_POSIX_PROCESS);

    return capability != NULL ? capability->handle : 0u;
}

static int
posix_process_error(uint32_t status)
{
    if (status == ASTRA_STATUS_NOT_FOUND)
        errno = ESRCH;
    else if (status == ASTRA_STATUS_ACCESS)
        errno = EPERM;
    else if (status == ASTRA_STATUS_BUSY)
        errno = EPERM;
    else if (status == ASTRA_STATUS_INVALID ||
             status == ASTRA_STATUS_PROTOCOL)
        errno = EINVAL;
    else
        errno = EIO;
    return -1;
}

static long
resident_frames_to_kib(uint32_t frames)
{
    const uint64_t kib =
        (uint64_t)frames * (ASTRA_MEMORY_PAGE_SIZE / UINT32_C(1024));

    return kib > (uint64_t)LONG_MAX ? LONG_MAX : (long)kib;
}

static void
rusage_set(struct rusage *usage, uint64_t runtime_ns,
           uint32_t peak_resident_frames)
{
    (void)memset(usage, 0, sizeof(*usage));
    usage->ru_utime.tv_sec =
        (time_t)(runtime_ns / UINT64_C(1000000000));
    usage->ru_utime.tv_usec =
        (suseconds_t)((runtime_ns % UINT64_C(1000000000)) / 1000u);
    usage->ru_maxrss = resident_frames_to_kib(peak_resident_frames);
}

static void
discard_inherited_children(void)
{
    while (children != NULL) {
        AstraPosixChild *child = children;

        children = child->next;
        free(child);
    }
    children_runtime_ns = 0u;
    children_peak_resident_frames = 0u;
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
                        result == ASTRA_SYSCALL_RESOURCE_LIMIT ? ENOMEM :
                result == ASTRA_SYSCALL_INVALID_ARGUMENT ? ENOTSUP : EIO;
        return (pid_t)-1;
    }
    if (process_id == 0u) {
        discard_inherited_children();
        free(child);
        if (astra_posix_file_after_fork_child() != 0 ||
            astra_posix_socket_after_fork_child() != 0) {
            static const char message[] =
                "fork: child I/O reinitialization failed\n";

            (void)astra_log_failure("fork filesystem reinitialization",
                                    (uint32_t)errno);
            ssize_t ignored = write(STDERR_FILENO, message,
                                    sizeof(message) - 1u);
            (void)ignored;
            _exit(127);
        }
        return (pid_t)0;
    }
    if (handle == 0u) {
        free(child);
        errno = EIO;
        return (pid_t)-1;
    }
    {
        uint32_t service = astra_posix_process_service();

        if (service != 0u) {
            result = astra_posix_process_register(service, handle,
                                                   process_id, 0u);
            if (result != ASTRA_STATUS_OK) {
                (void)astra_process_terminate(handle, SIGKILL);
                (void)astra_close(handle);
                free(child);
                (void)posix_process_error(result);
                return (pid_t)-1;
            }
        }
    }
    result = astra_process_resume(handle);
    if (result != ASTRA_SYSCALL_OK) {
        (void)astra_process_terminate(handle, SIGKILL);
        (void)astra_close(handle);
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

pid_t
getpid(void)
{
    AstraPosixProcessReply reply;
    uint32_t service = astra_posix_process_service();

    if (service == 0u ||
        astra_posix_process_query(service, 0, &reply) != ASTRA_STATUS_OK)
        return (pid_t)-1;
    return (pid_t)reply.process;
}

pid_t
getppid(void)
{
    AstraPosixProcessReply reply;
    uint32_t service = astra_posix_process_service();

    if (service == 0u ||
        astra_posix_process_query(service, 0, &reply) != ASTRA_STATUS_OK)
        return (pid_t)-1;
    return (pid_t)reply.parent;
}

int
kill(pid_t process, int signal_number)
{
    uint32_t service = astra_posix_process_service();
    uint32_t status;

    if (signal_number < 0 || signal_number >= 32) {
        errno = EINVAL;
        return -1;
    }
    if (service == 0u) {
        errno = ENOSYS;
        return -1;
    }
    status = astra_posix_process_signal(service, (int32_t)process,
                                        (uint32_t)signal_number);
    return status == ASTRA_STATUS_OK ? 0 : posix_process_error(status);
}

int
killpg(pid_t group, int signal_number)
{
    uint32_t service = astra_posix_process_service();
    uint32_t status;

    if (group <= 0 || signal_number < 0 || signal_number >= 32) {
        errno = EINVAL;
        return -1;
    }
    if (service == 0u) {
        errno = ENOSYS;
        return -1;
    }
    status = astra_posix_process_signal_group(
        service, (int32_t)group, (uint32_t)signal_number);
    return status == ASTRA_STATUS_OK ? 0 : posix_process_error(status);
}

int
setpgid(pid_t process, pid_t group)
{
    uint32_t service = astra_posix_process_service();
    uint32_t status;

    if (process < 0 || group < 0) {
        errno = EINVAL;
        return -1;
    }
    if (service == 0u) {
        errno = ENOSYS;
        return -1;
    }
    status = astra_posix_process_setpgid(service, (int32_t)process,
                                         (int32_t)group);
    return status == ASTRA_STATUS_OK ? 0 : posix_process_error(status);
}

pid_t
getpgid(pid_t process)
{
    AstraPosixProcessReply reply;
    uint32_t service = astra_posix_process_service();
    uint32_t status;

    if (process < 0) {
        errno = EINVAL;
        return (pid_t)-1;
    }
    if (service == 0u) {
        errno = ENOSYS;
        return (pid_t)-1;
    }
    status = astra_posix_process_query(service, (int32_t)process, &reply);
    if (status != ASTRA_STATUS_OK) {
        (void)posix_process_error(status);
        return (pid_t)-1;
    }
    return (pid_t)reply.group;
}

pid_t
getpgrp(void)
{
    return getpgid(0);
}

int
setpgrp(void)
{
    return setpgid(0, 0);
}

pid_t
getsid(pid_t process)
{
    AstraPosixProcessReply reply;
    uint32_t service = astra_posix_process_service();
    uint32_t status;

    if (process < 0) {
        errno = EINVAL;
        return (pid_t)-1;
    }
    if (service == 0u) {
        errno = ENOSYS;
        return (pid_t)-1;
    }
    status = astra_posix_process_query(service, (int32_t)process, &reply);
    if (status != ASTRA_STATUS_OK) {
        (void)posix_process_error(status);
        return (pid_t)-1;
    }
    return (pid_t)reply.session;
}

pid_t
setsid(void)
{
    AstraPosixProcessReply reply;
    uint32_t service = astra_posix_process_service();
    uint32_t status;

    if (service == 0u) {
        errno = ENOSYS;
        return (pid_t)-1;
    }
    status = astra_posix_process_setsid(service, &reply);
    if (status != ASTRA_STATUS_OK) {
        (void)posix_process_error(status);
        return (pid_t)-1;
    }
    return (pid_t)reply.session;
}

static int
posix_wait_status(uint32_t astra_status, uint32_t wait_result)
{
    if (ASTRA_STATUS_IS_SIGNALLED(astra_status))
        return (int)ASTRA_STATUS_SIGNAL_NUMBER(astra_status);
    if (wait_result == ASTRA_SYSCALL_PEER_DEAD ||
        (astra_status & ASTRA_STATUS_VERDICT) != 0u)
        return SIGSEGV;
    return (int)((astra_status & 0xffu) << 8);
}

static pid_t
reap_child(AstraPosixChild *child, AstraPosixChild *previous,
           int *status, struct rusage *usage, uint32_t astra_status,
           uint32_t wait_result)
{
    AstraProcessInfo info = {0};
    pid_t pid = child->pid;

    info.size = sizeof(info);
    {
        uint32_t info_status = astra_process_info(child->handle, &info);

        if (info_status != ASTRA_SYSCALL_OK) {
            (void)astra_log_failure("wait process accounting", info_status);
            info.runtime_ns = 0u;
        }
    }
    if (astra_close(child->handle) != ASTRA_SYSCALL_OK) {
        errno = EIO;
        return (pid_t)-1;
    }
    if (previous == NULL)
        children = child->next;
    else
        previous->next = child->next;
    free(child);
    if (UINT64_MAX - children_runtime_ns < info.runtime_ns)
        children_runtime_ns = UINT64_MAX;
    else
        children_runtime_ns += info.runtime_ns;
    if (info.peak_resident_frames > children_peak_resident_frames)
        children_peak_resident_frames = info.peak_resident_frames;
    if (status != NULL)
        *status = posix_wait_status(astra_status, wait_result);
    if (usage != NULL)
        rusage_set(usage, info.runtime_ns, info.peak_resident_frames);
    return pid;
}

static pid_t
wait_for_child(pid_t pid, int *status, int options, struct rusage *usage)
{
    if ((options & ~(WNOHANG | WUNTRACED | WCONTINUED)) != 0 ||
        pid == 0 || pid < (pid_t)-1) {
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
                return reap_child(child, previous, status, usage,
                                  astra_status, wait_result);
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
waitpid(pid_t pid, int *status, int options)
{
    return wait_for_child(pid, status, options, NULL);
}

pid_t
wait(int *status)
{
    return waitpid((pid_t)-1, status, 0);
}

pid_t
wait3(int *status, int options, struct rusage *usage)
{
    return wait_for_child((pid_t)-1, status, options, usage);
}

int
getrusage(AstraRusageWho who, struct rusage *usage)
{
    uint64_t runtime;
    uint32_t peak_resident_frames;

    if (usage == NULL) {
        errno = EFAULT;
        return -1;
    }
    if (who == RUSAGE_CHILDREN) {
        runtime = children_runtime_ns;
        peak_resident_frames = children_peak_resident_frames;
    } else if (who == RUSAGE_SELF) {
        const AstraStartupInfo *startup = astra_posix_startup();
        AstraProcessInfo info = {0};

        if (startup == NULL || startup->process_handle == 0u) {
            errno = EIO;
            return -1;
        }
        info.size = sizeof(info);
        if (astra_process_info(startup->process_handle, &info) !=
            ASTRA_SYSCALL_OK) {
            errno = EIO;
            return -1;
        }
        runtime = info.runtime_ns;
        peak_resident_frames = info.peak_resident_frames;
    } else if (who == RUSAGE_THREAD) {
        AstraThreadInfo info = {0};
        AstraProcessInfo process_info = {0};
        const AstraStartupInfo *startup = astra_posix_startup();
        uint32_t handle;

        if (startup == NULL || startup->process_handle == 0u ||
            astra_current_thread_handle(&handle) != ASTRA_SYSCALL_OK ||
            handle == 0u) {
            errno = EIO;
            return -1;
        }
        info.size = sizeof(info);
        if (astra_thread_info(handle, &info) != ASTRA_SYSCALL_OK) {
            errno = EIO;
            return -1;
        }
        process_info.size = sizeof(process_info);
        if (astra_process_info(startup->process_handle, &process_info) !=
            ASTRA_SYSCALL_OK) {
            errno = EIO;
            return -1;
        }
        runtime = info.runtime_ns;
        peak_resident_frames = process_info.peak_resident_frames;
    } else {
        errno = EINVAL;
        return -1;
    }
    rusage_set(usage, runtime, peak_resident_frames);
    return 0;
}

#define _GNU_SOURCE
#include "astra_storage_probe.h"

#include <blkid/blkid.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

_Static_assert(sizeof(AstraStorageProbeResult) <= PIPE_BUF,
               "probe result must fit one atomic pipe write");

static uint64_t now_ns(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return 0u;
    return (uint64_t)now.tv_sec * UINT64_C(1000000000) + (uint64_t)now.tv_nsec;
}

static int value(blkid_probe probe, const char *name, char *text, size_t capacity)
{
    const char *bytes = NULL;
    size_t length = 0u;

    if (blkid_probe_lookup_value(probe, name, &bytes, &length) != 0) {
        text[0] = '\0';
        return 0;
    }
    if (length != 0u && bytes[length - 1u] == '\0')
        --length;
    if (length >= capacity)
        return ENAMETOOLONG;
    if (memchr(bytes, '\0', length) != NULL)
        return EILSEQ;
    memcpy(text, bytes, length);
    text[length] = '\0';
    return 0;
}

static void inspect(int fd, uint64_t offset, uint64_t length,
                      AstraStorageProbeResult *result)
{
    blkid_probe probe = blkid_new_probe();
    char type[64];
    char version[64];
    int status;

    if (probe == NULL) {
        result->error = ENOMEM;
        return;
    }
    if (blkid_probe_set_device(probe, fd, (blkid_loff_t)offset,
                                (blkid_loff_t)length) != 0 ||
        blkid_probe_enable_superblocks(probe, 1) != 0 ||
        blkid_probe_set_superblocks_flags(probe,
            BLKID_SUBLKS_TYPE | BLKID_SUBLKS_UUID | BLKID_SUBLKS_LABEL |
            BLKID_SUBLKS_VERSION) != 0) {
        result->error = errno == 0 ? EIO : errno;
        blkid_free_probe(probe);
        return;
    }
    status = blkid_do_safeprobe(probe);
    if (status == -2) {
        result->ambiguous = 1u;
    } else if (status < 0) {
        result->error = errno == 0 ? EIO : errno;
    } else if (status == 0) {
        int error = value(probe, "TYPE", type, sizeof(type));
        if (error == 0)
            error = value(probe, "VERSION", version, sizeof(version));
        if (error == 0)
            error = value(probe, "UUID", result->uuid, sizeof(result->uuid));
        if (error == 0)
            error = value(probe, "LABEL", result->label, sizeof(result->label));
        result->error = error;
        if (error == 0) {
            if (strcmp(type, "ext4") == 0)
                result->format = ASTRA_VOLUME_EXT4;
            else if (strcmp(type, "vfat") == 0 && strcmp(version, "FAT32") == 0)
                result->format = ASTRA_VOLUME_FAT32;
            else if (strcmp(type, "exfat") == 0)
                result->format = ASTRA_VOLUME_EXFAT;
            else if (strcmp(type, "iso9660") == 0)
                result->format = ASTRA_VOLUME_ISO9660;
            else if (strcmp(type, "udf") == 0)
                result->format = ASTRA_VOLUME_UDF;
        }
    }
    blkid_free_probe(probe);
}

static void child(int source_fd, int result_fd, uint64_t source,
                   uint64_t generation, uid_t uid, gid_t gid,
                   uint64_t offset, uint64_t length, pid_t parent)
{
    AstraStorageProbeResult result = {.source = source, .generation = generation};
    int disk = fcntl(source_fd, F_DUPFD_CLOEXEC, 10);
    int output = fcntl(result_fd, F_DUPFD_CLOEXEC, 10);

    if (disk < 0 || output < 0 || dup2(disk, 3) < 0 || dup2(output, 4) < 0)
        _exit(1);
    /* A worker keeps only its read-only source and one result pipe. */
    if (syscall(SYS_close_range, 5u, UINT_MAX, 0u) != 0)
        _exit(1);
    (void)close(0);
    (void)close(1);
    (void)close(2);
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0)
        result.error = errno;
    else if (geteuid() == 0 &&
             (setgroups(0u, NULL) != 0 || setresgid(gid, gid, gid) != 0 ||
              setresuid(uid, uid, uid) != 0))
        result.error = errno;
    else if (prctl(PR_SET_PDEATHSIG, SIGKILL, 0, 0, 0) != 0 || getppid() != parent)
        result.error = ECANCELED;
    else
        inspect(3, offset, length, &result);
    result.completed_ns = now_ns();
    while (write(4, &result, sizeof(result)) < 0)
        if (errno != EINTR)
            _exit(1);
    _exit(0);
}

int astra_storage_probe_start(AstraStorageProbe *probe, int source_fd,
                              uint64_t source, uint64_t generation,
                              uid_t uid, gid_t gid, uint32_t timeout_ms,
                              uint64_t offset, uint64_t length)
{
    struct stat info;
    int pipes[2];
    int flags;
    uint64_t now;
    pid_t pid;

    if (probe == NULL || probe->pid != 0 || source == 0u || generation == 0u ||
        uid == 0u || gid == 0u || timeout_ms == 0u)
        return EINVAL;
    flags = fcntl(source_fd, F_GETFL);
    if (flags < 0 || fstat(source_fd, &info) != 0)
        return errno;
    if ((flags & O_ACCMODE) != O_RDONLY ||
        (!S_ISREG(info.st_mode) && !S_ISBLK(info.st_mode)))
        return EACCES;
    if (offset > INT64_MAX || length > INT64_MAX ||
        (S_ISREG(info.st_mode) && (info.st_size < 0 ||
            offset > (uint64_t)info.st_size ||
            length > (uint64_t)info.st_size - offset)))
        return EINVAL;
    if (geteuid() != 0 && (uid != geteuid() || gid != getegid()))
        return EACCES;
    now = now_ns();
    if (now == 0u)
        return EIO;
    if (pipe2(pipes, O_CLOEXEC | O_NONBLOCK) != 0)
        return errno;
    pid_t parent = getpid();
    pid = fork();
    if (pid < 0) {
        int error = errno;
        close(pipes[0]);
        close(pipes[1]);
        return error;
    }
    if (pid == 0)
        child(source_fd, pipes[1], source, generation, uid, gid, offset, length, parent);
    close(pipes[1]);
    *probe = (AstraStorageProbe){
        .pid = pid, .result_fd = pipes[0], .source = source,
        .generation = generation,
        .deadline_ns = now + (uint64_t)timeout_ms * UINT64_C(1000000)
    };
    return 0;
}

void astra_storage_probe_cancel(AstraStorageProbe *probe)
{
    if (probe == NULL || probe->pid <= 0 || probe->cancelled != 0)
        return;
    probe->cancelled = ECANCELED;
    (void)kill(probe->pid, SIGKILL);
}

int astra_storage_probe_poll(AstraStorageProbe *probe,
                             AstraStorageProbeResult *result)
{
    int status = 0;
    pid_t done;

    if (probe == NULL || probe->pid <= 0 || result == NULL)
        return -EINVAL;
    done = waitpid(probe->pid, &status, WNOHANG);
    if (done == 0 && probe->cancelled == 0) {
        uint64_t now = now_ns();
        if (now == 0u || now >= probe->deadline_ns) {
            probe->cancelled = now == 0u ? EIO : ETIMEDOUT;
            (void)kill(probe->pid, SIGKILL);
        }
    }
    if (done == 0 || (done < 0 && errno == EINTR))
        return 0;
    *result = (AstraStorageProbeResult){
        .source = probe->source, .generation = probe->generation, .error = EIO
    };
    if (done == probe->pid && WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        AstraStorageProbeResult received;
        ssize_t bytes = read(probe->result_fd, &received, sizeof(received));
        if (bytes == (ssize_t)sizeof(received) &&
            received.source == probe->source &&
            received.generation == probe->generation &&
            received.completed_ns != 0u &&
            received.error >= 0 && received.format <= ASTRA_VOLUME_TMPFS &&
            received.ambiguous <= 1u &&
            memchr(received.uuid, '\0', sizeof(received.uuid)) != NULL &&
            memchr(received.label, '\0', sizeof(received.label)) != NULL)
            *result = received;
        else
            result->error = EPROTO;
    }
    if (result->completed_ns != 0u && result->completed_ns > probe->deadline_ns)
        result->error = ETIMEDOUT;
    if (probe->cancelled != 0)
        result->error = probe->cancelled;
    close(probe->result_fd);
    *probe = (AstraStorageProbe){0};
    return 1;
}

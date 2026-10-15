#define _GNU_SOURCE
#include "astra_storage_work.h"
#include "astra_storage_path.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <linux/cdrom.h>
#include <unistd.h>

static void retain(const int *originals, unsigned count, pid_t parent)
{
    int copies[5];
    if (count > 5u)
        _exit(1);
    for (unsigned at = 0u; at < count; ++at) {
        copies[at] = originals[at] < 0 ? -1 : fcntl(originals[at], F_DUPFD_CLOEXEC, 10);
        if (originals[at] >= 0 && copies[at] < 0)
            _exit(1);
    }
    for (unsigned at = 0u; at < count; ++at)
        if (copies[at] < 0)
            (void)close(3 + (int)at);
        else if (dup2(copies[at], 3 + (int)at) < 0)
            _exit(1);
    if (syscall(SYS_close_range, 3u + count, UINT_MAX, 0u) != 0)
        _exit(1);
    (void)close(0);
    (void)close(1);
    (void)close(2);
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0 ||
        prctl(PR_SET_PDEATHSIG, SIGKILL, 0, 0, 0) != 0 || getppid() != parent)
        _exit(1);
}

static void child(AstraStorageDomain domain, AstraStorageRecord record,
                    AstraStorageMounted mounted, int source, int output, uint32_t operation,
                    pid_t parent)
{
    int originals[] = {domain.directory, source, mounted.loop, output};
    retain(originals, 4u, parent);
    domain.directory = 3;
    mounted.loop = originals[2] < 0 ? -1 : 5;
    mounted.backing = -1; /* Parent retains backing and performs final cleanup. */
    mounted.root = -1;
    int error;
    if (operation == ASTRA_STORAGE_WORK_EJECT)
        error = ioctl(4, CDROMEJECT, 0) == 0 ? 0 : errno;
    else if (operation == ASTRA_STORAGE_WORK_ATTACH)
        error = astra_storage_linux_attach(&domain, &record, originals[1] < 0 ? -1 : 4, &mounted);
    else
        error = astra_storage_linux_detach(&mounted, (record.flags & ASTRA_VOLUME_READ_ONLY) == 0u &&
            record.state != ASTRA_VOLUME_LOST && record.error != ENODEV);
    AstraStorageWorkReply reply = {.id = record.id, .generation = record.generation,
        .linux_mount_id = mounted.linux_mount_id, .operation = operation,
        .error = error, .attached = (uint32_t)mounted.attached,
        .root = mounted.root >= 0 ? 1u : 0u};
    if (astra_storage_send_packet(6, &reply, sizeof(reply), &mounted.root, reply.root) != 0)
        _exit(1);
    _exit(0);
}

static int endpoints(int sockets[2])
{
    int enabled = 1;
    if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC | SOCK_NONBLOCK, 0, sockets) != 0)
        return errno;
    if (setsockopt(sockets[0], SOL_SOCKET, SO_PASSCRED, &enabled, sizeof(enabled)) != 0) {
        int error = errno;
        close(sockets[0]);
        close(sockets[1]);
        return error;
    }
    return 0;
}

int astra_storage_work_start(AstraStorageWork *work, const AstraStorageDomain *domain,
                              const AstraStorageRecord *record, int source,
                              const AstraStorageMounted *mounted, uint32_t operation)
{
    int sockets[2];
    if (work == NULL || domain == NULL || record == NULL || mounted == NULL ||
        work->pid != 0 ||
        (operation == ASTRA_STORAGE_WORK_EJECT ? source < 0 || record->kind != ASTRA_VOLUME_SOURCE ||
            record->class_id != ASTRA_VOLUME_OPTICAL || record->origin != ASTRA_VOLUME_ORIGIN_DEVICE :
            record->id != mounted->id || !mounted->directory ||
            (operation != ASTRA_STORAGE_WORK_ATTACH && operation != ASTRA_STORAGE_WORK_DETACH)))
        return EINVAL;
    int error = endpoints(sockets);
    if (error != 0)
        return error;
    pid_t parent = getpid();
    pid_t pid = fork();
    if (pid < 0) {
        error = errno;
        close(sockets[0]);
        close(sockets[1]);
        return error;
    }
    if (pid == 0)
        child(*domain, *record, *mounted, source, sockets[1], operation, parent);
    close(sockets[1]);
    *work = (AstraStorageWork){.pid = pid, .result = sockets[0], .id = record->id,
        .generation = record->generation, .operation = operation};
    return 0;
}

int astra_storage_work_path_start(AstraStorageWork *work, const AstraStorageRecord *record,
                                   int allowed_root, const AstraStoragePathRequest *request,
                                   const int fds[ASTRA_STORAGE_PROVIDER_FDS])
{
    int sockets[2];
    if (work == NULL || record == NULL || request == NULL || fds == NULL ||
        work->pid != 0 || allowed_root < 0 || record->id != request->mount ||
        record->generation != request->generation || record->state != ASTRA_VOLUME_MOUNTED)
        return EINVAL;
    int error = endpoints(sockets);
    if (error != 0)
        return error;
    pid_t parent = getpid();
    pid_t pid = fork();
    if (pid < 0) {
        error = errno;
        close(sockets[0]);
        close(sockets[1]);
        return error;
    }
    if (pid == 0) {
        int originals[] = {allowed_root, fds[0], fds[1], fds[2], sockets[1]};
        retain(originals, 5u, parent);
        int roots[3] = {4, fds[1] < 0 ? -1 : 5, fds[2] < 0 ? -1 : 6}, result;
        AstraStoragePathReply reply;
        astra_storage_path_execute(3, (record->flags & ASTRA_VOLUME_READ_ONLY) != 0u,
            request, roots, &reply, &result);
        if (astra_storage_path_reply(7, &reply, result) != 0)
            _exit(1);
        _exit(0);
    }
    close(sockets[1]);
    *work = (AstraStorageWork){.pid = pid, .result = sockets[0], .id = record->id,
        .generation = record->generation, .operation = ASTRA_STORAGE_WORK_PATH,
        .serial = request->serial, .epoch = request->epoch, .path_operation = request->operation};
    return 0;
}

void astra_storage_work_cancel(AstraStorageWork *work)
{
    if (work != NULL && work->pid > 0 && work->cancelled == 0) {
        work->cancelled = ECANCELED;
        (void)kill(work->pid, SIGKILL);
    }
}

static int receive(AstraStorageWork *work, void *reply, size_t bytes,
                     int fds[ASTRA_STORAGE_PROVIDER_FDS], unsigned *count, int *error)
{
    int status;
    struct ucred credentials;
    *count = 0u;
    *error = EIO;
    if (work == NULL || work->pid <= 0)
        return -EINVAL;
    pid_t done = waitpid(work->pid, &status, WNOHANG);
    if (done == 0 || (done < 0 && errno == EINTR))
        return 0;
    if (done == work->pid && WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        *error = astra_storage_receive_packet(work->result, reply, bytes,
                                               fds, count, &credentials);
        if (*error == 0 && (credentials.pid != work->pid || credentials.uid != geteuid() ||
            credentials.gid != getegid()))
            *error = EPROTO;
    }
    if (work->cancelled != 0)
        *error = work->cancelled;
    return 1;
}

int astra_storage_work_poll(AstraStorageWork *work, AstraStorageWorkReply *reply, int *root)
{
    int fds[ASTRA_STORAGE_PROVIDER_FDS], error;
    unsigned count;
    if (work == NULL || reply == NULL || root == NULL)
        return -EINVAL;
    *root = -1;
    AstraStorageWorkReply received;
    int ready = receive(work, &received, sizeof(received), fds, &count, &error);
    if (ready <= 0)
        return ready;
    *reply = (AstraStorageWorkReply){.id = work->id, .generation = work->generation,
        .operation = work->operation, .error = error};
    if (error == 0) {
        if (received.id == work->id &&
            received.generation == work->generation && received.operation == work->operation &&
            received.error >= 0 && received.error <= 4095 && received.attached <= 1u &&
            received.root <= 1u && count == received.root) {
            *reply = received;
            if (count != 0u) {
                *root = fds[0];
                fds[0] = -1;
            }
        } else
            reply->error = EPROTO;
    }
    for (unsigned at = 0u; at < count; ++at)
        if (fds[at] >= 0)
            close(fds[at]);
    close(work->result);
    *work = (AstraStorageWork){.result = -1};
    return 1;
}

int astra_storage_work_path_poll(AstraStorageWork *work, AstraStoragePathReply *reply, int *result)
{
    int fds[ASTRA_STORAGE_PROVIDER_FDS], error;
    unsigned count;
    if (work == NULL || reply == NULL || result == NULL || work->operation != ASTRA_STORAGE_WORK_PATH)
        return -EINVAL;
    *result = -1;
    AstraStoragePathReply received;
    int ready = receive(work, &received, sizeof(received), fds, &count, &error);
    if (ready <= 0)
        return ready;
    *reply = (AstraStoragePathReply){.version = ASTRA_STORAGE_PROVIDER_VERSION,
        .serial = work->serial, .epoch = work->epoch, .error = error};
    if (error == 0) {
        if (received.version != ASTRA_STORAGE_PROVIDER_VERSION || received.serial != work->serial ||
            received.epoch != work->epoch || received.error < 0 || received.error > 4095 ||
            count != (unsigned)(received.error == 0 &&
                (work->path_operation == ASTRA_STORAGE_PATH_OPEN || work->path_operation == ASTRA_STORAGE_PATH_RESOLVE)) ||
            received.length > sizeof(received.data) ||
            memchr(received.leaf, '\0', sizeof(received.leaf)) == NULL ||
            memchr(received.continuation, '\0', sizeof(received.continuation)) == NULL)
            reply->error = EPROTO;
        else {
            *reply = received;
            if (count != 0u) {
                *result = fds[0];
                fds[0] = -1;
            }
        }
    }
    for (unsigned at = 0u; at < count; ++at)
        if (fds[at] >= 0)
            close(fds[at]);
    close(work->result);
    *work = (AstraStorageWork){.result = -1};
    return 1;
}

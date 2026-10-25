#define _GNU_SOURCE
#include "astra_storage_work.h"
#include "astra_storage_path.h"

#include <blkid/blkid.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <time.h>
#include <linux/cdrom.h>
#include <linux/fs.h>
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
    if (operation == ASTRA_STORAGE_WORK_EJECT) {
        /* Explicit unlock matches Linux eject and clears drive media removal. */
        if (ioctl(4, CDROM_LOCKDOOR, 0) != 0 && errno != EOPNOTSUPP)
            error = errno;
        else
            error = ioctl(4, CDROMEJECT, 0) == 0 ? 0 : errno;
    }
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

int astra_storage_work_format_start(AstraStorageWork *work,
    const AstraStorageRecord *record, int device, int executable,
    uint32_t format, const char *label, uid_t uid, gid_t gid)
{
    if (work == NULL || record == NULL || label == NULL || work->pid != 0 ||
        device < 0 || executable < 0 || uid == 0u || gid == 0u ||
        record->kind != ASTRA_VOLUME_SOURCE ||
        record->origin != ASTRA_VOLUME_ORIGIN_DEVICE ||
        record->state != ASTRA_VOLUME_FORMATTING ||
        (format != ASTRA_VOLUME_FAT32 && format != ASTRA_VOLUME_EXFAT))
        return EINVAL;
    size_t length = strnlen(label, 12u);
    if (length == 12u) return EINVAL;
    for (size_t at = 0u; at < length; ++at)
        if ((label[at] < 'A' || label[at] > 'Z') &&
            (label[at] < 'a' || label[at] > 'z') &&
            (label[at] < '0' || label[at] > '9') &&
            label[at] != '_' && label[at] != ' ')
            return EINVAL;
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return errno;
    uint64_t deadline = (uint64_t)now.tv_sec * UINT64_C(1000000000) +
        (uint64_t)now.tv_nsec + UINT64_C(300000000000);
    pid_t parent = getpid();
    pid_t pid = fork();
    if (pid < 0) return errno;
    if (pid == 0) {
        const int originals[] = {device, executable};
        retain(originals, 2u, parent);
        int null = open("/dev/null", O_RDWR | O_CLOEXEC);
        if (null < 0 || dup2(null, 0) < 0 || dup2(null, 1) < 0 ||
            dup2(null, 2) < 0)
            _exit(125);
        if (null > 2) close(null);
        if (setgroups(0u, NULL) != 0 || setresgid(gid, gid, gid) != 0 ||
            setresuid(uid, uid, uid) != 0)
            _exit(125);
        char *fat[] = {"astra-mkfs.fat", "-F", "32", "--astra-fd", "3", NULL,
                       NULL, NULL};
        char *exfat[] = {"astra-mkfs.exfat", "-P", "none", "--astra-fd", "3",
                         NULL, NULL, NULL};
        char **args = format == ASTRA_VOLUME_FAT32 ? fat : exfat;
        if (length != 0u) {
            args[5] = format == ASTRA_VOLUME_FAT32 ? "-n" : "-L";
            args[6] = (char *)label;
        }
        char *environment[] = {"LC_ALL=C", NULL};
        (void)syscall(SYS_execveat, 4, "", args, environment, AT_EMPTY_PATH);
        _exit(127);
    }
    *work = (AstraStorageWork){.pid = pid, .result = -1,
        .id = record->id, .generation = record->generation,
        .operation = ASTRA_STORAGE_WORK_FORMAT, .deadline_ns = deadline};
    return 0;
}

int astra_storage_work_partition_start(AstraStorageWork *work,
    const AstraStorageRecord *record, int device, int executable,
    const AstraVolumePartitionEdit *edit)
{
    if (work == NULL || record == NULL || edit == NULL || work->pid != 0 ||
        device < 0 || executable < 0 || record->kind != ASTRA_VOLUME_DEVICE ||
        record->state != ASTRA_VOLUME_PARTITIONING ||
        record->origin != ASTRA_VOLUME_ORIGIN_DEVICE ||
        (record->class_id != ASTRA_VOLUME_USB &&
         record->class_id != ASTRA_VOLUME_FIXED) ||
        (record->flags & (ASTRA_VOLUME_SYSTEM | ASTRA_VOLUME_PHYSICAL_READ_ONLY)) != 0u ||
        (edit->operation != ASTRA_VOLUME_PARTITION_INITIALIZE &&
         edit->operation != ASTRA_VOLUME_PARTITION_CREATE &&
         edit->operation != ASTRA_VOLUME_PARTITION_DELETE))
        return EINVAL;
    uint64_t bytes = 0;
    int sector_size = 0;
    struct stat info;
    if (fstat(device, &info) != 0 || !S_ISBLK(info.st_mode) ||
        ioctl(device, BLKGETSIZE64, &bytes) != 0 ||
        ioctl(device, BLKSSZGET, &sector_size) != 0 ||
        sector_size < 512 || bytes != record->capacity_bytes)
        return EINVAL;
    if (edit->operation == ASTRA_VOLUME_PARTITION_INITIALIZE) {
        if ((edit->map != ASTRA_VOLUME_PARTITION_MAP_MBR &&
             edit->map != ASTRA_VOLUME_PARTITION_MAP_GPT) ||
            edit->format != 0u || edit->number != 0u ||
            edit->offset_bytes != 0u || edit->length_bytes != 0u)
            return EINVAL;
    } else if (edit->map != record->partition_map ||
               (edit->map != ASTRA_VOLUME_PARTITION_MAP_MBR &&
                edit->map != ASTRA_VOLUME_PARTITION_MAP_GPT))
        return EINVAL;
    if (edit->operation == ASTRA_VOLUME_PARTITION_CREATE &&
        (edit->number != 0u ||
         (edit->format != ASTRA_VOLUME_FAT32 &&
          edit->format != ASTRA_VOLUME_EXFAT &&
          edit->format != ASTRA_VOLUME_EXT4) ||
         edit->offset_bytes < UINT64_C(1048576) ||
         edit->length_bytes == 0u ||
         edit->offset_bytes % (uint64_t)sector_size != 0u ||
         edit->length_bytes % (uint64_t)sector_size != 0u ||
         edit->offset_bytes >= bytes ||
         edit->length_bytes > bytes - edit->offset_bytes ||
         edit->offset_bytes > INT64_MAX - edit->length_bytes ||
         (edit->map == ASTRA_VOLUME_PARTITION_MAP_MBR &&
          (edit->offset_bytes / (uint64_t)sector_size > UINT32_MAX ||
           edit->length_bytes / (uint64_t)sector_size > UINT32_MAX ||
           (edit->offset_bytes + edit->length_bytes) /
               (uint64_t)sector_size > UINT32_MAX))))
        return EINVAL;
    if (edit->operation == ASTRA_VOLUME_PARTITION_DELETE &&
        (edit->format != 0u || edit->number == 0u || edit->number > 128u ||
         edit->offset_bytes != 0u || edit->length_bytes != 0u))
        return EINVAL;
    char script[160] = {0};
    const char *type = NULL;
    if (edit->operation == ASTRA_VOLUME_PARTITION_INITIALIZE)
        (void)snprintf(script, sizeof(script), "label: %s\n",
            edit->map == ASTRA_VOLUME_PARTITION_MAP_MBR ? "dos" : "gpt");
    else if (edit->operation == ASTRA_VOLUME_PARTITION_CREATE) {
        if (edit->map == ASTRA_VOLUME_PARTITION_MAP_MBR)
            type = edit->format == ASTRA_VOLUME_FAT32 ? "c" :
                   edit->format == ASTRA_VOLUME_EXFAT ? "7" : "83";
        else
            type = edit->format == ASTRA_VOLUME_EXT4 ?
                "0FC63DAF-8483-4772-8E79-3D69D8477DE4" :
                "EBD0A0A2-B9E5-4433-87C0-68B6B72699C7";
        int size = snprintf(script, sizeof(script), "start=%llu,size=%llu,type=%s\n",
            (unsigned long long)(edit->offset_bytes / (uint64_t)sector_size),
            (unsigned long long)(edit->length_bytes / (uint64_t)sector_size), type);
        if (size < 0 || (size_t)size >= sizeof(script)) return EINVAL;
    }
    int input[2] = {-1, -1};
    if (pipe2(input, O_CLOEXEC) != 0) return errno;
    size_t length = strlen(script);
    if (length != 0u && write(input[1], script, length) != (ssize_t)length) {
        int error = errno != 0 ? errno : EIO;
        close(input[0]); close(input[1]);
        return error;
    }
    close(input[1]);
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        int error = errno;
        close(input[0]);
        return error;
    }
    uint64_t deadline = (uint64_t)now.tv_sec * UINT64_C(1000000000) +
        (uint64_t)now.tv_nsec + UINT64_C(300000000000);
    pid_t parent = getpid();
    pid_t pid = fork();
    if (pid < 0) {
        int error = errno;
        close(input[0]);
        return error;
    }
    if (pid == 0) {
        const int originals[] = {device, executable, input[0], STDERR_FILENO};
        retain(originals, 4u, parent);
        int null = open("/dev/null", O_RDWR | O_CLOEXEC);
        /* /dev/null can be fd 0; copy it before replacing stdin. */
        if (null < 0 || dup2(null, 1) < 0 || dup2(6, 2) < 0 ||
            dup2(5, 0) < 0)
            _exit(125);
        if (null > 6) close(null);
        close(5);
        close(6);
        char number[12];
        (void)snprintf(number, sizeof(number), "%u", edit->number);
        char *initialize[] = {"sfdisk", "--no-reread", "--no-tell-kernel",
            "--wipe", "always", "/proc/self/fd/3", NULL};
        char *create[] = {"sfdisk", "--append", "--no-reread", "--no-tell-kernel",
            "--wipe", "never", "--wipe-partitions", "never", "/proc/self/fd/3", NULL};
        char *remove[] = {"sfdisk", "--delete", "--no-reread", "--no-tell-kernel",
            "/proc/self/fd/3", number, NULL};
        char **args = edit->operation == ASTRA_VOLUME_PARTITION_INITIALIZE ? initialize :
                      edit->operation == ASTRA_VOLUME_PARTITION_CREATE ? create : remove;
        char *environment[] = {"LC_ALL=C", NULL};
        (void)syscall(SYS_execveat, 4, "", args, environment, AT_EMPTY_PATH);
        _exit(127);
    }
    close(input[0]);
    *work = (AstraStorageWork){.pid = pid, .result = -1,
        .id = record->id, .generation = record->generation,
        .operation = ASTRA_STORAGE_WORK_PARTITION, .deadline_ns = deadline,
        .wipe_fd = device,
        .wipe_offset = edit->operation == ASTRA_VOLUME_PARTITION_CREATE ?
            edit->offset_bytes : 0u,
        .wipe_length = edit->operation == ASTRA_VOLUME_PARTITION_CREATE ?
            edit->length_bytes : 0u};
    return 0;
}

/* sfdisk cannot open partition subpaths of /proc/self/fd/3 to wipe old signatures. */
static int wipe_partition_signatures(const AstraStorageWork *work)
{
    blkid_probe probe = blkid_new_probe();
    if (probe == NULL) return ENOMEM;
    int error = 0;
    const uint8_t zeros[512] = {0};
    for (unsigned attempt = 0u; attempt < 32u; ++attempt) {
        if (blkid_probe_set_device(probe, work->wipe_fd,
                (blkid_loff_t)work->wipe_offset,
                (blkid_loff_t)work->wipe_length) != 0 ||
            blkid_probe_enable_superblocks(probe, 1) != 0 ||
            blkid_probe_set_superblocks_flags(probe,
                BLKID_SUBLKS_TYPE | BLKID_SUBLKS_MAGIC) != 0 ||
            blkid_probe_enable_partitions(probe, 1) != 0 ||
            blkid_probe_set_partitions_flags(probe, BLKID_PARTS_MAGIC) != 0) {
            error = errno != 0 ? errno : EIO;
            break;
        }
        int found = blkid_do_fullprobe(probe);
        if (found == 1) break;
        if (found != 0) { error = errno != 0 ? errno : EIO; break; }
        const char *position = NULL;
        size_t length = 0u;
        if (blkid_probe_lookup_value(probe, "SBMAGIC_OFFSET", &position, NULL) == 0)
            (void)blkid_probe_lookup_value(probe, "SBMAGIC", NULL, &length);
        if (length == 0u &&
            blkid_probe_lookup_value(probe, "PTMAGIC_OFFSET", &position, NULL) == 0)
            (void)blkid_probe_lookup_value(probe, "PTMAGIC", NULL, &length);
        if (position == NULL || length == 0u) { error = EIO; break; }
        errno = 0;
        char *end = NULL;
        unsigned long long relative = strtoull(position, &end, 10);
        if (errno != 0 || end == position || *end != '\0' ||
            relative >= work->wipe_length ||
            length > work->wipe_length - relative) { error = EIO; break; }
        uint64_t absolute = work->wipe_offset + relative;
        for (size_t at = 0u; at < length; ) {
            size_t bytes = length - at < sizeof(zeros) ? length - at : sizeof(zeros);
            ssize_t written = pwrite(work->wipe_fd, zeros, bytes, (off_t)(absolute + at));
            if (written < 0 && errno == EINTR) continue;
            if (written <= 0) {
                error = written < 0 ? errno : EIO;
                break;
            }
            at += (size_t)written;
        }
        if (error != 0) break;
        if (attempt == 31u) error = ELOOP;
    }
    blkid_free_probe(probe);
    if (error == 0 && fsync(work->wipe_fd) != 0) error = errno;
    return error;
}

int astra_storage_work_edit_poll(AstraStorageWork *work, int *error)
{
    if (work == NULL || error == NULL || work->pid <= 0 ||
        (work->operation != ASTRA_STORAGE_WORK_FORMAT &&
         work->operation != ASTRA_STORAGE_WORK_PARTITION))
        return -EINVAL;
    int status = 0;
    pid_t done = waitpid(work->pid, &status, WNOHANG);
    if (done == 0 || (done < 0 && errno == EINTR)) return 0;
    *error = work->cancelled != 0 ? work->cancelled :
        done != work->pid || !WIFEXITED(status) || WEXITSTATUS(status) != 0 ? EIO : 0;
    if (*error == 0 && work->operation == ASTRA_STORAGE_WORK_PARTITION &&
        work->wipe_length != 0u)
        *error = wipe_partition_signatures(work);
    if (*error != 0)
        fprintf(stderr, "astra-storage edit worker operation=%u id=%llu pid=%ld "
            "wait=%ld exited=%d code=%d signal=%d cancelled=%d error=%d\n",
            work->operation, (unsigned long long)work->id, (long)work->pid,
            (long)done, done == work->pid && WIFEXITED(status),
            done == work->pid && WIFEXITED(status) ? WEXITSTATUS(status) : -1,
            done == work->pid && WIFSIGNALED(status) ? WTERMSIG(status) : 0,
            work->cancelled, *error);
    *work = (AstraStorageWork){.result = -1};
    return 1;
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

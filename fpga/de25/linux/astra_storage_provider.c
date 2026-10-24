#define _GNU_SOURCE
#include "astra_storage_provider.h"
#include "astra_storage_discovery.h"
#include "astra68-resolve.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/signalfd.h>
#include <sys/random.h>
#include <sys/syscall.h>
#include <linux/openat2.h>
#include <poll.h>
#include <limits.h>
#include <time.h>
#include <unistd.h>

enum { PARTITION_IDLE, PARTITION_RETIRE, PARTITION_REREAD };

/* Provider-only Linux resources. Native descriptions/owners stay in QEMU. */
struct AstraStorageResource {
    uint64_t id;
    uint64_t generation;
    uint64_t offset;
    uint64_t length;
    uint64_t charged_bytes;
    uint64_t charged_inodes;
    int source;
    int format_fd;
    uint64_t format_generation;
    int format_error;
    int partition_fd;
    uint64_t partition_generation;
    int partition_error;
    int partition_tool_error;
    int partition_stage;
    uint64_t partition_deadline_ns;
    int selection_backing;
    int selection_leased;
    int path_socket;
    AstraStorageProbe probe;
    AstraStorageMounted mounted;
    AstraStorageWork work;
    struct AstraStorageResource *next;
};

static AstraStorageResource *resource(AstraStorageProvider *provider, uint64_t id)
{
    for (AstraStorageResource *entry = provider->resources; entry != NULL; entry = entry->next)
        if (entry->id == id)
            return entry;
    return NULL;
}

static int under_device(const AstraStorageCatalog *catalog,
    uint64_t id, uint64_t device)
{
    const AstraStorageRecord *record = astra_storage_find(catalog, id);
    while (record != NULL && record->id != device && record->parent != 0u)
        record = astra_storage_find(catalog, record->parent);
    return record != NULL && record->id == device;
}

int astra_storage_provider_device_idle(AstraStorageProvider *provider, uint64_t device)
{
    if (provider == NULL || astra_storage_find(&provider->catalog, device) == NULL)
        return EINVAL;
    for (AstraStorageResource *entry = provider->resources; entry != NULL;
         entry = entry->next) {
        const AstraStorageRecord *record = astra_storage_find(&provider->catalog, entry->id);
        if (entry->probe.pid == 0 && entry->work.pid == 0 &&
            entry->format_fd < 0 && entry->path_socket < 0 &&
            !entry->mounted.attached && !entry->mounted.directory &&
            entry->mounted.root < 0 &&
            entry->mounted.loop < 0 && entry->mounted.backing < 0 &&
            !(entry->source >= 0 && record != NULL &&
              record->state == ASTRA_VOLUME_PROBING))
            continue;
        if (under_device(&provider->catalog, entry->id, device))
            return EBUSY;
    }
    return 0;
}

static AstraStorageResource *reserve(AstraStorageProvider *provider, uint64_t id,
                                       uint64_t generation)
{
    AstraStorageResource *entry;
    if (resource(provider, id) != NULL || provider->resource_bytes > provider->resource_limit ||
        sizeof(*entry) > provider->resource_limit - provider->resource_bytes)
        return NULL;
    entry = calloc(1u, sizeof(*entry));
    if (entry == NULL)
        return NULL;
    *entry = (AstraStorageResource){.id = id, .generation = generation, .source = -1,
        .format_fd = -1, .partition_fd = -1,
        .selection_backing = -1,
        .path_socket = -1,
        .probe = {.result_fd = -1}, .mounted = {.root = -1, .loop = -1, .backing = -1},
        .work = {.result = -1}};
    AstraStorageResource **tail = &provider->resources;
    while (*tail != NULL)
        tail = &(*tail)->next;
    *tail = entry;
    provider->resource_bytes += sizeof(*entry);
    return entry;
}

static void remove_resource(AstraStorageProvider *provider, AstraStorageResource *entry)
{
    AstraStorageResource **link = &provider->resources;
    while (*link != entry)
        link = &(*link)->next;
    *link = entry->next;
    provider->resource_bytes -= sizeof(*entry);
    free(entry);
}

static void release_ram_charge(AstraStorageProvider *provider, AstraStorageResource *entry)
{
    if (entry->format_fd >= 0 || entry->partition_fd >= 0 ||
        entry->partition_stage != PARTITION_IDLE || entry->mounted.attached ||
        entry->mounted.root >= 0 || entry->mounted.directory || entry->mounted.loop >= 0 ||
        entry->mounted.backing >= 0 || entry->work.pid != 0) return;
    provider->ram_bytes -= entry->charged_bytes;
    provider->ram_inodes -= entry->charged_inodes;
    entry->charged_bytes = entry->charged_inodes = 0u;
}

/* Preserve the actual failed admission after successful rollback. A failed
 * rollback publishes retained cleanup and stays charged; ordinary retirement
 * publishes UNMOUNTED only after every real resource has gone. */
static int cleanup_mount_resource(AstraStorageProvider *provider, AstraStorageResource *entry,
    int preserve_failure)
{
    int error = astra_storage_linux_cleanup(&entry->mounted);
    if (error == 0) release_ram_charge(provider, entry);
    const AstraStorageRecord *record = astra_storage_find(&provider->catalog, entry->id);
    if (record != NULL && (!preserve_failure || error != 0) &&
        (record->state == ASTRA_VOLUME_FAILED || record->state == ASTRA_VOLUME_LOST ||
         record->state == ASTRA_VOLUME_QUIESCING))
        (void)astra_storage_unmount_result(&provider->catalog, record->id, record->generation,
            error, entry->mounted.attached, 0);
    return error;
}

int astra_storage_provider_open(AstraStorageProvider *provider, AstraStorageDomain *domain,
                                 size_t metadata_limit, size_t resource_limit,
                                 uint32_t probes, uint32_t timeout_ms, uint32_t workers,
                                 uint64_t ram_bytes, uint64_t ram_inodes)
{
    int error;
    if (provider == NULL) return EINVAL;
    if (provider->domain.path[0] != '\0' || provider->resources != NULL ||
        provider->catalog.entries != NULL)
        return EBUSY;
    if (domain == NULL || domain->directory < 0 || domain->path[0] == '\0' ||
        domain->uid == 0u || domain->gid == 0u ||
        probes == 0u || timeout_ms == 0u || workers == 0u ||
        resource_limit < sizeof(AstraStorageResource) || ram_bytes == 0u || ram_inodes == 0u)
        return EINVAL;
    *provider = (AstraStorageProvider){.domain = {.directory = -1}, .lease_signal = -1,
        .resource_limit = resource_limit, .probe_limit = probes, .worker_limit = workers,
        .probe_timeout_ms = timeout_ms, .ram_byte_limit = ram_bytes,
        .ram_inode_limit = ram_inodes};
    ssize_t random_bytes;
    do {
        random_bytes = getrandom(&provider->epoch, sizeof(provider->epoch), 0u);
    } while (random_bytes < 0 && errno == EINTR);
    if (random_bytes != sizeof(provider->epoch) || provider->epoch == 0u)
        return random_bytes < 0 ? errno : EIO;
    error = astra_storage_catalog_init(&provider->catalog, metadata_limit);
    if (error != 0)
        return error;
    error = astra_storage_domain_enter(domain);
    if (error != 0) {
        astra_storage_catalog_destroy(&provider->catalog);
        return error;
    }
    sigset_t signals;
    sigemptyset(&signals);
    sigaddset(&signals, SIGIO);
    sigaddset(&signals, SIGCHLD);
    if (sigprocmask(SIG_BLOCK, &signals, &provider->previous_signals) != 0)
        error = errno;
    else {
        provider->lease_signal = signalfd(-1, &signals, SFD_CLOEXEC | SFD_NONBLOCK);
        if (provider->lease_signal < 0) {
            error = errno;
            (void)sigprocmask(SIG_SETMASK, &provider->previous_signals, NULL);
        }
    }
    if (error != 0) {
        astra_storage_catalog_destroy(&provider->catalog);
    } else {
        provider->domain = *domain;
        *domain = (AstraStorageDomain){.directory = -1};
    }
    return error;
}

int astra_storage_provider_close(AstraStorageProvider *provider)
{
    int error;
    if (provider == NULL)
        return EINVAL;
    if (provider->resources != NULL || provider->probes != 0u || provider->workers != 0u)
        return EBUSY;
    error = astra_storage_domain_close(&provider->domain);
    if (error == 0) {
        astra_storage_catalog_destroy(&provider->catalog);
        struct signalfd_siginfo signal;
        while (read(provider->lease_signal, &signal, sizeof(signal)) == sizeof(signal)) {}
        close(provider->lease_signal);
        provider->lease_signal = -1;
        (void)sigprocmask(SIG_SETMASK, &provider->previous_signals, NULL);
    }
    return error;
}

int astra_storage_provider_export(AstraStorageProvider *provider, int root,
                                   const char *name, uint32_t flags, uint64_t *id)
{
    struct statx info;
    uint64_t binding;
    int error;
    int fd = fcntl(root, F_DUPFD_CLOEXEC, 0);
    if (fd < 0)
        return errno;
    if (statx(fd, "", AT_EMPTY_PATH, STATX_TYPE | STATX_MNT_ID, &info) != 0) {
        error = errno;
        close(fd);
        return error;
    }
    if (!S_ISDIR(info.stx_mode) || !(info.stx_mask & STATX_MNT_ID)) {
        close(fd);
        return ENOTDIR;
    }
    error = astra_storage_binding(&provider->catalog, 0u, 0u, 0u,
                                   name, flags, &binding);
    if (error == 0) {
        AstraStorageResource *entry = reserve(provider, binding, 1u);
        if (entry == NULL) {
            (void)astra_storage_mount_result(&provider->catalog, binding, 1u, 0u, ENOSPC);
            (void)astra_storage_forget(&provider->catalog, binding);
            error = ENOSPC;
        } else {
            *id = binding;
            error = astra_storage_linux_bind(&provider->domain,
                astra_storage_find(&provider->catalog, binding), fd, &entry->mounted);
            close(fd);
            return astra_storage_mount_result(&provider->catalog, binding, 1u,
                entry->mounted.linux_mount_id, error);
        }
    }
    close(fd);
    return error;
}

static int start_probe(AstraStorageProvider *provider, AstraStorageResource *entry)
{
    char path[64];
    int error;
    (void)snprintf(path, sizeof(path), "/proc/self/fd/%d", entry->source);
    int probe_fd = open(path, O_RDONLY | O_CLOEXEC);
    error = probe_fd < 0 ? errno : astra_storage_probe_start(&entry->probe, probe_fd,
        entry->id, entry->generation, provider->domain.uid, provider->domain.gid,
        provider->probe_timeout_ms, entry->offset, entry->length);
    if (probe_fd >= 0)
        close(probe_fd);
    if (error == 0)
        ++provider->probes;
    return error;
}

int astra_storage_provider_source(AstraStorageProvider *provider, uint64_t source,
                                   uint64_t generation, int fd,
                                   uint64_t offset, uint64_t length)
{
    const AstraStorageRecord *record = astra_storage_find(&provider->catalog, source);
    AstraStorageResource *entry;
    struct stat info;
    int error;
    if (record == NULL || record->generation != generation || record->state == ASTRA_VOLUME_LOST)
        return ESTALE;
    if (record->kind != ASTRA_VOLUME_SOURCE || record->state != ASTRA_VOLUME_PROBING ||
        resource(provider, source) != NULL)
        return EINVAL;
    if (fstat(fd, &info) != 0) {
        error = errno;
        goto failed;
    }
    if (S_ISREG(info.st_mode)) {
        if (info.st_size <= 0 || length == 0u || offset > (uint64_t)info.st_size ||
            length > (uint64_t)info.st_size - offset) {
            error = EINVAL;
            goto failed;
        }
    } else if (!S_ISBLK(info.st_mode) || offset != 0u || length != 0u) {
        error = EINVAL;
        goto failed;
    }
    entry = reserve(provider, source, generation);
    if (entry == NULL) {
        error = ENOSPC;
        goto failed;
    }
    entry->source = fcntl(fd, F_DUPFD_CLOEXEC, 0);
    if (entry->source < 0) {
        error = errno;
        remove_resource(provider, entry);
        goto failed;
    }
    entry->offset = offset;
    entry->length = length;
    return 0;
failed:
    (void)astra_storage_probe_result(&provider->catalog, source, generation,
        ASTRA_VOLUME_FORMAT_UNKNOWN, "", "", "", 0, error);
    return error;
}

int astra_storage_provider_poll(AstraStorageProvider *provider)
{
    int completed = 0;
    struct signalfd_siginfo signal;
    while (read(provider->lease_signal, &signal, sizeof(signal)) == sizeof(signal)) {}
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return -errno;
    uint64_t current_ns = (uint64_t)now.tv_sec * UINT64_C(1000000000) +
        (uint64_t)now.tv_nsec;
    for (AstraStorageResource *entry = provider->resources; entry != NULL;
         entry = entry->next)
        if (entry->work.pid != 0 &&
            (entry->work.operation == ASTRA_STORAGE_WORK_FORMAT ||
             entry->work.operation == ASTRA_STORAGE_WORK_PARTITION) &&
            entry->work.cancelled == 0 && current_ns >= entry->work.deadline_ns) {
            astra_storage_work_cancel(&entry->work);
            entry->work.cancelled = ETIMEDOUT;
        }
    for (AstraStorageResource *entry = provider->resources; entry != NULL; entry = entry->next) {
        const AstraStorageRecord *record = astra_storage_find(&provider->catalog, entry->id);
        if (record != NULL && record->kind == ASTRA_VOLUME_SOURCE &&
            (record->origin == ASTRA_VOLUME_ORIGIN_IMAGE || record->origin == ASTRA_VOLUME_ORIGIN_SYSTEM_IMAGE) &&
            record->state != ASTRA_VOLUME_LOST && entry->source >= 0 &&
            fcntl(entry->selection_backing >= 0 ? entry->selection_backing : entry->source, F_GETLEASE) !=
                (record->origin == ASTRA_VOLUME_ORIGIN_SYSTEM_IMAGE ? F_WRLCK : F_RDLCK)) {
            int error = astra_storage_provider_loss(provider, record->id, record->generation);
            if (error != 0)
                return -error;
            ++completed;
        }
    }
    for (AstraStorageResource *entry = provider->resources; entry != NULL; entry = entry->next) {
        if (entry->work.pid == 0)
            continue;
        if (entry->work.operation == ASTRA_STORAGE_WORK_FORMAT) {
            uint64_t generation = entry->work.generation;
            int result = 0;
            int ready = astra_storage_work_edit_poll(&entry->work, &result);
            if (ready < 0) return ready;
            if (ready == 0) continue;
            --provider->workers;
            close(entry->format_fd);
            entry->format_fd = -1;
            entry->format_generation = generation;
            entry->format_error = result;
            const AstraStorageRecord *source = astra_storage_find(&provider->catalog, entry->id);
            if (source != NULL && source->generation == generation &&
                source->state == ASTRA_VOLUME_FORMATTING) {
                int error = astra_storage_format_complete(&provider->catalog,
                    entry->id, generation);
                if (error != 0) return -error;
                entry->generation = generation + 1u;
                astra_storage_discovery_format_result(provider->discovery,
                    entry->id, result == 0);
            } else if (result == 0)
                entry->format_error = ESTALE;
            ++completed;
            continue;
        }
        if (entry->work.operation == ASTRA_STORAGE_WORK_PARTITION) {
            int result = 0;
            int ready = astra_storage_work_edit_poll(&entry->work, &result);
            if (ready < 0) return ready;
            if (ready == 0) continue;
            --provider->workers;
            close(entry->partition_fd);
            entry->partition_fd = -1;
            entry->partition_tool_error = result;
            if (result != 0)
                fprintf(stderr, "astra-storage partition worker id=%" PRIu64
                    " generation=%" PRIu64 " error=%d\n",
                    entry->id, entry->partition_generation, result);
            entry->partition_stage = PARTITION_RETIRE;
            entry->partition_deadline_ns = current_ns + UINT64_C(5000000000);
            ++completed;
            continue;
        }
        if (entry->work.operation == ASTRA_STORAGE_WORK_PATH) {
            AstraStoragePathReply result;
            int fd = -1;
            uint64_t generation = entry->work.generation;
            int ready = astra_storage_work_path_poll(&entry->work, &result, &fd);
            if (ready < 0)
                return ready;
            if (ready == 0)
                continue;
            --provider->workers;
            const AstraStorageRecord *mounted = astra_storage_find(&provider->catalog, entry->id);
            if (mounted == NULL || mounted->generation != generation ||
                (mounted->state != ASTRA_VOLUME_MOUNTED && mounted->state != ASTRA_VOLUME_QUIESCING)) {
                result.error = ESTALE;
                if (fd >= 0) close(fd);
                fd = -1;
            }
            int sent = entry->path_socket < 0 ? 0 :
                astra_storage_path_reply(entry->path_socket, &result, fd);
            entry->path_socket = -1;
            if (fd >= 0) close(fd);
            if (sent != 0)
                return -sent;
            ++completed;
            continue;
        }
        AstraStorageWorkReply result;
        int root = -1;
        int ready = astra_storage_work_poll(&entry->work, &result, &root);
        if (ready < 0)
            return ready;
        if (ready == 0)
            continue;
        --provider->workers;
        if (root >= 0)
            close(root);
        if (result.operation == ASTRA_STORAGE_WORK_EJECT) {
            (void)astra_storage_eject_result(&provider->catalog, entry->id,
                result.generation, result.error);
            ++completed;
            continue;
        }
        int actual = astra_storage_linux_reconcile(&provider->domain, &entry->mounted);
        const AstraStorageRecord *record = astra_storage_find(&provider->catalog, entry->id);
        if (result.operation == ASTRA_STORAGE_WORK_ATTACH) {
            int error = actual != 0 ? actual : result.error;
            if (error == 0 && !entry->mounted.attached)
                error = EPROTO;
            (void)astra_storage_mount_result(&provider->catalog, entry->id,
                result.generation, entry->mounted.linux_mount_id, error);
            if (!entry->mounted.attached) {
                (void)cleanup_mount_resource(provider, entry, 1);
            }
        } else {
            if (actual == 0 && !entry->mounted.attached)
                (void)cleanup_mount_resource(provider, entry, 0);
            else {
                int error = actual != 0 ? actual : result.error != 0 ? result.error : EPROTO;
                (void)astra_storage_unmount_result(&provider->catalog, entry->id,
                    record->generation, error, entry->mounted.attached,
                    actual == 0 && entry->mounted.attached && entry->mounted.root >= 0);
            }
        }
        ++completed;
    }
    for (AstraStorageResource *entry = provider->resources; entry != NULL; entry = entry->next) {
        AstraStorageProbeResult result;
        if (entry->probe.pid == 0) {
            const AstraStorageRecord *record = astra_storage_find(&provider->catalog, entry->id);
            if (entry->source >= 0 && record->state == ASTRA_VOLUME_PROBING &&
                provider->probes < provider->probe_limit) {
                int error = start_probe(provider, entry);
                if (error != 0) {
                    (void)astra_storage_probe_result(&provider->catalog, entry->id, entry->generation,
                        ASTRA_VOLUME_FORMAT_UNKNOWN, "", "", "", 0, error);
                    ++completed;
                }
            }
            continue;
        }
        int ready = astra_storage_probe_poll(&entry->probe, &result);
        if (ready < 0)
            return ready;
        if (ready == 0)
            continue;
        --provider->probes;
        int error = astra_storage_probe_result(&provider->catalog, result.source,
            result.generation, result.format, result.uuid, result.label,
            result.detected_type,
            result.ambiguous, result.error);
        if (error != 0 && error != ESTALE)
            return -error;
        ++completed;
    }
    for (AstraStorageResource *entry = provider->resources; entry != NULL;
         entry = entry->next) {
        if (entry->partition_stage == PARTITION_IDLE)
            continue;
        const AstraStorageRecord *device = astra_storage_find(&provider->catalog, entry->id);
        if (device == NULL || device->generation != entry->partition_generation ||
            device->state == ASTRA_VOLUME_LOST) {
            entry->partition_error = ESTALE;
            entry->partition_stage = PARTITION_IDLE;
            ++completed;
            continue;
        }
        if (entry->partition_stage == PARTITION_RETIRE) {
            int error = astra_storage_discovery_partition_retire(provider->discovery,
                entry->id);
            if (error != 0) {
                if (current_ns >= entry->partition_deadline_ns)
                    entry->partition_error = error;
                continue;
            }
            entry->partition_stage = PARTITION_REREAD;
            entry->partition_deadline_ns = current_ns + UINT64_C(5000000000);
            ++completed;
        }
        int pending = 0;
        for (AstraStorageResource *child = provider->resources; child != NULL;
             child = child->next)
            if (child != entry && under_device(&provider->catalog, child->id, entry->id)) {
                pending = 1;
                break;
            }
        if (pending)
            continue; /* Reaper closes old partition descriptions next. */
        int error = astra_storage_discovery_partition_reread(provider->discovery,
            entry->id, entry->partition_generation);
        if (error == EBUSY || error == EAGAIN) {
            if (current_ns >= entry->partition_deadline_ns &&
                entry->partition_error == EAGAIN) {
                fprintf(stderr, "astra-storage partition reread id=%" PRIu64
                    " generation=%" PRIu64 " error=%d\n",
                    entry->id, entry->partition_generation, error);
                entry->partition_error = error;
            }
            continue;
        }
        if (error == ESTALE || error == ENODEV) {
            (void)astra_storage_provider_loss(provider, entry->id,
                entry->partition_generation);
            entry->partition_error = error;
            entry->partition_stage = PARTITION_IDLE;
            ++completed;
            continue;
        }
        if (error != 0) {
            /* Keep status pending while the retained reread fence retries. */
            if (current_ns >= entry->partition_deadline_ns &&
                entry->partition_error == EAGAIN) {
                fprintf(stderr, "astra-storage partition reread id=%" PRIu64
                    " generation=%" PRIu64 " error=%d\n",
                    entry->id, entry->partition_generation, error);
                entry->partition_error = error;
            }
            continue; /* Retain fence; a later reread may recover. */
        }
        error = astra_storage_partition_complete(&provider->catalog, entry->id,
            entry->partition_generation, entry->partition_tool_error);
        if (error != 0) {
            const AstraStorageRecord *failed = astra_storage_find(&provider->catalog, entry->id);
            fprintf(stderr, "astra-storage partition completion id=%" PRIu64
                " generation=%" PRIu64 " state=%u tool=%d error=%d\n",
                entry->id, entry->partition_generation,
                failed == NULL ? 0u : failed->state,
                entry->partition_tool_error, error);
            return -error;
        }
        entry->generation = entry->partition_generation + 1u;
        entry->partition_error = entry->partition_tool_error;
        entry->partition_stage = PARTITION_IDLE;
        ++completed;
    }
    return completed;
}

int astra_storage_provider_image(AstraStorageProvider *provider, uint64_t mount,
                                  uint64_t generation, uint64_t backing_object,
                                  int file, uint64_t *source)
{
    const AstraStorageRecord *parent = astra_storage_find(&provider->catalog, mount);
    struct statx info;
    int access = fcntl(file, F_GETFL);
    int error;
    if (source == NULL)
        return EINVAL;
    *source = 0u;
    if (parent == NULL || parent->generation != generation)
        return ESTALE;
    if (access < 0)
        return errno;
    if ((access & O_ACCMODE) != O_RDONLY ||
        statx(file, "", AT_EMPTY_PATH, STATX_TYPE | STATX_SIZE | STATX_INO | STATX_MNT_ID, &info) != 0)
        return EACCES;
    if (!S_ISREG(info.stx_mode) || info.stx_size == 0u ||
        !(info.stx_mask & STATX_MNT_ID) || info.stx_mnt_id != parent->linux_mount_id)
        return EXDEV;
    if ((info.stx_mask & (STATX_TYPE | STATX_SIZE | STATX_INO)) !=
        (STATX_TYPE | STATX_SIZE | STATX_INO) || info.stx_ino == 0u)
        return EIO;
    if (backing_object != info.stx_ino) return EINVAL;
    for (AstraStorageResource *entry = provider->resources; entry != NULL; entry = entry->next) {
        const AstraStorageRecord *record = astra_storage_find(&provider->catalog, entry->id);
        struct statx held;
        if (entry->source >= 0 && record->origin == ASTRA_VOLUME_ORIGIN_IMAGE &&
            statx(entry->source, "", AT_EMPTY_PATH, STATX_INO | STATX_MNT_ID, &held) == 0 &&
            held.stx_mnt_id == info.stx_mnt_id && held.stx_ino == info.stx_ino)
            return EBUSY;
    }
    /* Existing writers/mappings refuse the lease; new outside writers notify
     * the daemon, which withdraws image authority before relinquishing it. */
    if (fcntl(file, F_SETOWN, getpid()) != 0 || fcntl(file, F_SETLEASE, F_RDLCK) != 0)
        return errno;
    error = astra_storage_image_source(&provider->catalog, mount, generation,
                                        backing_object, source);
    if (error == 0)
        error = astra_storage_provider_source(provider, *source, 1u, file, 0u, info.stx_size);
    if (error != 0) {
        (void)fcntl(file, F_SETLEASE, F_UNLCK);
        if (*source != 0u) {
            (void)astra_storage_loss(&provider->catalog, *source, 1u);
            (void)astra_storage_forget(&provider->catalog, *source);
        }
    }
    return error;
}

int astra_storage_provider_system(AstraStorageProvider *provider, int file,
                                   uint64_t offset, uint64_t length, uint64_t *source)
{
    int access = fcntl(file, F_GETFL);
    int error;
    if (source == NULL)
        return EINVAL;
    *source = 0u;
    if (access < 0)
        return errno;
    if ((access & O_ACCMODE) != O_RDWR)
        return EACCES;
    if (provider->system_source != 0u)
        return EBUSY;
    error = astra_storage_system_source(&provider->catalog, source);
    if (error != 0)
        return error;
    provider->system_source = *source;
    AstraStorageResource *entry = reserve(provider, *source, 1u);
    if (entry == NULL) {
        error = ENOSPC;
        goto failed;
    }
    entry->selection_backing = fcntl(file, F_DUPFD_CLOEXEC, 0);
    if (entry->selection_backing < 0) {
        error = errno;
        goto failed;
    }
    /* The loop retains this exact open description. Its flock excludes a
     * replacement launcher until the actual last kernel backing reference
     * retires, including when the original service dies. The lease also
     * blocks/confirms loss on outside opens while the service is alive. */
    if (flock(entry->selection_backing, LOCK_EX | LOCK_NB) != 0 ||
        fcntl(entry->selection_backing, F_SETOWN, getpid()) != 0 ||
        fcntl(entry->selection_backing, F_SETLEASE, F_WRLCK) != 0) {
        error = errno;
        goto failed;
    }
    entry->selection_leased = 1;
    entry->source = astra_storage_linux_loop(entry->selection_backing, offset, length, 0);
    if (entry->source < 0) {
        error = -entry->source;
        entry->source = -1;
        goto failed;
    }
    /* Probe the exact prepared block window without opening the leased image. */
    return 0;
failed:
    (void)astra_storage_probe_result(&provider->catalog, *source, 1u,
        ASTRA_VOLUME_FORMAT_UNKNOWN, "", "", "", 0, error);
    return error;
}

int astra_storage_provider_mount(AstraStorageProvider *provider, uint64_t source,
                                  uint64_t generation, uint32_t flags,
                                  uint64_t bytes, uint64_t inodes, uint64_t *mount)
{
    const AstraStorageRecord *record = astra_storage_find(&provider->catalog, source);
    AstraStorageResource *backing = resource(provider, source);
    AstraStorageResource *entry;
    int error;
    if (record == NULL || record->generation != generation)
        return ESTALE;
    if (provider->workers >= provider->worker_limit)
        return EAGAIN;
    if (record->class_id == ASTRA_VOLUME_RAM) {
        if (bytes > provider->ram_byte_limit - provider->ram_bytes ||
            inodes > provider->ram_inode_limit - provider->ram_inodes)
            return ENOSPC;
    } else if (backing == NULL || backing->generation != generation || backing->probe.pid != 0)
        return EAGAIN;
    if (provider->resource_limit - provider->resource_bytes < sizeof(*entry))
        return ENOSPC;
    /* Keep the latest failed admission inspectable, but do not accumulate
     * an unbounded history on each retry. Retained cleanup remains fenced. */
    for (AstraStorageResource *old = provider->resources, *next; old != NULL; old = next) {
        next = old->next;
        const AstraStorageRecord *failed = astra_storage_find(&provider->catalog, old->id);
        if (failed != NULL && failed->kind == ASTRA_VOLUME_MOUNT && failed->parent == source &&
            failed->state == ASTRA_VOLUME_FAILED &&
            (failed->flags & ASTRA_VOLUME_CLEANUP_PENDING) == 0u) {
            error = astra_storage_provider_release(provider, failed->id);
            if (error != 0) return error;
        }
    }
    error = astra_storage_mount_begin(&provider->catalog, source, generation,
                                       flags, bytes, inodes, mount);
    if (error != 0)
        return error;
    entry = reserve(provider, *mount, 1u);
    if (entry == NULL) {
        (void)astra_storage_mount_result(&provider->catalog, *mount, 1u, 0u, ENOSPC);
        (void)astra_storage_forget(&provider->catalog, *mount);
        return ENOSPC;
    }
    provider->ram_bytes += bytes;
    provider->ram_inodes += inodes;
    entry->charged_bytes = bytes;
    entry->charged_inodes = inodes;
    record = astra_storage_find(&provider->catalog, *mount);
    error = astra_storage_linux_prepare(&provider->domain, record,
        backing == NULL ? -1 : backing->source,
        backing == NULL ? 0u : backing->offset,
        backing == NULL ? 0u : backing->length, &entry->mounted);
    if (error == 0)
        error = astra_storage_work_start(&entry->work, &provider->domain, record,
            backing == NULL ? -1 : backing->source, &entry->mounted, ASTRA_STORAGE_WORK_ATTACH);
    if (error != 0) {
        int admitted = astra_storage_mount_result(&provider->catalog, *mount, 1u,
            entry->mounted.linux_mount_id, error);
        int cleanup = cleanup_mount_resource(provider, entry, 1);
        if (cleanup == 0) (void)astra_storage_provider_release(provider, *mount);
        return cleanup != 0 ? cleanup : admitted;
    }
    ++provider->workers;
    return 0;
}

int astra_storage_provider_root(AstraStorageProvider *provider, uint64_t id,
                                 uint64_t generation, int *root)
{
    const AstraStorageRecord *record = astra_storage_find(&provider->catalog, id);
    AstraStorageResource *entry = resource(provider, id);
    *root = -1;
    if (record == NULL || record->generation != generation || record->state == ASTRA_VOLUME_LOST ||
        record->state == ASTRA_VOLUME_UNMOUNTED || record->state == ASTRA_VOLUME_FAILED)
        return ESTALE;
    if ((record->kind != ASTRA_VOLUME_MOUNT && record->kind != ASTRA_VOLUME_BINDING) ||
        record->state != ASTRA_VOLUME_MOUNTED || entry == NULL || entry->mounted.root < 0)
        return EBUSY;
    *root = fcntl(entry->mounted.root, F_DUPFD_CLOEXEC, 0);
    return *root < 0 ? errno : 0;
}

int astra_storage_provider_path(AstraStorageProvider *provider, int socket,
                                 pid_t peer, uid_t uid, gid_t gid)
{
    AstraStoragePathRequest request;
    AstraStoragePathReply reply;
    int fds[3];
    int result = -1;
    int changed = astra_storage_provider_poll(provider);
    if (changed < 0)
        return -changed;
    int error = astra_storage_path_receive(socket, peer, uid, gid, &request, fds);
    if (error == EPIPE)
        return error;
    reply = (AstraStoragePathReply){.version = ASTRA_STORAGE_PROVIDER_VERSION,
        .serial = request.serial, .epoch = request.epoch, .error = error};
    if (error == 0) {
        const AstraStorageRecord *record = astra_storage_find(&provider->catalog, request.mount);
        AstraStorageResource *entry = resource(provider, request.mount);
        if (request.epoch != provider->epoch || record == NULL || record->generation != request.generation ||
            record->state == ASTRA_VOLUME_LOST)
            reply.error = ESTALE;
        else if (record->state != ASTRA_VOLUME_MOUNTED || entry == NULL || entry->mounted.root < 0)
            reply.error = EBUSY;
        else {
            int confined = astra_storage_path_roots(entry->mounted.root, fds);
            if (confined != 0)
                reply.error = -confined;
            if (reply.error == 0 && request.operation == ASTRA_STORAGE_PATH_OPEN &&
                (request.flags & (O_ACCMODE | O_TRUNC)) != 0u) {
                AstraResolution resolved = {.parent = -1};
                int target = astra_resolve_open_path(fds[0], fds[1] < 0 ? fds[0] : fds[1],
                    request.path, O_PATH | O_CLOEXEC, 0u,
                    request.resolve, request.links, &resolved);
                if (target >= 0) {
                    struct statx info;
                    if (statx(target, "", AT_EMPTY_PATH, STATX_INO | STATX_MNT_ID, &info) != 0)
                        reply.error = errno;
                    else
                        for (AstraStorageResource *held = provider->resources;
                             held != NULL; held = held->next) {
                            const AstraStorageRecord *image = astra_storage_find(&provider->catalog, held->id);
                            struct statx other;
                            if (image->kind == ASTRA_VOLUME_SOURCE &&
                                image->origin == ASTRA_VOLUME_ORIGIN_IMAGE && held->source >= 0 &&
                                statx(held->source, "", AT_EMPTY_PATH, STATX_INO | STATX_MNT_ID, &other) == 0 &&
                                other.stx_ino == info.stx_ino && other.stx_mnt_id == info.stx_mnt_id)
                                reply.error = (request.flags & (O_CREAT | O_EXCL)) ==
                                    (O_CREAT | O_EXCL) ? EEXIST : EBUSY;
                        }
                    close(target);
                }
            }
            if (reply.error == 0) {
                if (entry->work.pid != 0 || provider->workers >= provider->worker_limit)
                    reply.error = EAGAIN;
                else {
                    reply.error = astra_storage_work_path_start(&entry->work, record,
                        entry->mounted.root, &request, fds);
                    if (reply.error == 0) {
                        ++provider->workers;
                        entry->path_socket = socket;
                    }
                }
            }
        }
    }
    error = reply.error == 0 ? 0 : astra_storage_path_reply(socket, &reply, result);
    if (result >= 0)
        close(result);
    for (unsigned at = 0; at < 3u; ++at)
        if (fds[at] >= 0)
            close(fds[at]);
    return error;
}

int astra_storage_provider_control(AstraStorageProvider *provider, int socket,
                                    pid_t peer, uid_t uid, gid_t gid)
{
    AstraStorageControlRequest request = {0};
    AstraStorageControlReply reply = {.version = ASTRA_STORAGE_PROVIDER_VERSION};
    struct ucred credentials;
    int fds[ASTRA_STORAGE_PROVIDER_FDS];
    int root = -1;
    unsigned count = 0u;
    int changed = astra_storage_provider_poll(provider);
    if (changed < 0)
        return -changed;
    int error = astra_storage_receive_packet(socket, &request, sizeof(request),
                                              fds, &count, &credentials);
    if (error == EPIPE)
        return error;
    reply.serial = request.serial;
    reply.epoch = provider->epoch;
    if (error == 0) {
        if (credentials.pid != peer || credentials.uid != uid || credentials.gid != gid)
            error = EACCES;
        else if ((request.epoch != provider->epoch &&
                  !(request.operation == ASTRA_STORAGE_CONTROL_SNAPSHOT && request.epoch == 0u)))
            error = ESTALE;
        else if (count != (unsigned)(request.operation == ASTRA_STORAGE_CONTROL_IMAGE) ||
                 request.version != ASTRA_STORAGE_PROVIDER_VERSION ||
                 request.serial == 0u || request.reserved != 0u)
            error = EPROTO;
        else if (request.operation != ASTRA_STORAGE_CONTROL_FORMAT &&
                 (request.format != 0u ||
                  memcmp(request.label, (char[sizeof(request.label)]){0},
                         sizeof(request.label)) != 0))
            error = EPROTO;
        else if (request.operation != ASTRA_STORAGE_CONTROL_PARTITION &&
                 memcmp(&request.edit, (AstraVolumePartitionEdit[1]){{0}},
                        sizeof(request.edit)) != 0)
            error = EPROTO;
    }
    if (error == 0) {
        const AstraStorageRecord *record;
        switch (request.operation) {
        case ASTRA_STORAGE_CONTROL_SNAPSHOT: {
            size_t rows;
            if (request.target || request.generation || request.flags ||
                request.byte_limit || request.inode_limit || request.backing_object) {
                error = EINVAL;
                break;
            }
            error = astra_storage_snapshot(&provider->catalog, request.sequence,
                request.after, reply.records, ASTRA_STORAGE_PROVIDER_PAGE,
                &rows, &reply.next, &reply.sequence);
            reply.count = (uint32_t)rows;
            break;
        }
        case ASTRA_STORAGE_CONTROL_ROOT:
            if (request.flags || request.byte_limit || request.inode_limit ||
                request.sequence || request.after || request.backing_object)
                error = EINVAL;
            else
                error = astra_storage_provider_root(provider, request.target,
                                                     request.generation, &root);
            break;
        case ASTRA_STORAGE_CONTROL_MOUNT:
            if (request.sequence || request.after || request.backing_object ||
                (request.flags & ASTRA_VOLUME_SYSTEM))
                error = EACCES; /* System selection belongs to the launcher. */
            else
                error = astra_storage_provider_mount(provider, request.target,
                    request.generation, request.flags, request.byte_limit,
                    request.inode_limit, &reply.object);
            break;
        case ASTRA_STORAGE_CONTROL_QUIESCE:
        case ASTRA_STORAGE_CONTROL_RESUME:
        case ASTRA_STORAGE_CONTROL_UNMOUNT:
            if (request.flags || request.byte_limit || request.inode_limit ||
                request.sequence || request.after || request.backing_object) {
                error = EINVAL;
                break;
            }
            record = astra_storage_find(&provider->catalog, request.target);
            if (request.operation == ASTRA_STORAGE_CONTROL_QUIESCE)
                error = astra_storage_quiesce(&provider->catalog, request.target,
                                               request.generation);
            else if (request.operation == ASTRA_STORAGE_CONTROL_RESUME) {
                AstraStorageResource *entry = resource(provider, request.target);
                if (record == NULL || record->generation != request.generation)
                    error = ESTALE;
                else if (record->state != ASTRA_VOLUME_QUIESCING || entry == NULL ||
                         !entry->mounted.attached || entry->mounted.root < 0)
                    error = EINVAL;
                else {
                    error = astra_storage_unmount_result(&provider->catalog,
                        request.target, request.generation, EBUSY, 1, 1);
                    if (error == EBUSY)
                        error = 0;
                }
            } else
                error = astra_storage_provider_unmount(provider, request.target,
                                                       request.generation);
            break;
        case ASTRA_STORAGE_CONTROL_IMAGE:
            if (request.flags || request.byte_limit || request.inode_limit ||
                request.sequence || request.after || request.backing_object == 0u)
                error = EINVAL;
            else
                error = astra_storage_provider_image(provider, request.target,
                    request.generation, request.backing_object, fds[0], &reply.object);
            break;
        case ASTRA_STORAGE_CONTROL_EJECT:
            if (request.flags || request.byte_limit || request.inode_limit ||
                request.sequence || request.after || request.backing_object)
                error = EINVAL;
            else
                error = astra_storage_provider_eject(provider, request.target, request.generation);
            break;
        case ASTRA_STORAGE_CONTROL_RELEASE_IMAGE_SOURCE:
            if (request.flags || request.byte_limit || request.inode_limit ||
                request.sequence || request.after || request.backing_object)
                error = EINVAL;
            else
                error = astra_storage_provider_release_image(provider, request.target, request.generation);
            break;
        case ASTRA_STORAGE_CONTROL_BACKING:
            if (request.flags || request.byte_limit || request.inode_limit ||
                request.sequence || request.after || request.backing_object)
                error = EINVAL;
            else
                error = astra_storage_provider_backing(provider, request.target, request.generation, &root);
            break;
        case ASTRA_STORAGE_CONTROL_FORMAT: {
            const char *end = memchr(request.label, '\0', sizeof(request.label));
            if (request.flags || request.byte_limit || request.inode_limit ||
                request.sequence || request.after || request.backing_object ||
                end == NULL ||
                memcmp(end, (char[sizeof(request.label)]){0},
                       sizeof(request.label) - (size_t)(end - request.label)) != 0)
                error = EINVAL;
            else
                error = astra_storage_provider_format(provider, request.target,
                    request.generation, request.format, request.label);
            break;
        }
        case ASTRA_STORAGE_CONTROL_FORMAT_STATUS:
            if (request.flags || request.byte_limit || request.inode_limit ||
                request.sequence || request.after || request.backing_object)
                error = EINVAL;
            else
                error = astra_storage_provider_format_status(provider, request.target,
                    request.generation, &reply.operation_error);
            break;
        case ASTRA_STORAGE_CONTROL_PARTITION:
            if (request.flags || request.byte_limit || request.inode_limit ||
                request.sequence || request.after || request.backing_object)
                error = EINVAL;
            else
                error = astra_storage_provider_partition(provider, request.target,
                    request.generation, &request.edit);
            break;
        case ASTRA_STORAGE_CONTROL_PARTITION_STATUS:
            if (request.flags || request.byte_limit || request.inode_limit ||
                request.sequence || request.after || request.backing_object)
                error = EINVAL;
            else
                error = astra_storage_provider_partition_status(provider, request.target,
                    request.generation, &reply.operation_error);
            break;
        default:
            error = ENOTSUP;
            break;
        }
    }
    for (unsigned at = 0u; at < count; ++at)
        if (fds[at] >= 0)
            close(fds[at]);
    if (error == 0 && (request.operation == ASTRA_STORAGE_CONTROL_MOUNT ||
        request.operation == ASTRA_STORAGE_CONTROL_IMAGE)) {
        const AstraStorageRecord *admitted = astra_storage_find(&provider->catalog, reply.object);
        if (admitted == NULL) error = EPROTO;
        else { reply.records[0] = *admitted; reply.count = 1u; }
    }
    reply.error = error;
    reply.sequence = provider->catalog.sequence;
    error = astra_storage_send_packet(socket, &reply, sizeof(reply), &root, root < 0 ? 0u : 1u);
    if (root >= 0)
        close(root);
    return error;
}

static int next_storage_timeout(AstraStorageProvider *provider)
{
    uint64_t deadline = UINT64_MAX;
    struct timespec now;
    for (AstraStorageResource *entry = provider->resources; entry != NULL; entry = entry->next)
        if (entry->probe.pid != 0 && entry->probe.cancelled == 0 && entry->probe.deadline_ns < deadline)
            deadline = entry->probe.deadline_ns;
    for (AstraStorageResource *entry = provider->resources; entry != NULL; entry = entry->next)
        if (entry->work.pid != 0 &&
            (entry->work.operation == ASTRA_STORAGE_WORK_FORMAT ||
             entry->work.operation == ASTRA_STORAGE_WORK_PARTITION) &&
            entry->work.cancelled == 0 && entry->work.deadline_ns < deadline)
            deadline = entry->work.deadline_ns;
    int partition_pending = 0;
    for (AstraStorageResource *entry = provider->resources; entry != NULL; entry = entry->next)
        if (entry->partition_stage != PARTITION_IDLE)
            partition_pending = 1;
    if (deadline == UINT64_MAX)
        return partition_pending ? 100 : -1;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return 0; /* poll reports the clock error and retires the probe. */
    uint64_t current = (uint64_t)now.tv_sec * UINT64_C(1000000000) + (uint64_t)now.tv_nsec;
    if (deadline <= current)
        return 0;
    uint64_t milliseconds = (deadline - current + 999999u) / 1000000u;
    if (partition_pending && milliseconds > 100u)
        return 100;
    return milliseconds > INT_MAX ? INT_MAX : (int)milliseconds;
}

static int retired(AstraStorageProvider *provider, const AstraStorageRecord *record)
{
    if ((record->kind == ASTRA_VOLUME_MOUNT || record->kind == ASTRA_VOLUME_BINDING) &&
        (record->state == ASTRA_VOLUME_UNMOUNTED || (record->state == ASTRA_VOLUME_FAILED &&
         (record->flags & ASTRA_VOLUME_CLEANUP_PENDING) != 0u))) return 1;
    while (record != NULL) {
        if (record->state == ASTRA_VOLUME_LOST)
            return 1;
        if (record->parent == 0u)
            return 0;
        const AstraStorageRecord *parent = astra_storage_find(&provider->catalog, record->parent);
        if (parent == NULL || parent->generation != record->parent_generation)
            return 1;
        record = parent;
    }
    return 0;
}

int astra_storage_provider_reap(AstraStorageProvider *provider)
{
    AstraStorageRecord page[ASTRA_STORAGE_PROVIDER_PAGE];
    uint64_t sequence = 0u, after = 0u, next, current;
    size_t count;
    int pending = 0;
    do {
        int error = astra_storage_snapshot(&provider->catalog, sequence, after, page,
            ASTRA_STORAGE_PROVIDER_PAGE, &count, &next, &current);
        if (error != 0)
            return error;
        for (size_t at = count; at != 0u; --at) {
            const AstraStorageRecord *record = astra_storage_find(&provider->catalog, page[at - 1u].id);
            if (record == NULL || !retired(provider, record))
                continue;
            uint64_t id = record->id;
            if (record->kind == ASTRA_VOLUME_MOUNT || record->kind == ASTRA_VOLUME_BINDING) {
                error = astra_storage_provider_unmount(provider, id, record->generation);
                if (error != 0) {
                    pending = 1;
                    continue;
                }
            }
            error = astra_storage_provider_release(provider, id);
            if (error != 0) pending = 1;
        }
        after = next;
        sequence = provider->catalog.sequence;
    } while (next != 0u);
    return pending ? EAGAIN : 0;
}

int astra_storage_provider_wait(AstraStorageProvider *provider, int timeout_ms)
{
    if (provider == NULL || provider->lease_signal < 0 || timeout_ms < 0)
        return EINVAL;
    int deadline = next_storage_timeout(provider);
    if (deadline >= 0 && deadline < timeout_ms)
        timeout_ms = deadline;
    struct pollfd event = {provider->lease_signal, POLLIN, 0};
    if (poll(&event, 1u, timeout_ms) < 0)
        return errno;
    return (event.revents & (POLLERR | POLLHUP | POLLNVAL)) ? EIO : 0;
}

int astra_storage_provider_serve(AstraStorageProvider *provider, int control, int path,
                                  pid_t peer)
{
    int enabled = 1;
    int sockets[] = {control, path};
    if (provider == NULL || peer <= 0 || provider->lease_signal < 0 ||
        provider->domain.path[0] == '\0')
        return EINVAL;
    for (unsigned at = 0u; at < 2u; ++at) {
        int type = 0;
        socklen_t bytes = sizeof(type);
        if (getsockopt(sockets[at], SOL_SOCKET, SO_TYPE, &type, &bytes) != 0)
            return errno;
        if (type != SOCK_SEQPACKET)
            return EPROTOTYPE;
        if (setsockopt(sockets[at], SOL_SOCKET, SO_PASSCRED, &enabled, sizeof(enabled)) != 0)
            return errno;
    }
    int process = (int)syscall(SYS_pidfd_open, peer, 0u);
    if (process < 0)
        return errno;
    struct pollfd waits[] = {{control, POLLIN, 0}, {path, POLLIN, 0},
                            {provider->lease_signal, POLLIN, 0}, {process, POLLIN, 0},
                            {provider->discovery == NULL ? -1 : provider->discovery->fd, POLLIN, 0}};
    int error = 0;
    const char *phase = "provider poll";
    while (error == 0) {
        phase = "provider poll";
        int changed = astra_storage_provider_poll(provider);
        if (changed < 0) {
            error = -changed;
            break;
        }
        phase = "resource reap";
        int pending = astra_storage_provider_reap(provider);
        if (pending != 0 && pending != EAGAIN) {
            error = pending;
            break;
        }
        if (provider->discovery != NULL) {
            phase = "discovery poll";
            error = astra_storage_discovery_poll(provider->discovery);
            if (error != 0)
                break;
        }
        if (provider->system_source != 0u) {
            const AstraStorageRecord *system = astra_storage_find(&provider->catalog,
                provider->system_source);
            if (system == NULL || system->state == ASTRA_VOLUME_LOST) {
                error = ENODEV;
                break;
            }
        }
        int timeout = next_storage_timeout(provider);
        if (pending == EAGAIN && (timeout < 0 || timeout > 100))
            timeout = 100; /* real retained cleanup, never a zero-time spin */
        phase = "event wait";
        int ready = poll(waits, sizeof(waits) / sizeof(waits[0]), timeout);
        if (ready < 0) {
            if (errno == EINTR)
                continue;
            error = errno;
            break;
        }
        if (waits[3].revents != 0)
            break;
        if (waits[4].revents & POLLIN) {
            phase = "discovery event";
            error = astra_storage_discovery_receive(provider->discovery);
        }
        if (error == 0 && (waits[4].revents & (POLLERR | POLLHUP | POLLNVAL)))
            error = EIO;
        if (error == 0 && (waits[0].revents & POLLIN)) {
            phase = "control request";
            error = astra_storage_provider_control(provider, control, peer,
                provider->domain.uid, provider->domain.gid);
        }
        if (error == 0 && (waits[1].revents & POLLIN)) {
            phase = "path request";
            error = astra_storage_provider_path(provider, path, peer,
                provider->domain.uid, provider->domain.gid);
        }
        if (error == 0 && ((waits[0].revents | waits[1].revents) & (POLLERR | POLLHUP | POLLNVAL)))
            error = EPIPE;
        if (error == 0 && (waits[2].revents & (POLLERR | POLLHUP | POLLNVAL)))
            error = EIO;
    }
    if (error != 0)
        fprintf(stderr, "astra-storage %s failed: %s\n", phase, strerror(error));
    close(process);
    return error;
}

int astra_storage_provider_unmount(AstraStorageProvider *provider, uint64_t mount,
                                    uint64_t generation)
{
    const AstraStorageRecord *record = astra_storage_find(&provider->catalog, mount);
    AstraStorageResource *entry = resource(provider, mount);
    AstraStorageRecord page[16];
    uint64_t sequence = 0u, after = 0u, next, current;
    size_t count;
    int error;
    if (record == NULL || record->generation != generation)
        return ESTALE;
    if (entry == NULL || (record->kind != ASTRA_VOLUME_MOUNT && record->kind != ASTRA_VOLUME_BINDING))
        return EINVAL;
    if (entry->work.pid != 0 || provider->workers >= provider->worker_limit)
        return EAGAIN;
    if (record->kind == ASTRA_VOLUME_BINDING && record->state == ASTRA_VOLUME_MOUNTED)
        return EPERM;
    do {
        error = astra_storage_snapshot(&provider->catalog, sequence, after,
                                       page, 16u, &count, &next, &current);
        if (error != 0)
            return error;
        sequence = current;
        for (size_t at = 0; at < count; ++at)
            if (page[at].parent == mount &&
                page[at].state != ASTRA_VOLUME_LOST &&
                page[at].state != ASTRA_VOLUME_FAILED)
                return EBUSY;
        after = next;
    } while (next != 0u);
    if (record->state == ASTRA_VOLUME_MOUNTED) {
        error = astra_storage_quiesce(&provider->catalog, mount, generation);
        if (error != 0)
            return error;
    }
    if (!entry->mounted.attached) {
        return cleanup_mount_resource(provider, entry, 0);
    }
    if (entry->mounted.root >= 0) {
        close(entry->mounted.root);
        entry->mounted.root = -1;
    }
    error = astra_storage_work_start(&entry->work, &provider->domain, record, -1,
                                      &entry->mounted, ASTRA_STORAGE_WORK_DETACH);
    if (error != 0) {
        int actual = astra_storage_linux_reconcile(&provider->domain, &entry->mounted);
        (void)astra_storage_unmount_result(&provider->catalog, mount, generation,
            actual != 0 ? actual : error, entry->mounted.attached,
            actual == 0 && entry->mounted.attached && entry->mounted.root >= 0);
        return error;
    }
    ++provider->workers;
    return 0;
}

int astra_storage_provider_backing(AstraStorageProvider *provider, uint64_t source,
    uint64_t generation, int *file)
{
    *file = -1;
    const AstraStorageRecord *record = astra_storage_find(&provider->catalog, source);
    AstraStorageResource *entry = resource(provider, source);
    if (record == NULL || record->generation != generation) return ESTALE;
    if (record->kind != ASTRA_VOLUME_SOURCE || record->origin != ASTRA_VOLUME_ORIGIN_IMAGE)
        return EPERM;
    if (entry == NULL || entry->source < 0) return ESTALE;
    *file = fcntl(entry->source, F_DUPFD_CLOEXEC, 0);
    return *file < 0 ? errno : 0;
}

int astra_storage_provider_eject(AstraStorageProvider *provider, uint64_t source,
    uint64_t generation)
{
    const AstraStorageRecord *record = astra_storage_find(&provider->catalog, source);
    AstraStorageResource *entry = resource(provider, source);
    struct stat info;
    if (record == NULL || record->generation != generation || record->state == ASTRA_VOLUME_LOST)
        return ESTALE;
    if (record->kind != ASTRA_VOLUME_SOURCE || record->class_id != ASTRA_VOLUME_OPTICAL ||
        record->origin != ASTRA_VOLUME_ORIGIN_DEVICE) return ENOTSUP;
    if (entry == NULL || entry->source < 0) return ESTALE;
    if (entry->work.pid != 0 || entry->probe.pid != 0 || provider->workers >= provider->worker_limit)
        return EAGAIN;
    if (fstat(entry->source, &info) != 0) return errno;
    if (!S_ISBLK(info.st_mode)) return ENOTSUP;
    int error = astra_storage_eject_begin(&provider->catalog, source, generation);
    if (error != 0) return error;
    error = astra_storage_work_start(&entry->work, &provider->domain, record,
        entry->source, &entry->mounted, ASTRA_STORAGE_WORK_EJECT);
    if (error != 0) {
        (void)astra_storage_eject_result(&provider->catalog, source, generation, error);
        return error;
    }
    ++provider->workers;
    return 0;
}

static int verified_executable(const char *path, int *executable)
{
    struct open_how how = {.flags = O_RDONLY | O_CLOEXEC,
        .resolve = RESOLVE_NO_SYMLINKS | RESOLVE_NO_MAGICLINKS};
    int fd = (int)syscall(SYS_openat2, AT_FDCWD, path, &how, sizeof(how));
    if (fd < 0) return errno;
    struct stat info;
    int error = fstat(fd, &info) != 0 ? errno :
        !S_ISREG(info.st_mode) || info.st_uid != 0u ||
        (info.st_mode & 0022u) != 0u ||
        (info.st_mode & 0111u) == 0u ? EACCES : 0;
    if (error != 0) { close(fd); return error; }
    *executable = fd;
    return 0;
}

static int formatter_executable(uint32_t format, int *executable)
{
    char path[PATH_MAX];
    ssize_t length = readlink("/proc/self/exe", path, sizeof(path) - 1u);
    if (length <= 0 || length >= (ssize_t)sizeof(path) - 1)
        return length < 0 ? errno : ENAMETOOLONG;
    path[length] = '\0';
    char *slash = strrchr(path, '/');
    if (slash == NULL) return EINVAL;
    const char *name = format == ASTRA_VOLUME_FAT32 ?
        "/astra-mkfs.fat" : "/astra-mkfs.exfat";
    if ((size_t)(slash - path) + strlen(name) >= sizeof(path))
        return ENAMETOOLONG;
    strcpy(slash, name);
    return verified_executable(path, executable);
}

int astra_storage_provider_partition(AstraStorageProvider *provider,
    uint64_t device, uint64_t generation, const AstraVolumePartitionEdit *edit)
{
    if (provider == NULL || edit == NULL) return EINVAL;
    const AstraStorageRecord *record = astra_storage_find(&provider->catalog, device);
    if (record == NULL || record->generation != generation ||
        record->state == ASTRA_VOLUME_LOST)
        return ESTALE;
    if (provider->discovery == NULL) return ENOTSUP;
    if (provider->workers >= provider->worker_limit) return EAGAIN;
    AstraStorageResource *entry = resource(provider, device);
    if (entry != NULL && (entry->work.pid != 0 ||
        entry->partition_stage != PARTITION_IDLE || entry->partition_fd >= 0))
        return EBUSY;
    int exclusive = -1, executable = -1;
    int error = astra_storage_discovery_device_fd(provider->discovery,
        device, generation, &exclusive);
    if (error != 0) return error;
    error = verified_executable("/usr/sbin/sfdisk", &executable);
    if (error != 0) { close(exclusive); return error; }
    int added = entry == NULL;
    if (added) {
        entry = reserve(provider, device, generation);
        if (entry == NULL) error = ENOSPC;
    }
    if (error == 0)
        error = astra_storage_partition_begin(&provider->catalog, device, generation);
    if (error == 0) {
        record = astra_storage_find(&provider->catalog, device);
        error = astra_storage_work_partition_start(&entry->work, record,
            exclusive, executable, edit);
        if (error != 0)
            (void)astra_storage_partition_abort(&provider->catalog, device, generation);
    }
    close(executable);
    if (error != 0) {
        close(exclusive);
        if (added && entry != NULL) remove_resource(provider, entry);
        return error;
    }
    entry->partition_fd = exclusive;
    entry->partition_generation = generation;
    entry->partition_error = EAGAIN;
    entry->partition_tool_error = EAGAIN;
    entry->partition_stage = PARTITION_IDLE;
    ++provider->workers;
    return 0;
}

int astra_storage_provider_partition_status(AstraStorageProvider *provider,
    uint64_t device, uint64_t generation, int *result)
{
    if (provider == NULL || result == NULL) return EINVAL;
    AstraStorageResource *entry = resource(provider, device);
    if (entry == NULL || entry->partition_generation != generation)
        return ESTALE;
    *result = entry->partition_error;
    if (*result != 0 && *result != EAGAIN)
        fprintf(stderr, "astra-storage partition status id=%" PRIu64
            " generation=%" PRIu64 " stage=%d error=%d tool=%d\n",
            device, generation, entry->partition_stage, *result,
            entry->partition_tool_error);
    return 0;
}

int astra_storage_provider_format(AstraStorageProvider *provider, uint64_t source,
    uint64_t generation, uint32_t format, const char *label)
{
    if (provider == NULL || label == NULL ||
        (format != ASTRA_VOLUME_FAT32 && format != ASTRA_VOLUME_EXFAT))
        return EINVAL;
    const AstraStorageRecord *record = astra_storage_find(&provider->catalog, source);
    AstraStorageResource *entry = resource(provider, source);
    if (record == NULL || record->generation != generation ||
        record->state == ASTRA_VOLUME_LOST)
        return ESTALE;
    if (entry == NULL || entry->source < 0 || provider->discovery == NULL)
        return ENOTSUP;
    if (entry->work.pid != 0 || entry->probe.pid != 0 ||
        entry->format_fd >= 0 || provider->workers >= provider->worker_limit)
        return EAGAIN;
    int exclusive = -1, executable = -1;
    int error = astra_storage_discovery_format_fd(provider->discovery,
        source, generation, &exclusive);
    if (error != 0) {
        fprintf(stderr, "astra-storage format source=%" PRIu64
            " generation=%" PRIu64 " phase=open error=%d flags=%u\n",
            source, generation, error, record->flags);
        return error;
    }
    const char *phase = "formatter";
    error = formatter_executable(format, &executable);
    if (error == 0) {
        phase = "catalog";
        error = astra_storage_format_begin(&provider->catalog, source, generation);
    }
    if (error == 0) {
        phase = "worker";
        record = astra_storage_find(&provider->catalog, source);
        error = astra_storage_work_format_start(&entry->work, record,
            exclusive, executable, format, label,
            provider->domain.uid, provider->domain.gid);
        if (error != 0)
            (void)astra_storage_format_abort(&provider->catalog, source, generation);
    }
    if (executable >= 0) close(executable);
    if (error != 0) {
        fprintf(stderr, "astra-storage format source=%" PRIu64
            " generation=%" PRIu64 " phase=%s error=%d\n",
            source, generation, phase, error);
        close(exclusive);
        return error;
    }
    entry->format_fd = exclusive;
    entry->format_generation = generation;
    entry->format_error = EAGAIN;
    ++provider->workers;
    return 0;
}

int astra_storage_provider_format_status(AstraStorageProvider *provider,
    uint64_t source, uint64_t generation, int *result)
{
    if (provider == NULL || result == NULL) return EINVAL;
    AstraStorageResource *entry = resource(provider, source);
    if (entry == NULL || entry->format_generation != generation)
        return ESTALE;
    *result = entry->format_error;
    return 0;
}

int astra_storage_provider_release_image(AstraStorageProvider *provider, uint64_t source,
    uint64_t generation)
{
    const AstraStorageRecord *record = astra_storage_find(&provider->catalog, source);
    if (record == NULL || record->generation != generation || record->state == ASTRA_VOLUME_LOST)
        return ESTALE;
    if (record->kind != ASTRA_VOLUME_SOURCE || record->origin != ASTRA_VOLUME_ORIGIN_IMAGE ||
        (record->flags & ASTRA_VOLUME_SYSTEM) != 0u) return EPERM;
    int error = astra_storage_no_mounted_children(&provider->catalog, source);
    if (error != 0) return error;
    return astra_storage_provider_loss(provider, source, generation);
}

int astra_storage_provider_loss(AstraStorageProvider *provider, uint64_t id,
                                 uint64_t generation)
{
    int error = astra_storage_loss(&provider->catalog, id, generation);
    if (error != 0)
        return error;
    for (AstraStorageResource *entry = provider->resources; entry != NULL; entry = entry->next) {
        const AstraStorageRecord *record = astra_storage_find(&provider->catalog, entry->id);
        if (record->state == ASTRA_VOLUME_LOST && entry->probe.pid != 0)
            astra_storage_probe_cancel(&entry->probe);
        if (record->state == ASTRA_VOLUME_LOST && entry->work.pid != 0 &&
            entry->work.operation != ASTRA_STORAGE_WORK_DETACH)
            astra_storage_work_cancel(&entry->work);
    }
    return 0;
}

int astra_storage_provider_release(AstraStorageProvider *provider, uint64_t id)
{
    const AstraStorageRecord *record = astra_storage_find(&provider->catalog, id);
    AstraStorageResource *entry = resource(provider, id);
    uint64_t bytes;
    uint64_t inodes;
    int error;
    if (record == NULL)
        return ESTALE;
    if (entry == NULL) {
        error = astra_storage_can_release(&provider->catalog, id);
        if (error == 0) error = astra_storage_release_result(&provider->catalog, id, record->generation, 0);
        return error == 0 ? astra_storage_forget(&provider->catalog, id) : error;
    }
    if (entry->probe.pid != 0 || entry->work.pid != 0 ||
        entry->format_fd >= 0 || entry->partition_fd >= 0 ||
        entry->partition_stage != PARTITION_IDLE ||
        entry->mounted.root >= 0 || entry->mounted.attached || entry->mounted.directory || entry->mounted.loop >= 0 ||
        entry->mounted.backing >= 0)
        error = EBUSY;
    else
        error = astra_storage_can_release(&provider->catalog, id);
    if (error != 0) goto pending;
    bytes = entry->charged_bytes;
    inodes = entry->charged_inodes;
    if (entry->selection_backing >= 0) {
        error = astra_storage_linux_loop_release(&entry->source);
        if (error != 0)
            goto pending;
        if (entry->selection_leased &&
            fcntl(entry->selection_backing, F_SETLEASE, F_UNLCK) != 0)
            { error = errno; goto pending; }
    }
    if (record->origin == ASTRA_VOLUME_ORIGIN_IMAGE && entry->source >= 0 &&
        fcntl(entry->source, F_SETLEASE, F_UNLCK) != 0)
        { error = errno; goto pending; }
    error = astra_storage_release_result(&provider->catalog, id, record->generation, 0);
    if (error != 0) return error;
    error = astra_storage_forget(&provider->catalog, id);
    if (error != 0)
        return error;
    if (entry->mounted.root >= 0)
        close(entry->mounted.root);
    if (entry->source >= 0)
        close(entry->source);
    if (entry->selection_backing >= 0)
        close(entry->selection_backing);
    provider->ram_bytes -= bytes;
    provider->ram_inodes -= inodes;
    remove_resource(provider, entry);
    return 0;
pending:
    (void)astra_storage_release_result(&provider->catalog, id, record->generation, error);
    return error;
}

int astra_storage_provider_shutdown(AstraStorageProvider *provider)
{
    AstraStorageRecord page[ASTRA_STORAGE_PROVIDER_PAGE];
    uint64_t sequence = 0u, after = 0u, next, current;
    size_t count;
    int error;
    for (AstraStorageResource *entry = provider->resources; entry != NULL; entry = entry->next) {
        entry->path_socket = -1;
        if (entry->work.pid != 0 && entry->work.operation == ASTRA_STORAGE_WORK_PATH)
            astra_storage_work_cancel(&entry->work);
    }
    /* Retire every root observation once; children advance through that loss. */
    do {
        error = astra_storage_snapshot(&provider->catalog, sequence, after, page,
            ASTRA_STORAGE_PROVIDER_PAGE, &count, &next, &current);
        if (error != 0)
            return error;
        for (size_t at = 0u; at < count; ++at)
            if (page[at].parent == 0u && page[at].state != ASTRA_VOLUME_LOST) {
                error = astra_storage_provider_loss(provider, page[at].id, page[at].generation);
                if (error != 0)
                    return error;
            }
        after = next;
        sequence = provider->catalog.sequence;
    } while (next != 0u);
    if (astra_storage_provider_poll(provider) < 0)
        return EIO;
    error = astra_storage_provider_reap(provider);
    if (error != 0 && error != EAGAIN)
        return error;
    if (provider->resources != NULL || provider->probes != 0u || provider->workers != 0u || error == EAGAIN)
        return EAGAIN;
    return astra_storage_provider_close(provider);
}

#define _GNU_SOURCE
#include "astra_storage_discovery.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <libmount/libmount.h>
#include <libudev.h>
#include <linux/fs.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/sysmacros.h>
#include <unistd.h>

struct AstraStorageObservationEntry {
    struct AstraStorageObservationEntry *next;
    char *syspath;
    char *disk_syspath;
    char *devnode;
    dev_t devnum;
    dev_t disk_devnum;
    uint64_t disk_sequence;
    uint64_t device;
    uint64_t medium;
    uint64_t source;
    uint64_t mount;
    size_t bytes;
    uint32_t class_id;
    int partition;
    int read_only;
    int auto_attempted;
};

struct AstraStorageReservedDevice {
    struct AstraStorageReservedDevice *next;
    dev_t devnum;
};

/* Processed udev workers can finish out of kernel sequence order. A per-dev_t
 * watermark rejects stale events without losing valid events for another
 * device. Tiny tombstones survive removal; repeated insertion at the same
 * Linux device number reuses one charged entry, not one per insertion. */
struct AstraStorageDeviceSequence {
    struct AstraStorageDeviceSequence *next;
    dev_t devnum;
    uint64_t sequence;
};

static int accept_sequence(AstraStorageDiscovery *discovery, dev_t devnum,
                            uint64_t sequence, int *accepted)
{
    *accepted = 1;
    for (AstraStorageDeviceSequence *entry = discovery->sequences; entry != NULL; entry = entry->next)
        if (entry->devnum == devnum) {
            if (sequence <= entry->sequence)
                *accepted = 0;
            else
                entry->sequence = sequence;
            return 0;
        }
    if (sequence == 0u)
        return 0;
    if (discovery->bytes > discovery->byte_limit ||
        sizeof(AstraStorageDeviceSequence) > discovery->byte_limit - discovery->bytes)
        return ENOSPC;
    AstraStorageDeviceSequence *entry = malloc(sizeof(*entry));
    if (entry == NULL)
        return ENOMEM;
    *entry = (AstraStorageDeviceSequence){.next = discovery->sequences,
        .devnum = devnum, .sequence = sequence};
    discovery->sequences = entry;
    discovery->bytes += sizeof(*entry);
    return 0;
}

static int charge(AstraStorageDiscovery *discovery, size_t bytes)
{
    return discovery->bytes > discovery->byte_limit ||
        bytes > discovery->byte_limit - discovery->bytes ? ENOSPC : 0;
}

int astra_storage_discovery_reserve(AstraStorageDiscovery *discovery, dev_t devnum)
{
    if (discovery == NULL || devnum == 0)
        return EINVAL;
    for (AstraStorageReservedDevice *entry = discovery->reserved; entry != NULL; entry = entry->next)
        if (entry->devnum == devnum)
            return 0;
    if (charge(discovery, sizeof(AstraStorageReservedDevice)) != 0)
        return ENOSPC;
    AstraStorageReservedDevice *entry = malloc(sizeof(*entry));
    if (entry == NULL)
        return ENOMEM;
    *entry = (AstraStorageReservedDevice){.next = discovery->reserved, .devnum = devnum};
    discovery->reserved = entry;
    discovery->bytes += sizeof(*entry);
    return 0;
}

static int reserved(const AstraStorageDiscovery *discovery, dev_t devnum)
{
    for (const AstraStorageReservedDevice *entry = discovery->reserved; entry != NULL; entry = entry->next)
        if (entry->devnum == devnum)
            return 1;
    return 0;
}

static AstraStorageObservationEntry *find(AstraStorageDiscovery *discovery, const char *syspath)
{
    for (AstraStorageObservationEntry *entry = discovery->entries; entry != NULL; entry = entry->next)
        if (strcmp(entry->syspath, syspath) == 0)
            return entry;
    return NULL;
}

static int lose(AstraStorageDiscovery *discovery, uint64_t id)
{
    const AstraStorageRecord *record = astra_storage_find(&discovery->provider->catalog, id);
    return record == NULL ? 0 :
        astra_storage_provider_loss(discovery->provider, id, record->generation);
}

static void remove_entry(AstraStorageDiscovery *discovery, AstraStorageObservationEntry *entry)
{
    AstraStorageObservationEntry **link = &discovery->entries;
    while (*link != entry)
        link = &(*link)->next;
    *link = entry->next;
    discovery->bytes -= entry->bytes;
    free(entry);
}

static int retire(AstraStorageDiscovery *discovery, AstraStorageObservationEntry *entry)
{
    int error = lose(discovery, entry->partition ? entry->source : entry->device);
    if (error != 0)
        return error;
    if (!entry->partition) {
        /* Parent loss already retires every canonical descendant. Remove only
         * observation mappings, leaving Linux charges with their real owner. */
        AstraStorageObservationEntry *child = discovery->entries;
        while (child != NULL) {
            AstraStorageObservationEntry *next = child->next;
            if (child->partition && (child->device == entry->device ||
                strcmp(child->disk_syspath, entry->syspath) == 0))
                remove_entry(discovery, child);
            child = next;
        }
    }
    remove_entry(discovery, entry);
    return 0;
}

static AstraStorageObservationEntry *add(AstraStorageDiscovery *discovery,
                                          const AstraStorageObservation *observation)
{
    size_t length = strlen(observation->syspath) + 1u;
    size_t disk_length = strlen(observation->disk_syspath) + 1u;
    size_t node_length = strlen(observation->devnode) + 1u;
    size_t bytes = sizeof(AstraStorageObservationEntry) + length + disk_length + node_length;
    if (charge(discovery, bytes) != 0)
        return NULL;
    AstraStorageObservationEntry *entry = calloc(1u, bytes);
    if (entry == NULL)
        return NULL;
    entry->syspath = (char *)(entry + 1);
    memcpy(entry->syspath, observation->syspath, length);
    entry->disk_syspath = entry->syspath + length;
    memcpy(entry->disk_syspath, observation->disk_syspath, disk_length);
    entry->devnode = entry->disk_syspath + disk_length;
    memcpy(entry->devnode, observation->devnode, node_length);
    entry->bytes = bytes;
    entry->devnum = observation->devnum;
    entry->disk_devnum = observation->disk_devnum;
    entry->disk_sequence = observation->disk_sequence;
    entry->class_id = observation->class_id;
    entry->partition = observation->partition;
    entry->read_only = observation->read_only;
    entry->next = discovery->entries;
    discovery->entries = entry;
    discovery->bytes += bytes;
    return entry;
}

static int source(AstraStorageDiscovery *discovery, AstraStorageObservationEntry *entry)
{
    AstraStorageCatalog *catalog = &discovery->provider->catalog;
    const AstraStorageRecord *medium = astra_storage_find(catalog, entry->medium);
    if (medium == NULL)
        return ESTALE;
    return astra_storage_source(catalog, medium->id, medium->generation, &entry->source);
}

static int admit(AstraStorageDiscovery *discovery, AstraStorageObservationEntry *entry,
                   const AstraStorageObservation *observation)
{
    struct stat info;
    int error = source(discovery, entry);
    if (error != 0)
        return error;
    /* Never follow a udev spelling blindly. The open object must be the exact
     * observed block device. O_NONBLOCK admits empty optical drives without
     * blocking the owner; all filesystem inspection runs in bounded workers. */
    int fd = open(observation->devnode, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    uint64_t disk_sequence = 0u;
    if (fd < 0)
        error = errno;
    else if (fstat(fd, &info) != 0)
        error = errno;
    else if (!S_ISBLK(info.st_mode) || info.st_rdev != observation->devnum)
        error = ENODEV;
    else if (observation->disk_sequence != 0u &&
        (ioctl(fd, BLKGETDISKSEQ, &disk_sequence) != 0 || disk_sequence != observation->disk_sequence))
        error = ESTALE;
    else
        error = astra_storage_provider_source(discovery->provider, entry->source, 1u, fd, 0u, 0u);
    if (fd >= 0)
        close(fd);
    if (error != 0) {
        const AstraStorageRecord *record = astra_storage_find(&discovery->provider->catalog, entry->source);
        if (record != NULL && record->state == ASTRA_VOLUME_PROBING)
            (void)astra_storage_probe_result(&discovery->provider->catalog, record->id,
                record->generation, ASTRA_VOLUME_FORMAT_UNKNOWN, "", "", 0, error);
        /* Device failure is visible catalog state, not daemon failure. */
    }
    return 0;
}

static int attach_partition(AstraStorageDiscovery *discovery, AstraStorageObservationEntry *entry)
{
    AstraStorageObservationEntry *disk = find(discovery, entry->disk_syspath);
    if (disk == NULL || disk->partition || disk->devnum != entry->disk_devnum ||
        disk->disk_sequence != entry->disk_sequence || disk->medium == 0u)
        return EAGAIN;
    entry->device = disk->device;
    entry->medium = disk->medium;
    return 0;
}

int astra_storage_discovery_observe(AstraStorageDiscovery *discovery,
                                    const AstraStorageObservation *observation)
{
    if (discovery == NULL || discovery->provider == NULL || observation == NULL ||
        observation->syspath == NULL || observation->disk_syspath == NULL ||
        observation->devnode == NULL || strncmp(observation->syspath, "/sys/", 5u) != 0 ||
        strncmp(observation->disk_syspath, "/sys/", 5u) != 0 ||
        strncmp(observation->devnode, "/dev/", 5u) != 0 ||
        strnlen(observation->syspath, PATH_MAX) == PATH_MAX ||
        strnlen(observation->disk_syspath, PATH_MAX) == PATH_MAX ||
        strnlen(observation->devnode, PATH_MAX) == PATH_MAX ||
        observation->action < ASTRA_STORAGE_OBSERVE_PRESENT || observation->action > ASTRA_STORAGE_OBSERVE_REMOVE ||
        observation->class_id < ASTRA_VOLUME_FIXED || observation->class_id > ASTRA_VOLUME_OPTICAL ||
        observation->devnum == 0 || observation->disk_devnum == 0 ||
        (observation->sequence == 0u && observation->action != ASTRA_STORAGE_OBSERVE_PRESENT))
        return EINVAL;
    int accepted;
    int sequence_error = accept_sequence(discovery, observation->devnum, observation->sequence, &accepted);
    if (sequence_error != 0 || !accepted)
        return sequence_error;
    AstraStorageObservationEntry *entry = find(discovery, observation->syspath);
    if (observation->action == ASTRA_STORAGE_OBSERVE_REMOVE && entry != NULL &&
        observation->disk_sequence != 0u && entry->disk_sequence != observation->disk_sequence)
        return 0; /* removal of an older incarnation at a reused sysfs path */
    if (observation->action == ASTRA_STORAGE_OBSERVE_REMOVE)
        return entry == NULL ? 0 : retire(discovery, entry);
    if (reserved(discovery, observation->devnum) || reserved(discovery, observation->disk_devnum))
        return entry == NULL ? 0 : retire(discovery, entry);
    if (entry != NULL && (entry->devnum != observation->devnum ||
        entry->disk_sequence != observation->disk_sequence || entry->class_id != observation->class_id ||
        entry->partition != observation->partition)) {
        int error = retire(discovery, entry);
        if (error != 0)
            return error;
        entry = NULL;
    }
    if (entry != NULL && observation->sequence == 0u)
        return 0; /* enumeration cannot overwrite a processed event */
    if (entry != NULL && observation->action == ASTRA_STORAGE_OBSERVE_CHANGE && !entry->partition) {
        int error = lose(discovery, entry->medium);
        if (error != 0)
            return error;
        entry->medium = entry->source = entry->mount = 0u;
        entry->auto_attempted = 0;
        for (AstraStorageObservationEntry *partition = discovery->entries; partition != NULL; partition = partition->next)
            if (partition->partition && partition->device == entry->device) {
                partition->medium = partition->source = partition->mount = 0u;
                partition->auto_attempted = 0;
            }
    }
    if (entry != NULL && observation->action == ASTRA_STORAGE_OBSERVE_CHANGE && entry->partition) {
        int error = lose(discovery, entry->source);
        if (error != 0)
            return error;
        entry->source = entry->mount = 0u;
        entry->auto_attempted = 0;
    }
    if (entry != NULL && !entry->partition && observation->partition_table && entry->source != 0u) {
        /* A whole-disk filesystem may not coexist with partition sources. */
        int error = lose(discovery, entry->source);
        if (error != 0)
            return error;
        entry->source = entry->mount = 0u;
        entry->auto_attempted = 0;
    }
    if (entry != NULL)
        entry->read_only = observation->read_only;
    if (entry != NULL && entry->partition && entry->medium == 0u && attach_partition(discovery, entry) == EAGAIN)
        return 0;
    if (entry == NULL) {
        entry = add(discovery, observation);
        if (entry == NULL)
            return charge(discovery, sizeof(*entry) + strlen(observation->syspath) +
                strlen(observation->disk_syspath) + strlen(observation->devnode) + 3u) != 0 ? ENOSPC : ENOMEM;
        int error;
        if (entry->partition) {
            if (attach_partition(discovery, entry) == EAGAIN) {
                return 0; /* charged observation waits for its processed parent */
            }
            error = 0;
        } else
            error = astra_storage_device(&discovery->provider->catalog, entry->class_id, &entry->device);
        if (error != 0) {
            remove_entry(discovery, entry);
            return error;
        }
    }
    if (!observation->media)
        return 0; /* a real empty drive remains a visible device */
    if (entry->medium == 0u) {
        const AstraStorageRecord *device = astra_storage_find(&discovery->provider->catalog, entry->device);
        int error = device == NULL ? ESTALE : astra_storage_medium(&discovery->provider->catalog,
            device->id, device->generation, &entry->medium);
        if (error != 0)
            return error;
    }
    if (entry->source == 0u && (entry->partition || !observation->partition_table))
        return admit(discovery, entry, observation);
    return 0;
}

int astra_storage_discovery_poll(AstraStorageDiscovery *discovery)
{
    if (discovery == NULL || discovery->provider == NULL)
        return EINVAL;
    for (AstraStorageObservationEntry *entry = discovery->entries; entry != NULL; entry = entry->next) {
        if (entry->partition && entry->medium == 0u) {
            if (attach_partition(discovery, entry) == EAGAIN)
                continue;
            AstraStorageObservation observation = {.devnode = entry->devnode,
                .devnum = entry->devnum, .disk_sequence = entry->disk_sequence};
            int error = admit(discovery, entry, &observation);
            if (error != 0)
                return error;
        }
        const AstraStorageRecord *record = astra_storage_find(&discovery->provider->catalog, entry->source);
        if (entry->auto_attempted || record == NULL || record->state == ASTRA_VOLUME_PROBING ||
            entry->class_id == ASTRA_VOLUME_FIXED)
            continue;
        if (record->state != ASTRA_VOLUME_UNMOUNTED) {
            entry->auto_attempted = 1;
            continue;
        }
        uint64_t mount = 0u;
        int error = astra_storage_provider_mount(discovery->provider, record->id, record->generation,
            entry->class_id == ASTRA_VOLUME_OPTICAL || entry->read_only ? ASTRA_VOLUME_READ_ONLY : 0u,
            0u, 0u, &mount);
        if (error == EAGAIN)
            continue; /* worker completion wakes the same owner */
        entry->mount = mount;
        entry->auto_attempted = 1; /* manual unmount must stay unmounted */
        if (error != 0 && error != ENOTSUP && error != EROFS && error != ENOSPC)
            return error;
    }
    return 0;
}

static struct udev_device *disk_of(struct udev_device *device)
{
    const char *type = udev_device_get_devtype(device);
    return type != NULL && strcmp(type, "partition") == 0 ?
        udev_device_get_parent_with_subsystem_devtype(device, "block", "disk") : device;
}

static int reserve_device(AstraStorageDiscovery *discovery, struct udev_device *device)
{
    dev_t number = udev_device_get_devnum(device);
    if (reserved(discovery, number))
        return 0;
    int error = astra_storage_discovery_reserve(discovery, number);
    struct udev_device *disk = disk_of(device);
    if (error == 0 && disk != NULL && disk != device)
        error = reserve_device(discovery, disk);
    if (error != 0)
        return error;
    char path[PATH_MAX];
    const char *syspath = udev_device_get_syspath(device);
    int length = syspath == NULL ? -1 : snprintf(path, sizeof(path), "%s/slaves", syspath);
    if (length < 0 || (size_t)length >= sizeof(path))
        return ENAMETOOLONG;
    DIR *slaves = opendir(path);
    if (slaves == NULL)
        return errno == ENOENT ? 0 : errno;
    struct dirent *name;
    while (error == 0 && (name = readdir(slaves)) != NULL) {
        if (name->d_name[0] == '.')
            continue;
        char child_path[PATH_MAX];
        length = snprintf(child_path, sizeof(child_path), "%s/%s", path, name->d_name);
        if (length < 0 || (size_t)length >= sizeof(child_path)) {
            error = ENAMETOOLONG;
            break;
        }
        struct udev_device *child = udev_device_new_from_syspath(discovery->udev, child_path);
        if (child == NULL)
            error = ENODEV;
        else {
            error = reserve_device(discovery, child);
            udev_device_unref(child);
        }
    }
    closedir(slaves);
    return error;
}

static int reserve_mounted(AstraStorageDiscovery *discovery)
{
    struct libmnt_table *table = mnt_new_table_from_file("/proc/self/mountinfo");
    struct libmnt_iter *iterator = mnt_new_iter(MNT_ITER_FORWARD);
    if (table == NULL || iterator == NULL) {
        mnt_free_table(table);
        mnt_free_iter(iterator);
        return ENOMEM;
    }
    struct libmnt_fs *filesystem;
    int error = 0;
    while (error == 0 && mnt_table_next_fs(table, iterator, &filesystem) == 0) {
        dev_t number = mnt_fs_get_devno(filesystem);
        struct udev_device *device = udev_device_new_from_devnum(discovery->udev, 'b', number);
        if (device != NULL) {
            error = reserve_device(discovery, device);
            udev_device_unref(device);
        }
    }
    mnt_free_iter(iterator);
    mnt_free_table(table);
    return error;
}

static uint64_t decimal(const char *text)
{
    if (text == NULL || text[0] < '0' || text[0] > '9')
        return 0u;
    char *end;
    errno = 0;
    unsigned long long value = strtoull(text, &end, 10);
    return errno != 0 || *end != '\0' ? 0u : value;
}

static int one(struct udev_device *device, const char *name)
{
    const char *value = udev_device_get_property_value(device, name);
    return value != NULL && strcmp(value, "1") == 0;
}

static int observe_device(AstraStorageDiscovery *discovery, struct udev_device *device, int enumeration)
{
    struct udev_device *disk = disk_of(device);
    const char *type = udev_device_get_devtype(device);
    const char *action = enumeration ? NULL : udev_device_get_action(device);
    if (disk == NULL || type == NULL ||
        (strcmp(type, "disk") != 0 && strcmp(type, "partition") != 0))
        return 0;
    dev_t number = udev_device_get_devnum(device);
    dev_t disk_number = udev_device_get_devnum(disk);
    /* Loop images belong only to explicit trusted/native image admission.
     * They must never be rediscovered as physical fixed/USB disks. */
    const char *sysname = udev_device_get_sysname(disk);
    if (number == 0 || disk_number == 0 || sysname == NULL ||
        strncmp(sysname, "loop", 4u) == 0 || strncmp(sysname, "ram", 3u) == 0)
        return 0;
    if (enumeration && !udev_device_get_is_initialized(device))
        return 0; /* only the processed database is authoritative */
    uint32_t operation = ASTRA_STORAGE_OBSERVE_PRESENT;
    if (action != NULL) {
        if (strcmp(action, "remove") == 0)
            operation = ASTRA_STORAGE_OBSERVE_REMOVE;
        else if (strcmp(action, "change") == 0)
            operation = ASTRA_STORAGE_OBSERVE_CHANGE;
        else if (strcmp(action, "add") != 0)
            return 0;
    }
    const char *bus = udev_device_get_property_value(disk, "ID_BUS");
    int optical = one(disk, "ID_CDROM");
    const char *node = udev_device_get_devnode(device);
    const char *ro = udev_device_get_sysattr_value(device, "ro");
    AstraStorageObservation observation = {
        .syspath = udev_device_get_syspath(device), .disk_syspath = udev_device_get_syspath(disk),
        .devnode = node, .devnum = number, .disk_devnum = disk_number,
        .sequence = enumeration ? 0u : udev_device_get_seqnum(device),
        .disk_sequence = decimal(udev_device_get_property_value(device, "DISKSEQ")),
        .action = operation, .class_id = optical ? ASTRA_VOLUME_OPTICAL :
            bus != NULL && strcmp(bus, "usb") == 0 ? ASTRA_VOLUME_USB : ASTRA_VOLUME_FIXED,
        .partition = strcmp(type, "partition") == 0,
        .media = optical ? one(disk, "ID_CDROM_MEDIA") : 1,
        .partition_table = udev_device_get_property_value(disk, "ID_PART_TABLE_TYPE") != NULL,
        .read_only = ro != NULL && strcmp(ro, "1") == 0
    };
    if (observation.disk_sequence == 0u)
        observation.disk_sequence = decimal(udev_device_get_sysattr_value(disk, "diskseq"));
    if (node == NULL)
        return ENODEV; /* processed block events must identify their devnode */
    return astra_storage_discovery_observe(discovery, &observation);
}

int astra_storage_discovery_receive(AstraStorageDiscovery *discovery)
{
    if (discovery == NULL || discovery->monitor == NULL)
        return EINVAL;
    for (unsigned at = 0u; at < 64u; ++at) {
        errno = 0;
        struct udev_device *device = udev_monitor_receive_device(discovery->monitor);
        if (device == NULL)
            return errno == 0 || errno == EAGAIN || errno == EWOULDBLOCK ? 0 : errno;
        int error = observe_device(discovery, device, 0);
        udev_device_unref(device);
        if (error != 0)
            return error;
    }
    return 0; /* retain fairness with native control/path and worker completion */
}

int astra_storage_discovery_open(AstraStorageDiscovery *discovery,
                                  AstraStorageProvider *provider, size_t bytes)
{
    if (discovery == NULL || provider == NULL || bytes == 0u || discovery->udev != NULL ||
        discovery->entries != NULL || discovery->reserved != NULL)
        return EINVAL;
    *discovery = (AstraStorageDiscovery){.provider = provider, .byte_limit = bytes, .fd = -1};
    discovery->udev = udev_new();
    if (discovery->udev == NULL)
        return ENOMEM;
    discovery->monitor = udev_monitor_new_from_netlink(discovery->udev, "udev");
    int error = discovery->monitor == NULL ? ENOMEM : 0;
    if (error == 0 && (udev_monitor_filter_add_match_subsystem_devtype(discovery->monitor, "block", NULL) < 0 ||
        udev_monitor_enable_receiving(discovery->monitor) < 0))
        error = errno != 0 ? errno : EIO;
    if (error == 0) {
        discovery->fd = udev_monitor_get_fd(discovery->monitor);
        if (discovery->fd < 0)
            error = EIO;
    }
    if (error == 0)
        error = reserve_mounted(discovery);
    /* Monitor is active before the snapshot. Enumerate disks before partitions
     * so one inserted medium owns every admitted partition source. Queued
     * processed events are applied afterwards and supersede the snapshot. */
    for (unsigned pass = 0u; error == 0 && pass < 2u; ++pass) {
        struct udev_enumerate *enumerate = udev_enumerate_new(discovery->udev);
        if (enumerate == NULL) {
            error = ENOMEM;
            break;
        }
        if (udev_enumerate_add_match_subsystem(enumerate, "block") < 0 ||
            udev_enumerate_scan_devices(enumerate) < 0)
            error = errno != 0 ? errno : EIO;
        struct udev_list_entry *item;
        udev_list_entry_foreach(item, udev_enumerate_get_list_entry(enumerate)) {
            if (error != 0)
                break;
            struct udev_device *device = udev_device_new_from_syspath(discovery->udev, udev_list_entry_get_name(item));
            if (device == NULL)
                continue; /* removed during snapshot; its queued event wins */
            const char *type = udev_device_get_devtype(device);
            if (type != NULL && strcmp(type, pass == 0u ? "disk" : "partition") == 0)
                error = observe_device(discovery, device, 1);
            udev_device_unref(device);
        }
        udev_enumerate_unref(enumerate);
    }
    if (error == 0)
        error = astra_storage_discovery_receive(discovery);
    if (error != 0)
        astra_storage_discovery_close(discovery);
    return error;
}

void astra_storage_discovery_close(AstraStorageDiscovery *discovery)
{
    if (discovery == NULL)
        return;
    while (discovery->entries != NULL)
        remove_entry(discovery, discovery->entries);
    while (discovery->reserved != NULL) {
        AstraStorageReservedDevice *entry = discovery->reserved;
        discovery->reserved = entry->next;
        discovery->bytes -= sizeof(*entry);
        free(entry);
    }
    while (discovery->sequences != NULL) {
        AstraStorageDeviceSequence *entry = discovery->sequences;
        discovery->sequences = entry->next;
        discovery->bytes -= sizeof(*entry);
        free(entry);
    }
    if (discovery->monitor != NULL)
        udev_monitor_unref(discovery->monitor);
    if (discovery->udev != NULL)
        udev_unref(discovery->udev);
    *discovery = (AstraStorageDiscovery)ASTRA_STORAGE_DISCOVERY_INIT;
}

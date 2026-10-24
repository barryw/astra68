#ifndef ASTRA_STORAGE_DISCOVERY_H
#define ASTRA_STORAGE_DISCOVERY_H

#include "astra_storage_provider.h"
#include <sys/types.h>

struct udev;
struct udev_monitor;
typedef struct AstraStorageObservationEntry AstraStorageObservationEntry;
typedef struct AstraStorageReservedDevice AstraStorageReservedDevice;
typedef struct AstraStorageDeviceSequence AstraStorageDeviceSequence;

/* An observation maps Linux discovery identity to the canonical catalog. It
 * owns no mount, source descriptor, probe worker or second lifecycle. */
typedef struct AstraStorageDiscovery {
    AstraStorageProvider *provider;
    struct udev *udev;
    struct udev_monitor *monitor;
    AstraStorageObservationEntry *entries;
    AstraStorageReservedDevice *reserved;
    AstraStorageDeviceSequence *sequences;
    size_t bytes;
    size_t byte_limit;
    int fd;
} AstraStorageDiscovery;

#define ASTRA_STORAGE_DISCOVERY_INIT { .fd = -1 }

enum AstraStorageObservationAction {
    ASTRA_STORAGE_OBSERVE_PRESENT = 1,
    ASTRA_STORAGE_OBSERVE_CHANGE,
    ASTRA_STORAGE_OBSERVE_REMOVE
};

/* The libudev adapter produces this exact bounded observation. PRESENT is also
 * used for enumeration; sequence zero is enumeration only. Optical CHANGE
 * withdraws the old medium even when an inserted replacement has the same
 * filesystem UUID. Disk sequence independently detects reused device nodes. */
typedef struct AstraStorageObservation {
    const char *syspath;
    const char *disk_syspath;
    const char *devnode;
    dev_t devnum;
    dev_t disk_devnum;
    uint64_t sequence;
    uint64_t disk_sequence;
    uint64_t capacity_bytes;
    uint64_t offset_bytes;
    uint32_t action;
    uint32_t class_id;
    uint32_t partition_number;
    uint32_t partition_map;
    int partition;
    int media;
    int partition_table;
    int read_only;
} AstraStorageObservation;

/* Subscribe to processed udev before enumerating. Reserve all existing Linux
 * mounted devices, their containing disks and recursively their slaves. */
int astra_storage_discovery_open(AstraStorageDiscovery *discovery,
                                  AstraStorageProvider *provider, size_t bytes);
void astra_storage_discovery_close(AstraStorageDiscovery *discovery);
int astra_storage_discovery_receive(AstraStorageDiscovery *discovery);
int astra_storage_discovery_poll(AstraStorageDiscovery *discovery);
/* Shared reducer, also exercised with disposable real block-device fixtures. */
int astra_storage_discovery_observe(AstraStorageDiscovery *discovery,
                                    const AstraStorageObservation *observation);
int astra_storage_discovery_reserve(AstraStorageDiscovery *discovery,
                                    dev_t devnum);
/* Provider-only exact physical source opener. Caller owns the returned
 * exclusive descriptor and must pair it with catalog format admission. */
int astra_storage_discovery_format_fd(AstraStorageDiscovery *discovery,
    uint64_t source, uint64_t generation, int *fd);
/* Provider-only exact parent-disk opener for partition edits. Sibling usage
 * admission still belongs to the provider before this exclusive open. */
int astra_storage_discovery_device_fd(AstraStorageDiscovery *discovery,
    uint64_t device, uint64_t generation, int *fd);
/* After an edit worker has actually exited, retire stale media descriptions.
 * Provider reaps their Linux resources before rereading the kernel table. */
int astra_storage_discovery_partition_retire(AstraStorageDiscovery *discovery,
    uint64_t device);
int astra_storage_discovery_partition_reread(AstraStorageDiscovery *discovery,
    uint64_t device, uint64_t generation);
/* Successful format may be auto-mounted after its new probe; failure stays
 * unmounted for explicit user inspection. */
void astra_storage_discovery_format_result(AstraStorageDiscovery *discovery,
    uint64_t source, int success);

#endif

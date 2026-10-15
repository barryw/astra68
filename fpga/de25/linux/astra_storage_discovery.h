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
    uint32_t action;
    uint32_t class_id;
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

#endif

#define _GNU_SOURCE
#include "astra_storage_discovery.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/sysmacros.h>

static const AstraStorageRecord *kind(AstraStorageProvider *provider, uint32_t wanted,
                                      uint64_t after)
{
    AstraStorageRecord rows[ASTRA_STORAGE_PROVIDER_PAGE];
    size_t count;
    uint64_t next, sequence;
    assert(astra_storage_snapshot(&provider->catalog, 0u, 0u, rows,
        ASTRA_STORAGE_PROVIDER_PAGE, &count, &next, &sequence) == 0 && next == 0u);
    for (size_t at = 0u; at < count; ++at)
        if (rows[at].kind == wanted && rows[at].id > after && rows[at].state != ASTRA_VOLUME_LOST)
            return astra_storage_find(&provider->catalog, rows[at].id);
    return NULL;
}

static void clear(AstraStorageDiscovery *discovery)
{
    AstraStorageProvider *provider = discovery->provider;
    AstraStorageRecord rows[ASTRA_STORAGE_PROVIDER_PAGE];
    size_t count;
    uint64_t next, sequence;
    assert(astra_storage_snapshot(&provider->catalog, 0u, 0u, rows,
        ASTRA_STORAGE_PROVIDER_PAGE, &count, &next, &sequence) == 0 && next == 0u);
    for (size_t at = 0u; at < count; ++at)
        if (rows[at].parent == 0u && rows[at].state != ASTRA_VOLUME_LOST)
            assert(astra_storage_provider_loss(provider, rows[at].id, rows[at].generation) == 0);
    for (unsigned at = 0u; at < ASTRA_STORAGE_PROVIDER_PAGE && provider->catalog.bytes != 0u; ++at)
        assert(astra_storage_provider_reap(provider) == 0 || provider->catalog.bytes != 0u);
    assert(provider->catalog.bytes == 0u);
    astra_storage_discovery_close(discovery);
}

static void optical(void)
{
    AstraStorageProvider provider = ASTRA_STORAGE_PROVIDER_INIT;
    assert(astra_storage_catalog_init(&provider.catalog, 1u << 20) == 0);
    AstraStorageDiscovery discovery = {.provider = &provider, .byte_limit = 1u << 20, .fd = -1};
    AstraStorageObservation observation = {.syspath = "/sys/devices/test/sr0", .disk_syspath = "/sys/devices/test/sr0",
        .devnode = "/dev/null", .devnum = makedev(11u, 0u), .disk_devnum = makedev(11u, 0u),
        .sequence = 100u, .disk_sequence = 9u, .action = ASTRA_STORAGE_OBSERVE_PRESENT,
        .class_id = ASTRA_VOLUME_OPTICAL};
    assert(astra_storage_discovery_observe(&discovery, &observation) == 0);
    const AstraStorageRecord *device = kind(&provider, ASTRA_VOLUME_DEVICE, 0u);
    assert(device != NULL && device->state == ASTRA_VOLUME_DISCOVERED);
    assert(kind(&provider, ASTRA_VOLUME_MEDIUM, 0u) == NULL);
    uint64_t device_id = device->id;
    observation.sequence = 101u;
    observation.action = ASTRA_STORAGE_OBSERVE_CHANGE;
    observation.media = 1;
    assert(astra_storage_discovery_observe(&discovery, &observation) == 0);
    const AstraStorageRecord *medium = kind(&provider, ASTRA_VOLUME_MEDIUM, 0u);
    const AstraStorageRecord *source = kind(&provider, ASTRA_VOLUME_SOURCE, 0u);
    assert(medium != NULL && medium->parent == device_id && source != NULL && source->parent == medium->id);
    assert(source->state == ASTRA_VOLUME_FAILED && source->error == ENODEV); /* a char device is never admitted */
    uint64_t first_medium = medium->id, first_source = source->id;
    observation.sequence = 102u;
    assert(astra_storage_discovery_observe(&discovery, &observation) == 0);
    assert(astra_storage_find(&provider.catalog, first_medium)->state == ASTRA_VOLUME_LOST);
    assert(astra_storage_find(&provider.catalog, first_source)->generation == 2u);
    medium = kind(&provider, ASTRA_VOLUME_MEDIUM, first_medium);
    assert(medium != NULL && medium->id != first_medium && medium->parent == device_id);
    assert(astra_storage_provider_reap(&provider) == 0);
    assert(astra_storage_find(&provider.catalog, first_medium) == NULL);
    observation.sequence = 101u;
    observation.action = ASTRA_STORAGE_OBSERVE_REMOVE;
    assert(astra_storage_discovery_observe(&discovery, &observation) == 0);
    assert(astra_storage_find(&provider.catalog, device_id)->state == ASTRA_VOLUME_DISCOVERED);
    observation.sequence = 103u;
    observation.action = ASTRA_STORAGE_OBSERVE_CHANGE;
    observation.media = 0;
    assert(astra_storage_discovery_observe(&discovery, &observation) == 0);
    assert(astra_storage_provider_reap(&provider) == 0);
    assert(kind(&provider, ASTRA_VOLUME_MEDIUM, 0u) == NULL);
    assert(astra_storage_find(&provider.catalog, device_id) != NULL);
    observation.sequence = 104u;
    observation.action = ASTRA_STORAGE_OBSERVE_REMOVE;
    assert(astra_storage_discovery_observe(&discovery, &observation) == 0);
    assert(astra_storage_provider_reap(&provider) == 0 && provider.catalog.bytes == 0u);
    observation.sequence = 102u;
    observation.action = ASTRA_STORAGE_OBSERVE_PRESENT;
    assert(astra_storage_discovery_observe(&discovery, &observation) == 0 && provider.catalog.bytes == 0u);
    size_t sequence_bytes = discovery.bytes;
    for (unsigned at = 0u; at < 1000u; ++at) {
        observation.sequence = 200u + 2u * at;
        observation.action = ASTRA_STORAGE_OBSERVE_PRESENT;
        assert(astra_storage_discovery_observe(&discovery, &observation) == 0);
        device = kind(&provider, ASTRA_VOLUME_DEVICE, 0u);
        assert(device != NULL && device->id > device_id);
        ++observation.sequence;
        observation.action = ASTRA_STORAGE_OBSERVE_REMOVE;
        assert(astra_storage_discovery_observe(&discovery, &observation) == 0);
        assert(astra_storage_provider_reap(&provider) == 0 && provider.catalog.bytes == 0u);
        assert(discovery.bytes == sequence_bytes); /* no per-insertion tombstone leak */
    }
    clear(&discovery);
    assert(discovery.bytes == 0u);
    astra_storage_catalog_destroy(&provider.catalog);
}

static void partitions_and_reserved(void)
{
    AstraStorageProvider provider = ASTRA_STORAGE_PROVIDER_INIT;
    assert(astra_storage_catalog_init(&provider.catalog, 1u << 20) == 0);
    AstraStorageDiscovery discovery = {.provider = &provider, .byte_limit = 1u << 20, .fd = -1};
    AstraStorageObservation disk = {.syspath = "/sys/devices/test/sda", .disk_syspath = "/sys/devices/test/sda",
        .devnode = "/dev/null", .devnum = makedev(8u, 0u), .disk_devnum = makedev(8u, 0u),
        .sequence = 200u, .disk_sequence = 10u, .action = ASTRA_STORAGE_OBSERVE_PRESENT,
        .class_id = ASTRA_VOLUME_USB, .media = 1, .partition_table = 1};
    AstraStorageObservation partition = disk;
    partition.syspath = "/sys/devices/test/sda/sda1";
    partition.devnum = makedev(8u, 1u);
    partition.partition = 1;
    partition.sequence = 199u; /* other dev_t may finish processing out of order */
    assert(astra_storage_discovery_observe(&discovery, &disk) == 0);
    assert(kind(&provider, ASTRA_VOLUME_SOURCE, 0u) == NULL); /* no competing whole-disk source */
    assert(astra_storage_discovery_observe(&discovery, &partition) == 0);
    uint64_t medium = kind(&provider, ASTRA_VOLUME_MEDIUM, 0u)->id;
    const AstraStorageRecord *source = kind(&provider, ASTRA_VOLUME_SOURCE, 0u);
    assert(source != NULL && source->class_id == ASTRA_VOLUME_USB && source->parent == medium);
    uint64_t source_id = source->id;
    assert(astra_storage_discovery_observe(&discovery, &partition) == 0);
    assert(kind(&provider, ASTRA_VOLUME_SOURCE, source_id) == NULL);
    ++partition.sequence;
    partition.action = ASTRA_STORAGE_OBSERVE_CHANGE;
    assert(astra_storage_discovery_observe(&discovery, &partition) == 0);
    assert(astra_storage_find(&provider.catalog, source_id)->state == ASTRA_VOLUME_LOST);
    assert(astra_storage_provider_reap(&provider) == 0);
    source_id = kind(&provider, ASTRA_VOLUME_SOURCE, 0u)->id;
    assert(astra_storage_discovery_reserve(&discovery, disk.devnum) == 0);
    ++partition.sequence;
    assert(astra_storage_discovery_observe(&discovery, &partition) == 0);
    assert(astra_storage_find(&provider.catalog, source_id)->state == ASTRA_VOLUME_LOST);
    ++disk.sequence;
    assert(astra_storage_discovery_observe(&discovery, &disk) == 0);
    assert(astra_storage_provider_reap(&provider) == 0 && provider.catalog.bytes == 0u);
    ++disk.sequence;
    assert(astra_storage_discovery_observe(&discovery, &disk) == 0 && provider.catalog.bytes == 0u);
    clear(&discovery);
    astra_storage_catalog_destroy(&provider.catalog);
}

static void parent_order(void)
{
    AstraStorageProvider provider = ASTRA_STORAGE_PROVIDER_INIT;
    assert(astra_storage_catalog_init(&provider.catalog, 1u << 20) == 0);
    AstraStorageDiscovery discovery = {.provider = &provider, .byte_limit = 1u << 20, .fd = -1};
    AstraStorageObservation disk = {.syspath = "/sys/devices/test/sdb", .disk_syspath = "/sys/devices/test/sdb",
        .devnode = "/dev/null", .devnum = makedev(8u, 16u), .disk_devnum = makedev(8u, 16u),
        .sequence = 10u, .disk_sequence = 22u, .action = ASTRA_STORAGE_OBSERVE_PRESENT,
        .class_id = ASTRA_VOLUME_USB, .media = 1, .partition_table = 1};
    AstraStorageObservation partition = disk;
    partition.syspath = "/sys/devices/test/sdb/sdb1";
    partition.devnum = makedev(8u, 17u);
    partition.partition = 1;
    partition.sequence = 11u;
    assert(astra_storage_discovery_observe(&discovery, &partition) == 0);
    assert(provider.catalog.bytes == 0u && discovery.bytes != 0u);
    assert(astra_storage_discovery_observe(&discovery, &disk) == 0);
    assert(astra_storage_discovery_poll(&discovery) == 0);
    uint64_t source_id = kind(&provider, ASTRA_VOLUME_SOURCE, 0u)->id;
    disk.sequence = 12u;
    disk.action = ASTRA_STORAGE_OBSERVE_CHANGE;
    assert(astra_storage_discovery_observe(&discovery, &disk) == 0);
    assert(astra_storage_find(&provider.catalog, source_id)->state == ASTRA_VOLUME_LOST);
    assert(astra_storage_discovery_poll(&discovery) == 0);
    assert(astra_storage_provider_reap(&provider) == 0);
    assert(kind(&provider, ASTRA_VOLUME_SOURCE, 0u)->id > source_id);
    disk.sequence = 13u;
    disk.action = ASTRA_STORAGE_OBSERVE_REMOVE;
    assert(astra_storage_discovery_observe(&discovery, &disk) == 0);
    assert(astra_storage_provider_reap(&provider) == 0 && provider.catalog.bytes == 0u);
    clear(&discovery);
    astra_storage_catalog_destroy(&provider.catalog);
}

static void budget(void)
{
    AstraStorageProvider provider = ASTRA_STORAGE_PROVIDER_INIT;
    assert(astra_storage_catalog_init(&provider.catalog, 1u << 20) == 0);
    AstraStorageDiscovery discovery = {.provider = &provider, .byte_limit = 1u, .fd = -1};
    AstraStorageObservation observation = {.syspath = "/sys/devices/test/sda", .disk_syspath = "/sys/devices/test/sda",
        .devnode = "/dev/null", .devnum = makedev(8u, 0u), .disk_devnum = makedev(8u, 0u),
        .sequence = 1u, .action = ASTRA_STORAGE_OBSERVE_PRESENT, .class_id = ASTRA_VOLUME_USB};
    assert(astra_storage_discovery_observe(&discovery, &observation) == ENOSPC);
    assert(provider.catalog.bytes == 0u && discovery.bytes == 0u);
    clear(&discovery);
    astra_storage_catalog_destroy(&provider.catalog);
}

int main(void)
{
    optical();
    partitions_and_reserved();
    parent_order();
    budget();
    puts("storage discovery: empty/change/loss, exact generations, out-of-order/stale events, partition/root exclusion, 1000 cycles and budget PASS");
    return 0;
}

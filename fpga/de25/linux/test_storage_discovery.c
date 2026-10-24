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
    assert((device->flags & ASTRA_VOLUME_READ_ONLY) != 0u);
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
    assert((source->flags & ASTRA_VOLUME_READ_ONLY) != 0u);
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
        .class_id = ASTRA_VOLUME_USB, .media = 1, .partition_table = 1,
        .read_only = 1,
        .partition_map = ASTRA_VOLUME_PARTITION_MAP_GPT, .capacity_bytes = UINT64_C(64) << 30};
    AstraStorageObservation partition = disk;
    partition.syspath = "/sys/devices/test/sda/sda1";
    partition.devnum = makedev(8u, 1u);
    partition.partition = 1;
    partition.partition_number = 1u;
    partition.offset_bytes = UINT64_C(1) << 20;
    partition.capacity_bytes = UINT64_C(8) << 30;
    partition.read_only = 0; /* parent disk write protection still applies */
    partition.sequence = 199u; /* other dev_t may finish processing out of order */
    assert(astra_storage_discovery_observe(&discovery, &disk) == 0);
    assert((kind(&provider, ASTRA_VOLUME_DEVICE, 0u)->flags & ASTRA_VOLUME_READ_ONLY) != 0u);
    assert(kind(&provider, ASTRA_VOLUME_SOURCE, 0u) == NULL); /* no competing whole-disk source */
    assert(astra_storage_discovery_observe(&discovery, &partition) == 0);
    uint64_t medium = kind(&provider, ASTRA_VOLUME_MEDIUM, 0u)->id;
    const AstraStorageRecord *source = kind(&provider, ASTRA_VOLUME_SOURCE, 0u);
    assert(source != NULL && source->class_id == ASTRA_VOLUME_USB && source->parent == medium);
    assert((source->flags & ASTRA_VOLUME_READ_ONLY) != 0u);
    assert(source->partition_number == 1u && source->partition_map == ASTRA_VOLUME_PARTITION_MAP_GPT);
    assert(source->offset_bytes == (UINT64_C(1) << 20) &&
        source->capacity_bytes == (UINT64_C(8) << 30));
    assert(kind(&provider, ASTRA_VOLUME_DEVICE, 0u)->capacity_bytes == (UINT64_C(64) << 30));
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

static void optical_partition_inherits_protection(void)
{
    AstraStorageProvider provider = ASTRA_STORAGE_PROVIDER_INIT;
    assert(astra_storage_catalog_init(&provider.catalog, 1u << 20) == 0);
    AstraStorageDiscovery discovery = {.provider = &provider, .byte_limit = 1u << 20, .fd = -1};
    AstraStorageObservation disk = {.syspath = "/sys/devices/test/sr1",
        .disk_syspath = "/sys/devices/test/sr1", .devnode = "/dev/null",
        .devnum = makedev(11u, 1u), .disk_devnum = makedev(11u, 1u),
        .sequence = 1u, .disk_sequence = 3u, .action = ASTRA_STORAGE_OBSERVE_PRESENT,
        .class_id = ASTRA_VOLUME_OPTICAL, .media = 1, .partition_table = 1,
        .capacity_bytes = UINT64_C(4) << 30};
    assert(astra_storage_discovery_observe(&discovery, &disk) == 0);
    AstraStorageObservation partition = disk;
    partition.syspath = "/sys/devices/test/sr1/sr1p1";
    partition.devnum = makedev(11u, 2u);
    partition.class_id = ASTRA_VOLUME_USB; /* parent still owns transport policy */
    partition.partition = 1;
    partition.partition_number = 1u;
    partition.offset_bytes = UINT64_C(1) << 20;
    partition.capacity_bytes = UINT64_C(2) << 30;
    assert(astra_storage_discovery_observe(&discovery, &partition) == 0);
    const AstraStorageRecord *source = kind(&provider, ASTRA_VOLUME_SOURCE, 0u);
    assert(source != NULL && source->class_id == ASTRA_VOLUME_OPTICAL &&
        (source->flags & ASTRA_VOLUME_READ_ONLY) != 0u);
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
        .class_id = ASTRA_VOLUME_USB, .media = 1, .partition_table = 1,
        .partition_map = ASTRA_VOLUME_PARTITION_MAP_MBR, .capacity_bytes = UINT64_C(32) << 30};
    AstraStorageObservation partition = disk;
    partition.syspath = "/sys/devices/test/sdb/sdb1";
    partition.devnum = makedev(8u, 17u);
    partition.partition = 1;
    partition.partition_number = 1u;
    partition.offset_bytes = UINT64_C(1) << 20;
    partition.capacity_bytes = UINT64_C(4) << 30;
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

static void independent_partition_geometry(void)
{
    AstraStorageProvider provider = ASTRA_STORAGE_PROVIDER_INIT;
    assert(astra_storage_catalog_init(&provider.catalog, 1u << 20) == 0);
    AstraStorageDiscovery discovery = {.provider = &provider, .byte_limit = 1u << 20, .fd = -1};
    AstraStorageObservation disk = {.syspath = "/sys/devices/test/sdc",
        .disk_syspath = "/sys/devices/test/sdc", .devnode = "/dev/null",
        .devnum = makedev(8u, 32u), .disk_devnum = makedev(8u, 32u),
        .sequence = 10u, .disk_sequence = 30u, .action = ASTRA_STORAGE_OBSERVE_PRESENT,
        .class_id = ASTRA_VOLUME_USB, .media = 1, .partition_table = 1,
        .partition_map = ASTRA_VOLUME_PARTITION_MAP_GPT,
        .capacity_bytes = UINT64_C(64) << 30};
    assert(astra_storage_discovery_observe(&discovery, &disk) == 0);
    AstraStorageObservation first = disk;
    first.syspath = "/sys/devices/test/sdc/sdc1";
    first.devnum = makedev(8u, 33u);
    first.partition = 1;
    first.partition_number = 1u;
    first.offset_bytes = UINT64_C(1) << 20;
    first.capacity_bytes = UINT64_C(8) << 30;
    assert(astra_storage_discovery_observe(&discovery, &first) == 0);
    const AstraStorageRecord *source1 = kind(&provider, ASTRA_VOLUME_SOURCE, 0u);
    assert(source1 != NULL && source1->partition_number == 1u);
    uint64_t first_id = source1->id, medium = source1->parent;
    AstraStorageObservation second = first;
    second.syspath = "/sys/devices/test/sdc/sdc2";
    second.devnum = makedev(8u, 34u);
    second.partition_number = 2u;
    second.offset_bytes = UINT64_C(9) << 30;
    second.capacity_bytes = UINT64_C(16) << 30;
    assert(astra_storage_discovery_observe(&discovery, &second) == 0);
    const AstraStorageRecord *source2 = kind(&provider, ASTRA_VOLUME_SOURCE, first_id);
    assert(source2 != NULL && source2->id != first_id && source2->parent == medium);
    assert(source2->partition_number == 2u && source2->offset_bytes == (UINT64_C(9) << 30));
    uint64_t second_id = source2->id;
    second.action = ASTRA_STORAGE_OBSERVE_CHANGE;
    ++second.sequence;
    second.partition_number = 0u; /* Linux may send CHANGE after sysfs removal. */
    second.offset_bytes = 0u;
    second.capacity_bytes = 0u;
    assert(astra_storage_discovery_observe(&discovery, &second) == 0);
    assert(astra_storage_find(&provider.catalog, first_id)->state != ASTRA_VOLUME_LOST);
    assert(astra_storage_find(&provider.catalog, second_id)->state == ASTRA_VOLUME_LOST);
    second.action = ASTRA_STORAGE_OBSERVE_REMOVE;
    ++second.sequence;
    assert(astra_storage_discovery_observe(&discovery, &second) == 0);
    AstraStorageObservation invalid = first;
    invalid.syspath = "/sys/devices/test/sdc/sdc3";
    invalid.devnum = makedev(8u, 35u);
    invalid.partition_number = 3u;
    invalid.offset_bytes = UINT64_C(63) << 30;
    invalid.capacity_bytes = UINT64_C(2) << 30;
    invalid.sequence = 13u;
    assert(astra_storage_discovery_observe(&discovery, &invalid) == 0);
    assert(kind(&provider, ASTRA_VOLUME_SOURCE, first_id) == NULL);
    ++invalid.sequence;
    invalid.action = ASTRA_STORAGE_OBSERVE_CHANGE;
    invalid.capacity_bytes = UINT64_C(1) << 30;
    assert(astra_storage_discovery_observe(&discovery, &invalid) == 0);
    const AstraStorageRecord *recovered = kind(&provider, ASTRA_VOLUME_SOURCE, first_id);
    assert(recovered != NULL && recovered->partition_number == 3u &&
        recovered->offset_bytes + recovered->capacity_bytes == (UINT64_C(64) << 30));
    uint64_t recovered_id = recovered->id;
    ++disk.sequence;
    disk.capacity_bytes = UINT64_C(4) << 30;
    assert(astra_storage_discovery_observe(&discovery, &disk) == 0);
    assert(astra_storage_find(&provider.catalog, first_id)->state == ASTRA_VOLUME_LOST);
    assert(astra_storage_find(&provider.catalog, recovered_id)->state == ASTRA_VOLUME_LOST);
    assert(astra_storage_discovery_poll(&discovery) == 0);
    assert(kind(&provider, ASTRA_VOLUME_SOURCE, 0u) == NULL);
    ++disk.sequence;
    disk.capacity_bytes = UINT64_C(64) << 30;
    assert(astra_storage_discovery_observe(&discovery, &disk) == 0);
    assert(astra_storage_discovery_poll(&discovery) == 0);
    source1 = kind(&provider, ASTRA_VOLUME_SOURCE, recovered_id);
    assert(source1 != NULL && source1->id > recovered_id);
    uint64_t restored_id = source1->id;
    ++disk.sequence;
    disk.read_only = 1;
    assert(astra_storage_discovery_observe(&discovery, &disk) == 0);
    assert(astra_storage_find(&provider.catalog, restored_id)->state == ASTRA_VOLUME_LOST);
    assert(astra_storage_discovery_poll(&discovery) == 0);
    source1 = kind(&provider, ASTRA_VOLUME_SOURCE, restored_id);
    assert(source1 != NULL && (source1->flags & ASTRA_VOLUME_READ_ONLY) != 0u);
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

static void format_target_identity(void)
{
    AstraStorageProvider provider = ASTRA_STORAGE_PROVIDER_INIT;
    assert(astra_storage_catalog_init(&provider.catalog, 1u << 20) == 0);
    AstraStorageDiscovery discovery = {.provider = &provider, .byte_limit = 1u << 20, .fd = -1};
    AstraStorageObservation disk = {.syspath = "/sys/devices/test/format",
        .disk_syspath = "/sys/devices/test/format", .devnode = "/dev/null",
        .devnum = makedev(8u, 224u), .disk_devnum = makedev(8u, 224u),
        .sequence = 1u, .disk_sequence = 42u, .action = ASTRA_STORAGE_OBSERVE_PRESENT,
        .class_id = ASTRA_VOLUME_USB, .media = 1,
        .capacity_bytes = UINT64_C(128) << 20};
    assert(astra_storage_discovery_observe(&discovery, &disk) == 0);
    const AstraStorageRecord *device = kind(&provider, ASTRA_VOLUME_DEVICE, 0u);
    const AstraStorageRecord *source = kind(&provider, ASTRA_VOLUME_SOURCE, 0u);
    assert(device != NULL && source != NULL);
    int fd = 99;
    assert(astra_storage_discovery_device_fd(&discovery, device->id, 2u, &fd) == ESTALE);
    assert(fd == -1);
    assert(astra_storage_discovery_device_fd(&discovery, device->id, 1u, &fd) == ENODEV);
    assert(fd == -1);
    assert(astra_storage_discovery_format_fd(&discovery, source->id, 2u, &fd) == ESTALE);
    assert(fd == -1);
    assert(astra_storage_discovery_format_fd(&discovery, source->id, 1u, &fd) == ENODEV);
    assert(fd == -1); /* The observed dev_t may never select a different node. */
    assert(astra_storage_discovery_reserve(&discovery, disk.devnum) == 0);
    assert(astra_storage_discovery_device_fd(&discovery, device->id, 1u, &fd) == EACCES);
    assert(fd == -1);
    assert(astra_storage_discovery_format_fd(&discovery, source->id, 1u, &fd) == EACCES);
    assert(fd == -1);
    clear(&discovery);
    astra_storage_catalog_destroy(&provider.catalog);
}

static void partition_edit_fence(void)
{
    AstraStorageProvider provider = ASTRA_STORAGE_PROVIDER_INIT;
    assert(astra_storage_catalog_init(&provider.catalog, 1u << 20) == 0);
    AstraStorageDiscovery discovery = {.provider = &provider, .byte_limit = 1u << 20, .fd = -1};
    AstraStorageObservation disk = {.syspath = "/sys/devices/test/edit-disk",
        .disk_syspath = "/sys/devices/test/edit-disk", .devnode = "/dev/null",
        .devnum = makedev(8u, 225u), .disk_devnum = makedev(8u, 225u),
        .sequence = 1u, .disk_sequence = 43u, .action = ASTRA_STORAGE_OBSERVE_PRESENT,
        .class_id = ASTRA_VOLUME_USB, .capacity_bytes = UINT64_C(128) << 20};
    assert(astra_storage_discovery_observe(&discovery, &disk) == 0);
    const AstraStorageRecord *device = kind(&provider, ASTRA_VOLUME_DEVICE, 0u);
    assert(device != NULL);
    uint64_t id = device->id;
    assert(astra_storage_partition_begin(&provider.catalog, id, 1u) == 0);
    ++disk.sequence;
    disk.action = ASTRA_STORAGE_OBSERVE_CHANGE;
    disk.partition_map = ASTRA_VOLUME_PARTITION_MAP_GPT;
    assert(astra_storage_discovery_observe(&discovery, &disk) == 0);
    device = astra_storage_find(&provider.catalog, id);
    assert(device->state == ASTRA_VOLUME_PARTITIONING &&
        device->partition_map == ASTRA_VOLUME_PARTITION_MAP_NONE);
    assert(astra_storage_partition_abort(&provider.catalog, id, 1u) == 0);
    ++disk.sequence;
    assert(astra_storage_discovery_observe(&discovery, &disk) == 0);
    device = astra_storage_find(&provider.catalog, id);
    assert(device->state == ASTRA_VOLUME_DISCOVERED &&
        device->partition_map == ASTRA_VOLUME_PARTITION_MAP_GPT);
    clear(&discovery);
    astra_storage_catalog_destroy(&provider.catalog);
}

int main(void)
{
    optical();
    partitions_and_reserved();
    optical_partition_inherits_protection();
    parent_order();
    independent_partition_geometry();
    format_target_identity();
    partition_edit_fence();
    budget();
    puts("storage discovery: empty/change/loss, exact generations, out-of-order/stale events, partition/root exclusion, 1000 cycles and budget PASS");
    return 0;
}

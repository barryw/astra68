#include <astra/dos_library.h>

#include <assert.h>
#include <stdio.h>
#include <string.h>

static AstraVolumeRecord rows[3];
static uint32_t admitted;
static uint32_t last_format;
static AstraVolumePartitionEdit last_edit;
static uint64_t sequence = 5u;

AstraResult astra_volume_snapshot_open(AstraHandle service, AstraVolumeSnapshot *out)
{
    assert(service == 11u);
    *out = (AstraVolumeSnapshot){9u, sequence, rows, 3u};
    return ASTRA_OK;
}

void astra_volume_snapshot_close(AstraVolumeSnapshot *out)
{
    *out = (AstraVolumeSnapshot)ASTRA_VOLUME_SNAPSHOT_INIT;
}

AstraResult astra_volume_usage_snapshot_open(AstraHandle service,
    const AstraVolumeReference *filter, AstraVolumeUsageSnapshot *out)
{
    assert(service == 12u && filter == NULL);
    *out = (AstraVolumeUsageSnapshot)ASTRA_VOLUME_USAGE_SNAPSHOT_INIT;
    out->catalog_sequence = 5u;
    return ASTRA_OK;
}

void astra_volume_usage_snapshot_close(AstraVolumeUsageSnapshot *out)
{
    *out = (AstraVolumeUsageSnapshot)ASTRA_VOLUME_USAGE_SNAPSHOT_INIT;
}

const AstraVolumeRecord *astra_volume_snapshot_find(
    const AstraVolumeSnapshot *snapshot, const AstraVolumeReference *reference)
{
    if (reference->epoch != snapshot->epoch) return NULL;
    for (uint32_t at = 0u; at < snapshot->count; ++at)
        if (snapshot->records[at].id == reference->id &&
            snapshot->records[at].generation == reference->generation)
            return &snapshot->records[at];
    return NULL;
}

const AstraVolumeRecord *astra_volume_snapshot_ancestor(
    const AstraVolumeSnapshot *snapshot, const AstraVolumeReference *reference,
    uint32_t kind)
{
    const AstraVolumeRecord *record = astra_volume_snapshot_find(snapshot, reference);
    return record != NULL && kind == ASTRA_VOLUME_DEVICE &&
        (record->id == 1u || record->id == 2u || record->id == 3u) ? &rows[0] : NULL;
}

AstraResult astra_volume_partition_find_free(const AstraVolumeSnapshot *snapshot,
    const AstraVolumeReference *device, uint64_t length, uint64_t *offset)
{
    assert(snapshot->epoch == 9u && device->id == 1u);
    if (length > UINT64_C(1024) * 1048576u) return ASTRA_ERROR_NO_RESOURCES;
    *offset = UINT64_C(16) * 1048576u;
    return ASTRA_OK;
}

AstraResult astra_volume_partition(AstraHandle administrator,
    const AstraVolumeReference *device, const AstraVolumePartitionEdit *edit)
{
    assert(administrator == 13u && device->id == 1u);
    ++admitted;
    last_edit = *edit;
    return ASTRA_OK;
}

AstraResult astra_volume_format(AstraHandle administrator,
    const AstraVolumeReference *source, uint32_t format, const char *label)
{
    assert(administrator == 13u && source->id == 2u && strcmp(label, "TEST") == 0);
    ++admitted;
    last_format = format;
    return ASTRA_OK;
}

AstraResult astra_volume_partition_status(AstraHandle administrator,
    const AstraVolumeReference *device, AstraResult *outcome)
{
    assert(administrator == 13u && device->id == 1u);
    *outcome = ASTRA_ERROR_BUSY;
    return ASTRA_OK;
}

AstraResult astra_volume_format_status(AstraHandle administrator,
    const AstraVolumeReference *source, AstraResult *outcome)
{
    assert(administrator == 13u && source->id == 2u);
    *outcome = ASTRA_OK;
    return ASTRA_OK;
}

int main(void)
{
    rows[0] = (AstraVolumeRecord){.id = 1u, .generation = 7u,
        .kind = ASTRA_VOLUME_DEVICE, .class_id = ASTRA_VOLUME_USB,
        .state = ASTRA_VOLUME_DISCOVERED, .partition_map = ASTRA_VOLUME_PARTITION_MAP_GPT};
    rows[1] = (AstraVolumeRecord){.id = 2u, .generation = 4u,
        .kind = ASTRA_VOLUME_SOURCE, .origin = ASTRA_VOLUME_ORIGIN_DEVICE,
        .state = ASTRA_VOLUME_UNMOUNTED, .partition_number = 1u};
    rows[2] = (AstraVolumeRecord){.id = 3u, .generation = 2u,
        .kind = ASTRA_VOLUME_MOUNT, .state = ASTRA_VOLUME_LOST};
    AstraDosDiskSnapshot snapshot = ASTRA_DOS_DISK_SNAPSHOT_INIT;
    assert(astra_dos_disk_snapshot_open(11u, 12u, &snapshot) == ASTRA_OK);
    assert(astra_dos_disk_snapshot_open(11u, 12u, &snapshot) ==
        ASTRA_ERROR_INVALID_ARGUMENT && snapshot.volumes.records == rows);
    AstraDosDiskRequest request = {{9u, 1u, 7u}, ASTRA_DOS_DISK_CREATE_FAT32,
        UINT64_C(256) * 1048576u, NULL};
    AstraDosDiskJob job = {0};
    assert(astra_dos_disk_check(&snapshot, &request) == ASTRA_OK);
    assert(astra_dos_disk_submit(13u, &snapshot, &request, &job) == ASTRA_OK);
    assert(admitted == 1u && job.reference.id == 1u &&
        last_edit.operation == ASTRA_VOLUME_PARTITION_CREATE &&
        last_edit.offset_bytes == UINT64_C(16) * 1048576u);
    AstraResult outcome = ASTRA_ERROR_IO;
    assert(astra_dos_disk_status(13u, &job, &outcome) == ASTRA_OK &&
        outcome == ASTRA_ERROR_BUSY);
    request.operation = ASTRA_DOS_DISK_INITIALIZE_MBR;
    assert(astra_dos_disk_submit(13u, &snapshot, &request, &job) == ASTRA_OK &&
        last_edit.map == ASTRA_VOLUME_PARTITION_MAP_MBR);
    request.target = (AstraVolumeReference){9u, 2u, 4u};
    request.operation = ASTRA_DOS_DISK_FORMAT_EXFAT;
    request.label = "TEST";
    assert(astra_dos_disk_submit(13u, &snapshot, &request, &job) == ASTRA_OK &&
        last_format == ASTRA_VOLUME_EXFAT && job.reference.id == 2u);
    assert(astra_dos_disk_status(13u, &job, &outcome) == ASTRA_OK && outcome == ASTRA_OK);
    request.operation = ASTRA_DOS_DISK_DELETE_PARTITION;
    assert(astra_dos_disk_submit(13u, &snapshot, &request, &job) == ASTRA_OK &&
        last_edit.number == 1u);
    rows[2].state = ASTRA_VOLUME_MOUNTED;
    assert(astra_dos_disk_check(&snapshot, &request) == ASTRA_ERROR_BUSY);
    rows[2].state = ASTRA_VOLUME_LOST;
    rows[0].flags = ASTRA_VOLUME_PHYSICAL_READ_ONLY;
    assert(astra_dos_disk_check(&snapshot, &request) == ASTRA_ERROR_PERMISSION);
    rows[0].flags = 0u;
    request.target.generation++;
    assert(astra_dos_disk_check(&snapshot, &request) == ASTRA_ERROR_INVALID_HANDLE);
    request.target = (AstraVolumeReference){9u, 1u, 7u};
    request.operation = ASTRA_DOS_DISK_CREATE_EXFAT;
    request.size_bytes = 5u;
    assert(astra_dos_disk_check(&snapshot, &request) == ASTRA_ERROR_INVALID_ARGUMENT);
    astra_dos_disk_snapshot_close(&snapshot);
    sequence++;
    assert(astra_dos_disk_snapshot_open(11u, 12u, &snapshot) == ASTRA_ERROR_BUSY);
    assert(snapshot.volumes.records == NULL && snapshot.usage.records == NULL);
    puts("dos.library disk policy PASS");
    return 0;
}

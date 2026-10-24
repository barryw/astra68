#include <astra/dos_library.h>
#include <astra/library.h>

#include <string.h>

ASTRA_DYNAMIC_LIBRARY("dos.library.1", 1, 0, 0,
    ASTRA_DOS_LIBRARY_ABI_MAJOR, ASTRA_DOS_LIBRARY_ABI_MINOR,
    "Astra68 contributors", "Copyright 2026 Astra68 contributors");

static const AstraVolumeRecord *device_for(const AstraVolumeSnapshot *snapshot,
    const AstraVolumeReference *target, const AstraVolumeRecord *record)
{
    if (record->kind == ASTRA_VOLUME_DEVICE) return record;
    return record->kind == ASTRA_VOLUME_SOURCE &&
        record->origin == ASTRA_VOLUME_ORIGIN_DEVICE ?
        astra_volume_snapshot_ancestor(snapshot, target, ASTRA_VOLUME_DEVICE) : NULL;
}

static AstraResult prepare(const AstraDosDiskSnapshot *snapshot,
    const AstraDosDiskRequest *request, AstraVolumeReference *device,
    AstraVolumePartitionEdit *edit)
{
    if (snapshot == NULL || request == NULL || device == NULL || edit == NULL ||
        snapshot->volumes.epoch == 0u ||
        snapshot->usage.catalog_sequence != snapshot->volumes.sequence)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    const AstraVolumeRecord *record = astra_volume_snapshot_find(
        &snapshot->volumes, &request->target);
    if (record == NULL) return ASTRA_ERROR_INVALID_HANDLE;
    const AstraVolumeRecord *disk = device_for(&snapshot->volumes,
        &request->target, record);
    if (disk == NULL) return ASTRA_ERROR_INVALID_ARGUMENT;
    if ((disk->flags & (ASTRA_VOLUME_SYSTEM | ASTRA_VOLUME_PHYSICAL_READ_ONLY)) != 0u ||
        (disk->class_id != ASTRA_VOLUME_USB && disk->class_id != ASTRA_VOLUME_FIXED))
        return ASTRA_ERROR_PERMISSION;
    if (disk->state != ASTRA_VOLUME_DISCOVERED) return ASTRA_ERROR_BUSY;
    *device = (AstraVolumeReference){snapshot->volumes.epoch, disk->id,
        disk->generation};
    for (uint32_t at = 0u; at < snapshot->volumes.count; ++at) {
        const AstraVolumeRecord *mount = &snapshot->volumes.records[at];
        if (mount->kind != ASTRA_VOLUME_MOUNT ||
            (mount->state != ASTRA_VOLUME_MOUNTED &&
             mount->state != ASTRA_VOLUME_MOUNTING &&
             mount->state != ASTRA_VOLUME_QUIESCING)) continue;
        const AstraVolumeReference current = {snapshot->volumes.epoch,
            mount->id, mount->generation};
        if (astra_volume_snapshot_ancestor(&snapshot->volumes, &current,
            ASTRA_VOLUME_DEVICE) == disk) return ASTRA_ERROR_BUSY;
    }
    memset(edit, 0, sizeof(*edit));
    switch (request->operation) {
    case ASTRA_DOS_DISK_INITIALIZE_MBR:
    case ASTRA_DOS_DISK_INITIALIZE_GPT:
        if (record != disk) return ASTRA_ERROR_INVALID_ARGUMENT;
        edit->operation = ASTRA_VOLUME_PARTITION_INITIALIZE;
        edit->map = request->operation == ASTRA_DOS_DISK_INITIALIZE_MBR ?
            ASTRA_VOLUME_PARTITION_MAP_MBR : ASTRA_VOLUME_PARTITION_MAP_GPT;
        return ASTRA_OK;
    case ASTRA_DOS_DISK_CREATE_FAT32:
    case ASTRA_DOS_DISK_CREATE_EXFAT:
        if (disk->partition_map != ASTRA_VOLUME_PARTITION_MAP_MBR &&
            disk->partition_map != ASTRA_VOLUME_PARTITION_MAP_GPT)
            return ASTRA_ERROR_INVALID_ARGUMENT;
        if (request->size_bytes == 0u ||
            request->size_bytes % UINT64_C(1048576) != 0u)
            return ASTRA_ERROR_INVALID_ARGUMENT;
        edit->operation = ASTRA_VOLUME_PARTITION_CREATE;
        edit->map = disk->partition_map;
        edit->format = request->operation == ASTRA_DOS_DISK_CREATE_FAT32 ?
            ASTRA_VOLUME_FAT32 : ASTRA_VOLUME_EXFAT;
        edit->length_bytes = request->size_bytes;
        return astra_volume_partition_find_free(&snapshot->volumes, device,
            edit->length_bytes, &edit->offset_bytes);
    case ASTRA_DOS_DISK_DELETE_PARTITION:
        if (disk->partition_map != ASTRA_VOLUME_PARTITION_MAP_MBR &&
            disk->partition_map != ASTRA_VOLUME_PARTITION_MAP_GPT)
            return ASTRA_ERROR_INVALID_ARGUMENT;
        if (record->kind != ASTRA_VOLUME_SOURCE ||
            record->partition_number == 0u)
            return ASTRA_ERROR_INVALID_ARGUMENT;
        if (record->state != ASTRA_VOLUME_UNMOUNTED &&
            record->state != ASTRA_VOLUME_FAILED) return ASTRA_ERROR_BUSY;
        edit->operation = ASTRA_VOLUME_PARTITION_DELETE;
        edit->map = disk->partition_map;
        edit->number = record->partition_number;
        return ASTRA_OK;
    case ASTRA_DOS_DISK_FORMAT_FAT32:
    case ASTRA_DOS_DISK_FORMAT_EXFAT:
        if (record->kind != ASTRA_VOLUME_SOURCE ||
            record->origin != ASTRA_VOLUME_ORIGIN_DEVICE)
            return ASTRA_ERROR_INVALID_ARGUMENT;
        if ((record->flags & (ASTRA_VOLUME_SYSTEM |
            ASTRA_VOLUME_PHYSICAL_READ_ONLY)) != 0u) return ASTRA_ERROR_PERMISSION;
        if (record->state != ASTRA_VOLUME_UNMOUNTED &&
            record->state != ASTRA_VOLUME_FAILED) return ASTRA_ERROR_BUSY;
        return request->label == NULL ? ASTRA_ERROR_INVALID_ARGUMENT : ASTRA_OK;
    default:
        return ASTRA_ERROR_INVALID_ARGUMENT;
    }
}

AstraResult astra_dos_disk_snapshot_open(AstraHandle metadata,
    AstraHandle usage, AstraDosDiskSnapshot *snapshot)
{
    if (snapshot == NULL || snapshot->volumes.epoch != 0u ||
        snapshot->volumes.sequence != 0u || snapshot->volumes.records != NULL ||
        snapshot->volumes.count != 0u || snapshot->usage.volume.epoch != 0u ||
        snapshot->usage.volume.id != 0u ||
        snapshot->usage.volume.generation != 0u ||
        snapshot->usage.sequence != 0u ||
        snapshot->usage.catalog_sequence != 0u ||
        snapshot->usage.records != NULL || snapshot->usage.count != 0u)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    AstraResult result = astra_volume_snapshot_open(metadata, &snapshot->volumes);
    if (result != ASTRA_OK) return result;
    result = astra_volume_usage_snapshot_open(usage, NULL, &snapshot->usage);
    if (result != ASTRA_OK ||
        snapshot->usage.catalog_sequence != snapshot->volumes.sequence) {
        astra_dos_disk_snapshot_close(snapshot);
        return result == ASTRA_OK ? ASTRA_ERROR_BUSY : result;
    }
    return ASTRA_OK;
}

void astra_dos_disk_snapshot_close(AstraDosDiskSnapshot *snapshot)
{
    if (snapshot == NULL) return;
    astra_volume_usage_snapshot_close(&snapshot->usage);
    astra_volume_snapshot_close(&snapshot->volumes);
}

AstraResult astra_dos_disk_check(const AstraDosDiskSnapshot *snapshot,
    const AstraDosDiskRequest *request)
{
    AstraVolumeReference device = {0};
    AstraVolumePartitionEdit edit = {0};
    return prepare(snapshot, request, &device, &edit);
}

AstraResult astra_dos_disk_submit(AstraHandle administrator,
    const AstraDosDiskSnapshot *snapshot, const AstraDosDiskRequest *request,
    AstraDosDiskJob *job)
{
    if (job == NULL) return ASTRA_ERROR_INVALID_ARGUMENT;
    memset(job, 0, sizeof(*job));
    AstraVolumeReference device = {0};
    AstraVolumePartitionEdit edit = {0};
    AstraResult result = prepare(snapshot, request, &device, &edit);
    if (result != ASTRA_OK) return result;
    if (request->operation == ASTRA_DOS_DISK_FORMAT_FAT32 ||
        request->operation == ASTRA_DOS_DISK_FORMAT_EXFAT) {
        result = astra_volume_format(administrator, &request->target,
            request->operation == ASTRA_DOS_DISK_FORMAT_FAT32 ?
                ASTRA_VOLUME_FAT32 : ASTRA_VOLUME_EXFAT, request->label);
        if (result == ASTRA_OK) job->reference = request->target;
    } else {
        result = astra_volume_partition(administrator, &device, &edit);
        if (result == ASTRA_OK) job->reference = device;
    }
    if (result == ASTRA_OK) job->operation = request->operation;
    return result;
}

AstraResult astra_dos_disk_status(AstraHandle administrator,
    const AstraDosDiskJob *job, AstraResult *outcome)
{
    if (outcome == NULL) return ASTRA_ERROR_INVALID_ARGUMENT;
    *outcome = ASTRA_ERROR_IO;
    if (job == NULL || job->reference.epoch == 0u || job->reference.id == 0u ||
        job->reference.generation == 0u) return ASTRA_ERROR_INVALID_ARGUMENT;
    if (job->operation == ASTRA_DOS_DISK_FORMAT_FAT32 ||
        job->operation == ASTRA_DOS_DISK_FORMAT_EXFAT)
        return astra_volume_format_status(administrator, &job->reference, outcome);
    if (job->operation < ASTRA_DOS_DISK_INITIALIZE_MBR ||
        job->operation > ASTRA_DOS_DISK_DELETE_PARTITION)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return astra_volume_partition_status(administrator, &job->reference, outcome);
}

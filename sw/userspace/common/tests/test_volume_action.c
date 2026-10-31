#include "../volume_action.h"

#include <astra/status.h>

#include <assert.h>
#include <stdio.h>

static AstraVolumeRecord records[5];
static uint32_t controls, operation, scripted_state, scripted_status;
static uint64_t held_mount, controlled_mount;
static AstraResult admission = ASTRA_OK;

const AstraVolumeRecord *astra_volume_snapshot_find(
    const AstraVolumeSnapshot *snapshot, const AstraVolumeReference *reference)
{
    if (snapshot->epoch != reference->epoch) return NULL;
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
    const AstraVolumeRecord *row = astra_volume_snapshot_find(snapshot, reference);
    for (uint32_t depth = 0u; row != NULL && depth < snapshot->count; ++depth) {
        if (row->kind == kind) return row;
        AstraVolumeReference parent = {snapshot->epoch, row->parent,
            row->parent_generation};
        row = astra_volume_snapshot_find(snapshot, &parent);
    }
    return NULL;
}

AstraResult astra_volume_usage_snapshot_open(AstraHandle inspector,
    const AstraVolumeReference *reference, AstraVolumeUsageSnapshot *snapshot)
{
    assert(inspector == 8u && reference != NULL);
    snapshot->volume = *reference;
    snapshot->count = reference->id == held_mount ? 1u : 0u;
    return ASTRA_OK;
}

void astra_volume_usage_snapshot_close(AstraVolumeUsageSnapshot *snapshot)
{
    *snapshot = (AstraVolumeUsageSnapshot)ASTRA_VOLUME_USAGE_SNAPSHOT_INIT;
}

AstraResult astra_volume_control(AstraHandle administrator,
    const AstraVolumeControl *control, AstraVolumeReference *reference)
{
    assert(administrator == 7u);
    ++controls;
    operation = control->operation;
    controlled_mount = control->reference.id;
    *reference = control->reference;
    return admission;
}

AstraResult astra_volume_removal_advance(AstraHandle administrator,
    const AstraVolumeSnapshot *snapshot, AstraVolumeRemoval *removal)
{
    assert(administrator == 7u && snapshot != NULL);
    removal->state = scripted_state;
    removal->status = scripted_status;
    return ASTRA_OK;
}

int main(void)
{
    records[0] = (AstraVolumeRecord){.id = 10u, .generation = 2u,
        .parent = 20u, .parent_generation = 3u,
        .kind = ASTRA_VOLUME_MOUNT, .state = ASTRA_VOLUME_MOUNTED,
        .origin = ASTRA_VOLUME_ORIGIN_IMAGE};
    records[1] = (AstraVolumeRecord){.id = 20u, .generation = 3u,
        .parent = 30u, .parent_generation = 1u,
        .kind = ASTRA_VOLUME_SOURCE, .origin = ASTRA_VOLUME_ORIGIN_DEVICE,
        .class_id = ASTRA_VOLUME_OPTICAL};
    records[2] = (AstraVolumeRecord){.id = 30u, .generation = 1u,
        .kind = ASTRA_VOLUME_MEDIUM, .class_id = ASTRA_VOLUME_OPTICAL};
    AstraVolumeSnapshot catalog = {5u, 1u, records, 3u};
    AstraVolumeReference mount = {5u, 10u, 2u};
    AstraVolumeAction action = {0};

    assert(astra_volume_action_begin(7u, 8u, &catalog, &mount, 1, &action) == ASTRA_OK);
    assert(controls == 1u && operation == ASTRA_VOLUME_UNMOUNT);
    assert(action.removal.image_source.id == 20u &&
        action.removal.image_source.generation == 3u);
    assert(action.eject_source.id == 20u);
    records[0].state = ASTRA_VOLUME_UNMOUNTED;
    scripted_state = ASTRA_VOLUME_REMOVAL_COMPLETE;
    assert(astra_volume_action_advance(7u, &catalog, &action) == ASTRA_OK);
    assert(controls == 2u && operation == ASTRA_VOLUME_EJECT &&
        action.eject_admitted && !action.finished);
    records[1].state = ASTRA_VOLUME_LOST;
    assert(astra_volume_action_advance(7u, &catalog, &action) == ASTRA_OK);
    assert(action.finished && action.failure == 0u);

    action = (AstraVolumeAction){0};
    records[1].state = ASTRA_VOLUME_UNMOUNTED;
    records[1].class_id = ASTRA_VOLUME_USB;
    records[0].state = ASTRA_VOLUME_MOUNTED;
    assert(astra_volume_action_begin(7u, 8u, &catalog, &mount, 1, &action) ==
        ASTRA_ERROR_INVALID_ARGUMENT);
    assert(action.removal.mount.id == 0u && controls == 2u);
    admission = ASTRA_ERROR_BUSY;
    assert(astra_volume_action_begin(7u, 8u, &catalog, &mount, 0, &action) ==
        ASTRA_ERROR_BUSY);
    assert(action.removal.mount.id == 0u);
    admission = ASTRA_OK;
    assert(astra_volume_action_begin(7u, 8u, &catalog, &mount, 0, &action) == ASTRA_OK);
    scripted_state = ASTRA_VOLUME_REMOVAL_REFUSED;
    scripted_status = ASTRA_STATUS_BUSY;
    assert(astra_volume_action_advance(7u, &catalog, &action) == ASTRA_OK);
    assert(action.finished && action.failure == ASTRA_STATUS_BUSY);

    action = (AstraVolumeAction){0};
    assert(astra_volume_action_begin(7u, 8u, &catalog, &mount, 0, &action) == ASTRA_OK);
    scripted_state = ASTRA_VOLUME_REMOVAL_RETAINED;
    assert(astra_volume_action_advance(7u, &catalog, &action) == ASTRA_OK);
    assert(!action.finished && action.failure == ASTRA_STATUS_BUSY && action.reported);

    action = (AstraVolumeAction){0};
    records[0].flags = ASTRA_VOLUME_SYSTEM;
    assert(astra_volume_action_begin(7u, 8u, &catalog, &mount, 0, &action) ==
        ASTRA_ERROR_INVALID_HANDLE);
    records[0].flags = ASTRA_VOLUME_TEMPORARY;
    assert(astra_volume_action_begin(7u, 8u, &catalog, &mount, 0, &action) ==
        ASTRA_ERROR_INVALID_HANDLE);
    records[0].flags = 0u;
    AstraVolumeReference stale = {5u, 10u, 1u};
    assert(astra_volume_action_begin(7u, 8u, &catalog, &stale, 0, &action) ==
        ASTRA_ERROR_INVALID_HANDLE);
    assert(action.removal.mount.id == 0u);

    records[1].class_id = ASTRA_VOLUME_OPTICAL;
    assert(astra_volume_action_begin(7u, 8u, &catalog, &mount, 1, &action) == ASTRA_OK);
    records[0].state = ASTRA_VOLUME_UNMOUNTED;
    scripted_state = ASTRA_VOLUME_REMOVAL_COMPLETE;
    admission = ASTRA_ERROR_IO;
    assert(astra_volume_action_advance(7u, &catalog, &action) == ASTRA_OK);
    assert(action.finished && action.eject_failed &&
        action.failure == ASTRA_STATUS_IO);

    records[0].origin = ASTRA_VOLUME_ORIGIN_DEVICE;
    records[0].state = ASTRA_VOLUME_MOUNTED;
    records[1].state = ASTRA_VOLUME_UNMOUNTED;
    records[3] = (AstraVolumeRecord){.id = 50u, .generation = 1u,
        .parent = 30u, .parent_generation = 1u,
        .kind = ASTRA_VOLUME_SOURCE, .origin = ASTRA_VOLUME_ORIGIN_DEVICE,
        .class_id = ASTRA_VOLUME_OPTICAL};
    records[4] = (AstraVolumeRecord){.id = 40u, .generation = 1u,
        .parent = 50u, .parent_generation = 1u,
        .kind = ASTRA_VOLUME_MOUNT, .state = ASTRA_VOLUME_MOUNTED,
        .origin = ASTRA_VOLUME_ORIGIN_DEVICE};
    catalog.count = 5u;
    action = (AstraVolumeAction){0};
    admission = ASTRA_OK;
    held_mount = 40u;
    uint32_t before = controls;
    assert(astra_volume_action_begin(7u, 8u, &catalog, &mount, 1, &action) ==
        ASTRA_ERROR_BUSY);
    assert(controls == before && action.removal.mount.id == 0u &&
        action.failure_mount.id == 40u);

    held_mount = 0u;
    action = (AstraVolumeAction){0};
    assert(astra_volume_action_begin(7u, 8u, &catalog, &mount, 1, &action) == ASTRA_OK);
    records[0].state = ASTRA_VOLUME_UNMOUNTED;
    scripted_state = ASTRA_VOLUME_REMOVAL_COMPLETE;
    scripted_status = 0u;
    assert(astra_volume_action_advance(7u, &catalog, &action) == ASTRA_OK);
    assert(operation == ASTRA_VOLUME_UNMOUNT && controlled_mount == 40u &&
        action.removal.mount.id == 40u && !action.finished);
    records[4].state = ASTRA_VOLUME_UNMOUNTED;
    assert(astra_volume_action_advance(7u, &catalog, &action) == ASTRA_OK);
    assert(operation == ASTRA_VOLUME_EJECT && controlled_mount == 20u &&
        action.eject_admitted && !action.finished);
    records[1].state = ASTRA_VOLUME_LOST;
    assert(astra_volume_action_advance(7u, &catalog, &action) == ASTRA_OK);
    assert(action.finished && action.failure == 0u);

    records[0].state = ASTRA_VOLUME_MOUNTED;
    records[1].state = ASTRA_VOLUME_UNMOUNTED;
    records[4].state = ASTRA_VOLUME_MOUNTED;
    action = (AstraVolumeAction){0};
    assert(astra_volume_action_begin(7u, 8u, &catalog, &mount, 1, &action) == ASTRA_OK);
    records[0].state = ASTRA_VOLUME_UNMOUNTED;
    admission = ASTRA_ERROR_BUSY;
    assert(astra_volume_action_advance(7u, &catalog, &action) == ASTRA_OK);
    assert(action.finished && action.failure == ASTRA_STATUS_BUSY &&
        action.failure_mount.id == 40u);
    puts("shared volume action tests passed");
    return 0;
}

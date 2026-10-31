#include "volume_action.h"

#include <astra/result.h>

AstraResult astra_volume_action_begin(AstraHandle administrator,
    const AstraVolumeSnapshot *catalog, const AstraVolumeReference *mount,
    int eject, AstraVolumeAction *action)
{
    if (administrator == ASTRA_INVALID_HANDLE || catalog == NULL ||
        mount == NULL || action == NULL || mount->id == 0u)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    const AstraVolumeRecord *record = astra_volume_snapshot_find(catalog, mount);
    if (record == NULL || record->kind != ASTRA_VOLUME_MOUNT ||
        record->state != ASTRA_VOLUME_MOUNTED ||
        (record->flags & (ASTRA_VOLUME_SYSTEM | ASTRA_VOLUME_TEMPORARY)) != 0u)
        return ASTRA_ERROR_INVALID_HANDLE;
    const AstraVolumeRecord *source = NULL;
    if (eject) {
        source = astra_volume_snapshot_ancestor(catalog, mount,
            ASTRA_VOLUME_SOURCE);
        if (source == NULL || source->origin != ASTRA_VOLUME_ORIGIN_DEVICE ||
            source->class_id != ASTRA_VOLUME_OPTICAL)
            return ASTRA_ERROR_INVALID_ARGUMENT;
    }
    AstraVolumeAction next = {0};
    next.removal.mount = *mount;
    if (record->origin == ASTRA_VOLUME_ORIGIN_IMAGE)
        next.removal.image_source = (AstraVolumeReference){mount->epoch,
            record->parent, record->parent_generation};
    if (source != NULL)
        next.eject_source = (AstraVolumeReference){catalog->epoch,
            source->id, source->generation};
    AstraVolumeControl control = {.reference = *mount,
        .operation = ASTRA_VOLUME_UNMOUNT};
    AstraVolumeReference admitted = {0};
    AstraResult result = astra_volume_control(administrator, &control, &admitted);
    if (result == ASTRA_OK) *action = next;
    return result;
}

AstraResult astra_volume_action_advance(AstraHandle administrator,
    const AstraVolumeSnapshot *catalog, AstraVolumeAction *action)
{
    if (administrator == ASTRA_INVALID_HANDLE || catalog == NULL ||
        action == NULL || action->removal.mount.id == 0u)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (action->finished) return ASTRA_OK;
    AstraResult result = astra_volume_removal_advance(administrator, catalog,
        &action->removal);
    if (result != ASTRA_OK) return result;
    if (action->removal.state == ASTRA_VOLUME_REMOVAL_REFUSED) {
        action->failure = action->removal.status;
        action->finished = 1u;
        return ASTRA_OK;
    }
    if (action->removal.state == ASTRA_VOLUME_REMOVAL_RETAINED &&
        action->removal.status != 0u && !action->reported) {
        action->failure = action->removal.status;
        action->reported = 1u;
    }
    if (action->removal.state == ASTRA_VOLUME_REMOVAL_PROVIDER_RETIRED) {
        action->finished = 1u;
        return ASTRA_OK;
    }
    if (action->removal.state != ASTRA_VOLUME_REMOVAL_COMPLETE)
        return ASTRA_OK;
    if (action->eject_source.id == 0u) {
        action->finished = 1u;
        return ASTRA_OK;
    }
    if (!action->eject_admitted) {
        AstraVolumeControl control = {.reference = action->eject_source,
            .operation = ASTRA_VOLUME_EJECT};
        AstraVolumeReference admitted = {0};
        result = astra_volume_control(administrator, &control, &admitted);
        if (result == ASTRA_OK) action->eject_admitted = 1u;
        else {
            action->failure = astra_status_from_result(result);
            action->eject_failed = 1u;
            action->finished = 1u;
        }
        return ASTRA_OK;
    }
    const AstraVolumeRecord *source = NULL;
    if (catalog->epoch == action->eject_source.epoch)
        for (uint32_t at = 0u; at < catalog->count; ++at)
            if (catalog->records[at].id == action->eject_source.id) {
                source = &catalog->records[at];
                break;
            }
    if (source != NULL &&
        source->generation == action->eject_source.generation &&
        source->state == ASTRA_VOLUME_UNMOUNTED && source->status != 0u) {
        action->failure = source->status;
        action->eject_failed = 1u;
        action->finished = 1u;
    } else if (source == NULL ||
        source->generation != action->eject_source.generation ||
        source->state == ASTRA_VOLUME_LOST)
        action->finished = 1u;
    return ASTRA_OK;
}

#include "volume_action.h"

#include <astra/result.h>

static int same_eject_medium(const AstraVolumeSnapshot *catalog,
    const AstraVolumeRecord *record, const AstraVolumeReference *medium)
{
    if (record->kind != ASTRA_VOLUME_MOUNT) return 0;
    const AstraVolumeReference reference = {catalog->epoch, record->id,
        record->generation};
    const AstraVolumeRecord *ancestor = astra_volume_snapshot_ancestor(
        catalog, &reference, ASTRA_VOLUME_MEDIUM);
    return ancestor != NULL && ancestor->id == medium->id &&
        ancestor->generation == medium->generation;
}

AstraResult astra_volume_action_begin(AstraHandle administrator,
    AstraHandle inspector,
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
    const AstraVolumeRecord *source = NULL, *medium = NULL;
    if (eject) {
        source = astra_volume_snapshot_ancestor(catalog, mount,
            ASTRA_VOLUME_SOURCE);
        if (source == NULL || source->origin != ASTRA_VOLUME_ORIGIN_DEVICE ||
            source->class_id != ASTRA_VOLUME_OPTICAL)
            return ASTRA_ERROR_INVALID_ARGUMENT;
        medium = astra_volume_snapshot_ancestor(catalog, mount,
            ASTRA_VOLUME_MEDIUM);
        if (medium == NULL || inspector == ASTRA_INVALID_HANDLE)
            return ASTRA_ERROR_INVALID_ARGUMENT;
        /* All partitions share one medium. Check every mounted child before
         * admitting any unmount, so known holders cannot cause partial eject. */
        for (uint32_t at = 0u; at < catalog->count; ++at) {
            const AstraVolumeRecord *sibling = &catalog->records[at];
            if (sibling->state != ASTRA_VOLUME_MOUNTED ||
                !same_eject_medium(catalog, sibling,
                    &(AstraVolumeReference){catalog->epoch, medium->id,
                        medium->generation})) continue;
            if ((sibling->flags & (ASTRA_VOLUME_SYSTEM |
                    ASTRA_VOLUME_TEMPORARY)) != 0u)
                return ASTRA_ERROR_PERMISSION;
            AstraVolumeReference reference = {catalog->epoch, sibling->id,
                sibling->generation};
            AstraVolumeUsageSnapshot usage = ASTRA_VOLUME_USAGE_SNAPSHOT_INIT;
            AstraResult checked = astra_volume_usage_snapshot_open(inspector,
                &reference, &usage);
            if (checked == ASTRA_OK && usage.count != 0u)
                checked = ASTRA_ERROR_BUSY;
            astra_volume_usage_snapshot_close(&usage);
            if (checked != ASTRA_OK) {
                if (checked == ASTRA_ERROR_BUSY)
                    action->failure_mount = reference;
                return checked;
            }
        }
    }
    AstraVolumeAction next = {0};
    next.removal.mount = *mount;
    if (record->origin == ASTRA_VOLUME_ORIGIN_IMAGE)
        next.removal.image_source = (AstraVolumeReference){mount->epoch,
            record->parent, record->parent_generation};
    if (source != NULL)
        next.eject_source = (AstraVolumeReference){catalog->epoch,
            source->id, source->generation};
    if (medium != NULL)
        next.eject_medium = (AstraVolumeReference){catalog->epoch,
            medium->id, medium->generation};
    AstraVolumeControl control = {.reference = *mount,
        .operation = ASTRA_VOLUME_UNMOUNT};
    AstraVolumeReference admitted = {0};
    AstraResult result = astra_volume_control(administrator, &control, &admitted);
    if (result == ASTRA_OK) *action = next;
    else if (result == ASTRA_ERROR_BUSY) action->failure_mount = *mount;
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
    const AstraVolumeRecord *optical_source = astra_volume_snapshot_find(
        catalog, &action->eject_source);
    if (optical_source == NULL ||
        optical_source->state == ASTRA_VOLUME_LOST) {
        action->finished = 1u;
        return ASTRA_OK;
    }
    /* Unmount every mounted partition under the same physical medium before
     * asking the provider to eject it. The provider itself enforces this. */
    for (uint32_t at = 0u; at < catalog->count; ++at) {
        const AstraVolumeRecord *sibling = &catalog->records[at];
        if (!same_eject_medium(catalog, sibling, &action->eject_medium))
            continue;
        if (sibling->state == ASTRA_VOLUME_MOUNTED) {
            AstraVolumeReference reference = {catalog->epoch, sibling->id,
                sibling->generation};
            AstraVolumeControl control = {.reference = reference,
                .operation = ASTRA_VOLUME_UNMOUNT};
            AstraVolumeReference admitted = {0};
            result = astra_volume_control(administrator, &control, &admitted);
            if (result == ASTRA_OK) {
                action->removal = (AstraVolumeRemoval){.mount = reference};
                return ASTRA_OK;
            }
            action->failure_mount = reference;
            action->failure = astra_status_from_result(result);
            action->finished = 1u;
            return ASTRA_OK;
        }
        if (sibling->state == ASTRA_VOLUME_MOUNTING ||
            sibling->state == ASTRA_VOLUME_QUIESCING ||
            (sibling->flags & ASTRA_VOLUME_CLEANUP_PENDING) != 0u)
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

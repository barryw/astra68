#ifndef ASTRA_VOLUME_ACTION_H
#define ASTRA_VOLUME_ACTION_H

#include <astra/volume.h>

/* Exact asynchronous removal. Optical eject first removes every mounted
 * partition of the same medium, then asks the provider to eject it. */
typedef struct AstraVolumeAction {
    AstraVolumeRemoval removal;
    AstraVolumeReference eject_source;
    AstraVolumeReference eject_medium;
    AstraVolumeReference failure_mount;
    uint32_t eject_admitted;
    uint32_t eject_failed;
    uint32_t failure;
    uint32_t reported;
    uint32_t finished;
} AstraVolumeAction;

/* Admission owns no data grant. Eject checks all mounted partitions for known
 * holders before starting. BUSY callers inspect failure_mount when present. */
AstraResult astra_volume_action_begin(AstraHandle administrator,
    AstraHandle inspector,
    const AstraVolumeSnapshot *catalog, const AstraVolumeReference *mount,
    int eject, AstraVolumeAction *action);

/* Advance from one coherent catalog. Keep observing until finished. */
AstraResult astra_volume_action_advance(AstraHandle administrator,
    const AstraVolumeSnapshot *catalog, AstraVolumeAction *action);

#endif

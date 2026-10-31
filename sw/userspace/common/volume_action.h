#ifndef ASTRA_VOLUME_ACTION_H
#define ASTRA_VOLUME_ACTION_H

#include <astra/volume.h>

/* One exact asynchronous removal, optionally followed by optical eject. */
typedef struct AstraVolumeAction {
    AstraVolumeRemoval removal;
    AstraVolumeReference eject_source;
    uint32_t eject_admitted;
    uint32_t eject_failed;
    uint32_t failure;
    uint32_t reported;
    uint32_t finished;
} AstraVolumeAction;

/* Admission owns no data grant. BUSY callers may inspect exact holders. */
AstraResult astra_volume_action_begin(AstraHandle administrator,
    const AstraVolumeSnapshot *catalog, const AstraVolumeReference *mount,
    int eject, AstraVolumeAction *action);

/* Advance from one coherent catalog. Keep observing until finished. */
AstraResult astra_volume_action_advance(AstraHandle administrator,
    const AstraVolumeSnapshot *catalog, AstraVolumeAction *action);

#endif

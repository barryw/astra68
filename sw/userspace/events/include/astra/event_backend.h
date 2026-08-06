#ifndef ASTRA_EVENT_BACKEND_H
#define ASTRA_EVENT_BACKEND_H

#include <stdint.h>

#include <astra/event_catalog.h>
#include <astra/event_store.h>
#include <astra/vfs_backend.h>

/*
 * EVENTS: as a filesystem.
 *
 * This is an AstraVfsBackendOps like the ext4 one, so the service core, the
 * wire format, the Kit and the shell never learn that a second kind of
 * filesystem exists -- they learn there is a second mount. Reading history is
 * `cat`, watching it is re-reading a file that grew, and narrowing it is naming
 * a different file.
 *
 *   EVENTS:
 *     boot/current/all                every stored event of this boot
 *     boot/current/notice             ... and above
 *     boot/current/warning
 *     boot/current/error
 *     boot/current/earliest           the boot ring: the first events, kept
 *     activity/0000001a               one request across every process
 *     subsystem/shell                 one subsystem's events
 *
 * `boot/-1` and `boot/-2` are in the design and are not here: the store is RAM
 * until it is written to the state volume, and a directory that existed and was
 * always empty would be a promise the machine cannot keep.
 *
 * Text is rendered at read time from the catalog, so the file has no size until
 * it is read -- which is what the handler contract was written to allow, and
 * why nothing here materialises a listing into memory.
 *
 * Every write verb is refused. The rights on the binding refuse them first;
 * this is the second refusal, and it exists because a store whose immutability
 * depends on one check has one check to get wrong.
 */

#define ASTRA_EVENTS_NODE_MAX 8u

typedef struct AstraEventsNode {
    uint8_t  used;
    uint8_t  kind;
    uint8_t  level_min;
    uint8_t  subsystem;      /* ASTRA_EVENT_SUBSYSTEM_MAX means any */
    uint32_t activity;
    uint8_t  activity_filter;
    /*
     * Where the last read stopped, so a sequential read does not re-render the
     * file from the beginning for every page. A reader that seeks elsewhere
     * simply pays the walk; `cat` does not.
     */
    uint32_t memo_offset;
    uint32_t memo_index[3];   /* one per merged tier; [0] alone for the boot ring */
} AstraEventsNode;

typedef struct AstraEventsBackend {
    const AstraEventStore *store;
    const AstraEventCatalog *catalog;
    AstraEventsNode nodes[ASTRA_EVENTS_NODE_MAX];
} AstraEventsBackend;

int astra_events_backend_init(AstraEventsBackend *backend,
                              const AstraEventStore *store,
                              const AstraEventCatalog *catalog);

const AstraVfsBackendOps *astra_events_backend_ops(void);

#endif

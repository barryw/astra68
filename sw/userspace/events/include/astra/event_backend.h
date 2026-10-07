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
 *     log/syslog                      the text log (docs/SYSTEM_LOG.md)
 *     log/syslog.1 .. syslog.3        its rotations, oldest last, when present
 *
 * `boot/-1` is present when a valid durable snapshot was recovered. One prior
 * boot is retained; see the bounded ceiling beside the supervisor's two-bank
 * snapshot rotation.
 *
 * Text is rendered at read time from the catalog, so the file has no size until
 * it is read -- which is what the handler contract was written to allow, and
 * why nothing here materialises a listing into memory.
 *
 * Every write verb is refused. The rights on the binding refuse them first;
 * this is the second refusal, and it exists because a store whose immutability
 * depends on one check has one check to get wrong.
 */

/* Rotations of the syslog the service keeps: syslog.1 .. syslog.N. */
#define ASTRA_EVENTS_LOG_KEEP 3u

typedef struct AstraEventsNode {
    uint8_t  used;
    uint8_t  kind;
    uint8_t  level_min;
    uint8_t  subsystem;      /* ASTRA_EVENT_SUBSYSTEM_MAX means any */
    uint8_t  previous_boot;
    uint8_t  log_index;      /* KIND_LOG_FILE: 0 is syslog, N syslog.N */
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
    const AstraEventStore *previous;
    const AstraEventCatalog *catalog;
    AstraEventsNode *nodes;
    uint32_t node_capacity;
    /*
     * Bytes of syslog file `index`: 0 is the current `syslog`, 1 to
     * ASTRA_EVENTS_LOG_KEEP its rotations `syslog.N`. NULL when the service
     * has no log (then `log` does not exist). Returns nonzero with `*moved`
     * (0 at the end, or for a file that does not exist), or 0 on an I/O
     * failure.
     */
    int (*log_read)(void *context, uint32_t index, uint64_t offset,
                    void *buffer, uint32_t length, uint32_t *moved);
    /* File `index`'s size in `*bytes`, 0 when it does not exist; returns 0
     * when it cannot be had (then it reports 0). May be NULL, and then no
     * rotation is served. */
    int (*log_size)(void *context, uint32_t index, uint64_t *bytes);
    void *log_context;
} AstraEventsBackend;

#define ASTRA_EVENTS_BACKEND_INIT {0}

int astra_events_backend_init(AstraEventsBackend *backend,
                              const AstraEventStore *store,
                              const AstraEventStore *previous,
                              const AstraEventCatalog *catalog);

const AstraVfsBackendOps *astra_events_backend_ops(void);

#endif

/*
 * The events service, hosted by the supervisor for the same reason the storage
 * one is: userspace cannot start a process yet. Every caller already goes
 * through the protocol, so the day a loader exists this file is replaced by a
 * launch and a port transport, and no caller changes.
 *
 * Nothing here emits an event. An events service that logs about logging is
 * how a logging subsystem takes a machine down, and the rule has to be
 * structural rather than careful -- so this file does not include
 * astra/event_emit.h and must not grow an include of it.
 */

#include <events_host.h>
#include <vfs_host.h>

#include <astra/event_backend.h>
#include <astra/event_catalog.h>
#include <astra/event_store.h>
#include <astra/runtime.h>
#include <astra/vfs_client.h>
#include <astra/vfs_local_transport.h>
#include <astra/vfs_port_transport.h>
#include <astra/vfs_service_core.h>

/* One request deep. Reading history is not a hot path. */
#define EVENTS_PORT_MESSAGES 1u
#define EVENTS_PORT_BUDGET 2u

/*
 * 256 records is 18 KiB of BSS, and the whole store: four tiers and the boot
 * ring inside one budget, which is the single number the design says a person
 * configures. It is provisional by design -- §8.4's eviction accounting is
 * what will correct it against a real workload rather than an opinion.
 */
#define EVENTS_RECORD_MAX 256u

/*
 * Room for 64 descriptors. The catalog is the .astra_events section verbatim,
 * so this is a ceiling on call sites in the image rather than on events, and
 * the supervisor has four today. A catalog that does not fit is refused whole:
 * half a catalog renders the wrong message under the right id.
 */
#define EVENTS_CATALOG_MAX (64u * ASTRA_EVENT_DESCRIPTOR_SIZE)

static AstraEventStored event_records[EVENTS_RECORD_MAX];
/* Aligned by being descriptors: the catalog reader indexes them in place. */
static AstraEventDescriptor catalog_bytes[EVENTS_CATALOG_MAX /
                                          ASTRA_EVENT_DESCRIPTOR_SIZE];
static AstraEventCatalog catalog;
static AstraEventStore store;
static AstraEventsBackend backend;
static AstraVfsService service;
static AstraVfsClient client;
static AstraVfsPortService events_port;
static uint32_t events_handle;
static uint32_t events_receive;
static uint32_t debug_handle;
static uint32_t drain_cursor;
/* Why the catalog is not loaded, when it is not: the status that refused it. */
static uint32_t catalog_status;
static int events_ready;

/*
 * The catalog off SYS:, or nothing. Nothing is survivable: every event still
 * renders, as its message id, which is honest and still greppable -- and it is
 * what a machine whose system volume predates the build would otherwise get
 * wrong by rendering somebody else's text under this build's ids.
 */
static void
load_catalog(void)
{
    AstraVfsClient *storage = supervisor_vfs_client();
    const AstraAssign *assign = NULL;
    AstraVfsFile file = ASTRA_VFS_FILE_INVALID;
    char wire[ASTRA_VFS_PATH_MAX];
    uint64_t size = 0u;
    uint32_t offset = 0u;
    uint16_t kind = 0u;

    if (storage == NULL || supervisor_assigns() == NULL) {
        catalog_status = ASTRA_VFS_ERR_NOT_FOUND;
        return;
    }
    catalog_status = astra_assign_resolve(supervisor_assigns(),
                                          "SYS:astra_events.cat",
                                          ASTRA_RIGHT_READ, wire,
                                          sizeof(wire), &assign);
    if (catalog_status != ASTRA_VFS_OK) {
        return;
    }
    catalog_status = astra_vfs_open(storage, wire, ASTRA_VFS_OPEN_READ, &file,
                                    &size, &kind);
    if (catalog_status != ASTRA_VFS_OK) {
        return;
    }
    while (offset < sizeof(catalog_bytes)) {
        uint32_t moved = 0u;
        uint32_t status = astra_vfs_read(
            storage, file, offset, (uint8_t *)catalog_bytes + offset,
            sizeof(catalog_bytes) - offset, &moved);

        if (status != ASTRA_VFS_OK) {
            catalog_status = status;
            break;
        }
        if (moved == 0u) {
            break;
        }
        offset += moved;
    }
    (void)astra_vfs_close(storage, file);
    if (!astra_event_catalog_init(&catalog, catalog_bytes, offset,
                                     ASTRA_EVENT_CATALOG_BASE)) {
        /* Read, but not this build's catalog: refused whole rather than half. */
        catalog_status = ASTRA_VFS_ERR_INVALID;
    }
}

int
supervisor_events_start(uint32_t process_handle)
{
    AstraAssignTable *assigns = supervisor_assigns();

    if (events_ready) {
        return 1;
    }
    if (assigns == NULL) {
        return 0;
    }
    load_catalog();
    if (!astra_event_store_init(&store, event_records, EVENTS_RECORD_MAX,
                                &catalog)) {
        return 0;
    }
    if (!astra_events_backend_init(&backend, &store, &catalog)) {
        return 0;
    }
    if (!astra_vfs_service_init(&service, astra_events_backend_ops(),
                                &backend)) {
        return 0;
    }
    if (astra_vfs_connect(&client, astra_vfs_local_transport, &service) !=
        ASTRA_VFS_OK) {
        return 0;
    }
    /*
     * A port of its own, because EVENTS: is a second service and a child is
     * granted it separately from the volume. Reading history across a process
     * boundary is the same protocol as reading a file, which is the point.
     */
    if (astra_port_create(EVENTS_PORT_MESSAGES,
                          EVENTS_PORT_MESSAGES *
                              (uint32_t)sizeof(AstraVfsRequestMessage),
                          &events_receive, &events_handle) !=
        ASTRA_SYSCALL_OK) {
        return 0;
    }
    if (!astra_vfs_port_service_init(&events_port, events_receive,
                                     &service)) {
        return 0;
    }
    if (supervisor_vfs_register(&client, events_handle) == 0u) {
        return 0;
    }
    /*
     * Read, and only read. Not a policy inside a command -- the rights on the
     * binding are what make "nothing can rm an event" true, and they are the
     * same mechanism that makes SYS: unwritable.
     */
    if (astra_assign_bind(assigns, "EVENTS", events_handle, ASTRA_RIGHT_READ,
                          "") != ASTRA_VFS_OK) {
        return 0;
    }
    debug_handle = process_handle;
    events_ready = 1;
    supervisor_events_pump();
    return 1;
}

uint32_t
supervisor_events_port(void)
{
    return events_ready ? events_handle : 0u;
}

uint32_t
supervisor_events_catalog_count(void)
{
    return catalog.count;
}

uint32_t
supervisor_events_catalog_status(void)
{
    return catalog_status;
}

void
supervisor_events_pump(void)
{
    /*
     * Answering clients comes first and is unconditional: the drain below
     * gives up permanently when the ring refuses it, and a service that
     * stopped answering because its own logging failed would be the logging
     * subsystem taking the machine down by the route this file exists to
     * avoid.
     */
    if (events_ready) {
        (void)astra_vfs_port_service_pump(&events_port, EVENTS_PORT_BUDGET);
    }
    /*
     * Static, not automatic: a user thread gets one 4 KiB stack and a batch of
     * eight drained records is 448 bytes of it. The same reason the shell's
     * input batch lives outside its frame.
     */
    static AstraEventDrained drained[ASTRA_TRACE_READ_BATCH_MAX];
    uint32_t passes = 0u;

    if (!events_ready) {
        return;
    }
    /*
     * Bounded per call. A burst costs several passes rather than a stall,
     * because the drain runs on the thread a person is typing at and logging
     * may never be the reason something else stops.
     */
    while (passes < 4u) {
        uint32_t copied = 0u;
        uint32_t lost = 0u;

        if (astra_trace_read(debug_handle, &drain_cursor, drained,
                             ASTRA_TRACE_READ_BATCH_MAX, &copied,
                             &lost) != ASTRA_SYSCALL_OK) {
            /*
             * No authority, or no ring. Stop trying: a pump that retries a
             * refusal every pass would spend the machine's time saying
             * nothing, and the store already holds whatever arrived before.
             */
            events_ready = 0;
            return;
        }
        astra_event_store_lost(&store, lost);
        for (uint32_t index = 0u; index < copied; ++index) {
            astra_event_store_append(&store, &drained[index]);
        }
        if (copied < ASTRA_TRACE_READ_BATCH_MAX) {
            return;
        }
        ++passes;
    }
}

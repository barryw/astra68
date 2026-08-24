/*
 * The protected events service. Every caller goes through the storage
 * protocol; this process owns the trace drain, bounded store and persistence.
 *
 * Nothing here emits an event. An events service that logs about logging is
 * how a logging subsystem takes a machine down, and the rule has to be
 * structural rather than careful -- so this file does not include
 * astra/event_emit.h and must not grow an include of it.
 */

#include <astra/event_backend.h>
#include <astra/event_catalog.h>
#include <astra/event_control.h>
#include <astra/event_persist.h>
#include <astra/event_store.h>
#include <astra/bytes.h>
#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/service.h>
#include <astra/status.h>
#include <astra/vfs_process.h>
#include <astra/vfs_port_transport.h>
#include <astra/vfs_service_core.h>

/* One request deep. Reading history is not a hot path. */
#define EVENTS_PORT_MESSAGES 1u
#define EVENTS_PORT_BUDGET 2u
/*
 * The request and control ports wake this service immediately. The trace ring
 * is not waitable, so one slow maintenance sweep bounds unattended persistence
 * latency without forcing process switches and PMMU flushes all day.
 */
#define EVENTS_TRACE_POLL_NS 1000000000ull

ASTRA_PROGRAM("events-service", 0, 2, 0, "Barry Walker",
              "Copyright 2026 Barry Walker");

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
static AstraEventStored previous_records[EVENTS_RECORD_MAX];
static struct {
    uint8_t bytes[ASTRA_EVENT_SNAPSHOT_HEADER_SIZE + sizeof(event_records)];
    uint32_t length;
} snapshot_buffer;
/* Aligned by being descriptors: the catalog reader indexes them in place. */
static AstraEventDescriptor catalog_bytes[EVENTS_CATALOG_MAX /
                                          ASTRA_EVENT_DESCRIPTOR_SIZE];
static AstraEventCatalog catalog;
static AstraEventStore store;
static AstraEventStore previous_store;
static AstraEventsBackend backend;
static AstraVfsService service;
static AstraProcessFilesystem process_filesystem =
    ASTRA_PROCESS_FILESYSTEM_INIT;
static AstraVfsPortService events_port;
static uint32_t events_handle;
static uint32_t events_receive;
static uint32_t control_handle;
static uint32_t control_receive;
static uint32_t event_target_handle;
static uint32_t debug_handle;
static uint32_t drain_cursor;
static uint32_t snapshot_boot = 1u;
static uint32_t snapshot_generation;
static uint32_t snapshot_length[2];
static uint8_t snapshot_written[2];
static int persistence_ready;
static int previous_ready;
/*
 * Whether the ring is still worth reading. Separate from `events_ready` on
 * purpose: the drain gives up permanently when the kernel refuses it, and if
 * that flag also gated answering clients then a service would stop serving
 * because its own logging failed -- which is the logging subsystem taking the
 * machine down by exactly the route this file exists to avoid. It was one flag
 * for a while, and a launched program's first request went unanswered for it.
 */
static int drain_ready;
/* Why the catalog is not loaded, when it is not: the status that refused it. */
static uint32_t catalog_status;
static int events_ready;

enum {
    EVENTS_FAIL_STORE = ASTRA_STATUS_PROGRAM_FIRST,
    EVENTS_FAIL_PREVIOUS_STORE,
    EVENTS_FAIL_BACKEND,
    EVENTS_FAIL_SERVICE,
    EVENTS_FAIL_PORT,
    EVENTS_FAIL_TRANSPORT,
    EVENTS_FAIL_CONTROL_PORT
};

static const char *const snapshot_path[] = {
    "STORE:store.0", "STORE:store.1"
};

typedef struct SnapshotFile {
    AstraFile file;
} SnapshotFile;

static int
snapshot_read(void *context, uint32_t offset, void *buffer, uint32_t length)
{
    SnapshotFile *snapshot = context;
    uint8_t *out = buffer;
    uint32_t done = 0u;

    while (done < length) {
        uint32_t moved = 0u;

        if (process_filesystem.library->read_at(
                &snapshot->file, offset + done, out + done, length - done,
                &moved) !=
                ASTRA_VFS_OK || moved == 0u) {
            return 0;
        }
        done += moved;
    }
    return 1;
}

static int
snapshot_buffer_write(void *context, uint32_t offset, const void *buffer,
                      uint32_t length)
{
    (void)context;
    if (offset > sizeof(snapshot_buffer.bytes) ||
        length > sizeof(snapshot_buffer.bytes) - offset) {
        return 0;
    }
    (void)memcpy(snapshot_buffer.bytes + offset, buffer, length);
    if (offset + length > snapshot_buffer.length)
        snapshot_buffer.length = offset + length;
    return 1;
}

static int
snapshot_write(SnapshotFile *snapshot, const void *buffer, uint32_t length)
{
    const uint8_t *in = buffer;
    uint32_t done = 0u;

    while (done < length) {
        uint32_t moved = 0u;

        if (astra_vfs_port_write_bulk(snapshot->file._private_client,
                snapshot->file._private_file, done, in + done,
                length - done, &moved) !=
                ASTRA_VFS_OK || moved == 0u) {
            return 0;
        }
        done += moved;
    }
    return 1;
}

static int
probe_snapshot(uint32_t bank, AstraEventSnapshotInfo *info)
{
    SnapshotFile snapshot = {ASTRA_FILE_INIT};
    AstraFileInfo file_info = ASTRA_FILE_INFO_INIT;
    uint32_t status;

    status = process_filesystem.library->open(
        &process_filesystem.filesystem, snapshot_path[bank],
        ASTRA_VFS_OPEN_READ, &snapshot.file);
    if (status != ASTRA_VFS_OK)
        return 0;
    status = process_filesystem.library->file_info(&snapshot.file,
                                                   &file_info);
    if (status != ASTRA_VFS_OK || file_info.byte_size > UINT32_MAX) {
        (void)process_filesystem.library->close(&snapshot.file);
        return 0;
    }
    status = astra_event_snapshot_probe(snapshot_read, &snapshot,
                                        (uint32_t)file_info.byte_size, info) ?
        ASTRA_VFS_OK : ASTRA_VFS_ERR_INVALID;
    (void)process_filesystem.library->close(&snapshot.file);
    return status == ASTRA_VFS_OK;
}

static int
load_snapshot(uint32_t bank, AstraEventSnapshotInfo *info)
{
    SnapshotFile snapshot = {ASTRA_FILE_INIT};
    AstraFileInfo file_info = ASTRA_FILE_INFO_INIT;
    int loaded;

    if (process_filesystem.library->open(
            &process_filesystem.filesystem, snapshot_path[bank],
            ASTRA_VFS_OPEN_READ, &snapshot.file) != ASTRA_VFS_OK)
        return 0;
    if (process_filesystem.library->file_info(&snapshot.file, &file_info) !=
            ASTRA_VFS_OK || file_info.byte_size > UINT32_MAX) {
        (void)process_filesystem.library->close(&snapshot.file);
        return 0;
    }
    loaded = astra_event_snapshot_load(&previous_store, snapshot_read,
                                       &snapshot,
                                       (uint32_t)file_info.byte_size, info);
    (void)process_filesystem.library->close(&snapshot.file);
    return loaded;
}

static int
generation_newer(uint32_t left, uint32_t right)
{
    return (int32_t)(left - right) > 0;
}

static void
start_persistence(void)
{
    AstraEventSnapshotInfo info[2];
    int valid[2] = {0, 0};
    uint32_t bank = 0u;
    uint32_t status = process_filesystem.library->mkdir(
        &process_filesystem.filesystem, "STORE:");

    if (status != ASTRA_VFS_OK && status != ASTRA_VFS_ERR_EXISTS) {
        return;
    }
    persistence_ready = 1;
    valid[0] = probe_snapshot(0u, &info[0]);
    valid[1] = probe_snapshot(1u, &info[1]);
    if (!valid[0] && !valid[1]) {
        return;
    }
    if (!valid[0] || (valid[1] &&
                      generation_newer(info[1].generation,
                                       info[0].generation))) {
        bank = 1u;
    }
    if (!load_snapshot(bank, &info[bank])) {
        return;
    }
    previous_ready = 1;
    snapshot_generation = info[bank].generation;
    snapshot_boot = info[bank].boot + 1u;
    if (snapshot_boot == 0u) {
        snapshot_boot = 1u;
    }
}

static void
save_snapshot(void)
{
    SnapshotFile snapshot = {ASTRA_FILE_INIT};
    uint32_t next = snapshot_generation + 1u;
    uint32_t bank;
    uint32_t flags = ASTRA_VFS_OPEN_WRITE | ASTRA_VFS_OPEN_CREATE;
    int saved;

    if (!persistence_ready) {
        return;
    }
    if (next == 0u) {
        next = 1u;
    }
    bank = next & 1u;
    snapshot_buffer.length = 0u;
    saved = astra_event_snapshot_save(&store, snapshot_boot, next,
                                      snapshot_buffer_write, NULL);
    if (!saved) {
        persistence_ready = 0;
        return;
    }
    /* Each bank only grows during a boot. Keep its allocated blocks unless
     * the serialized state actually became shorter. */
    if (snapshot_written[bank] == 0u ||
        snapshot_buffer.length < snapshot_length[bank]) {
        flags |= ASTRA_VFS_OPEN_TRUNCATE;
    }
    if (process_filesystem.library->open(
            &process_filesystem.filesystem, snapshot_path[bank], flags,
            &snapshot.file) != ASTRA_VFS_OK) {
        persistence_ready = 0;
        return;
    }
    saved = snapshot_write(&snapshot, snapshot_buffer.bytes,
                           snapshot_buffer.length);
    if (process_filesystem.library->close(&snapshot.file) != ASTRA_VFS_OK) {
        saved = 0;
    }
    if (saved) {
        snapshot_generation = next;
        snapshot_length[bank] = snapshot_buffer.length;
        snapshot_written[bank] = 1u;
    } else {
        /* The other bank remains valid; do not hammer a failing volume. */
        persistence_ready = 0;
    }
}

/*
 * The catalog off SYS:, or nothing. Nothing is survivable: every event still
 * renders, as its message id, which is honest and still greppable -- and it is
 * what a machine whose system volume predates the build would otherwise get
 * wrong by rendering somebody else's text under this build's ids.
 */
static void
load_catalog(void)
{
    AstraFile file = ASTRA_FILE_INIT;
    uint32_t offset = 0u;

    if (process_filesystem.library == NULL) {
        catalog_status = ASTRA_VFS_ERR_NOT_FOUND;
        return;
    }
    catalog_status = process_filesystem.library->open(
        &process_filesystem.filesystem, "SYS:astra_events.cat",
        ASTRA_VFS_OPEN_READ, &file);
    if (catalog_status != ASTRA_VFS_OK) {
        return;
    }
    while (offset < sizeof(catalog_bytes)) {
        uint32_t moved = 0u;
        uint32_t status = process_filesystem.library->read_at(
            &file, offset, (uint8_t *)catalog_bytes + offset,
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
    (void)process_filesystem.library->close(&file);
    if (!astra_event_catalog_init(&catalog, catalog_bytes, offset,
                                     ASTRA_EVENT_CATALOG_BASE)) {
        /* Read, but not this build's catalog: refused whole rather than half. */
        catalog_status = ASTRA_VFS_ERR_INVALID;
    }
}

static void events_pump(void);

static uint32_t
events_start(uint32_t process_handle)
{
    if (events_ready) {
        return ASTRA_STATUS_OK;
    }
    if (process_filesystem.library == NULL) {
        return ASTRA_STATUS_PROTOCOL;
    }
    load_catalog();
    if (!astra_event_store_init(&store, event_records, EVENTS_RECORD_MAX,
                                &catalog)) {
        return EVENTS_FAIL_STORE;
    }
    if (!astra_event_store_init(&previous_store, previous_records,
                                EVENTS_RECORD_MAX, &catalog)) {
        return EVENTS_FAIL_PREVIOUS_STORE;
    }
    start_persistence();
    /*
     * ponytail: two alternating banks retain one prior boot. Add another
     * recovered store and bank only when evidence needs boot/-2; boot/-1 is
     * the queued diagnostic contract and already doubles event-store BSS.
     */
    if (!astra_events_backend_init(&backend, &store,
                                   previous_ready ? &previous_store : NULL,
                                   &catalog)) {
        return EVENTS_FAIL_BACKEND;
    }
    if (!astra_vfs_service_init(&service, astra_events_backend_ops(),
                                &backend)) {
        return EVENTS_FAIL_SERVICE;
    }
    /*
     * A port of its own, because EVENTS: is a second service and a child is
     * granted it separately from the volume. Reading history across a process
     * boundary is the same protocol as reading a file, which is the point.
     */
    if (astra_rt_port_create(EVENTS_PORT_MESSAGES,
                          EVENTS_PORT_MESSAGES *
                          (uint32_t)sizeof(AstraVfsRequestMessage),
                          &events_receive, &events_handle) !=
        ASTRA_SYSCALL_OK) {
        return EVENTS_FAIL_PORT;
    }
    if (!astra_vfs_port_service_init(&events_port, events_receive,
                                     &service)) {
        return EVENTS_FAIL_TRANSPORT;
    }
    if (astra_rt_port_create(EVENTS_PORT_MESSAGES,
                          ASTRA_EVENT_CONTROL_REQUEST_SIZE,
                          &control_receive, &control_handle) !=
        ASTRA_SYSCALL_OK) {
        return EVENTS_FAIL_CONTROL_PORT;
    }
    debug_handle = process_handle;
    events_ready = 1;
    drain_ready = 1;
    events_pump();
    return ASTRA_STATUS_OK;
}

static void
events_pump(void)
{
    /*
     * Answering clients comes first and is unconditional: the drain below
     * gives up permanently when the ring refuses it, and a service that
     * stopped answering because its own logging failed would be the logging
     * subsystem taking the machine down by the route this file exists to
     * avoid.
     */
    if (events_ready) {
        /*
         * ponytail: control remains boot-global. Add process-aware catalog
         * routing before making thresholds producer-specific.
         */
        (void)astra_event_control_proxy_pump(control_receive,
                                             event_target_handle, 1u);
        (void)astra_vfs_port_service_pump(&events_port, EVENTS_PORT_BUDGET);
    }
    /*
     * Static, not automatic: a user thread gets one 4 KiB stack and a batch of
     * eight drained records is 448 bytes of it. The same reason the shell's
     * input batch lives outside its frame.
     */
    static AstraEventDrained drained[ASTRA_TRACE_READ_BATCH_MAX];
    uint32_t passes = 0u;
    int changed = 0;

    if (!drain_ready) {
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
             *
             * Only the drain stops. The service keeps answering: what it
             * already holds is still worth reading, and a client asking for it
             * has nothing to do with the ring being unreadable.
             */
            drain_ready = 0;
            break;
        }
        astra_event_store_lost(&store, lost);
        changed |= lost != 0u;
        for (uint32_t index = 0u; index < copied; ++index) {
            astra_event_store_append(&store, &drained[index]);
        }
        changed |= copied != 0u;
        if (copied < ASTRA_TRACE_READ_BATCH_MAX) {
            break;
        }
        ++passes;
    }
    if (changed) {
        save_snapshot();
    }
}

static const AstraStartupCapability *
capability(const AstraStartupInfo *startup,
           const AstraStartupCapability *capabilities, const char *name)
{
    for (uint32_t index = 0u; index < startup->capability_count; ++index) {
        if (astra_capability_name_equal(capabilities[index].name, name))
            return &capabilities[index];
    }
    return NULL;
}

static void ready(uint32_t handle, uint32_t status, uint32_t service_handle,
                  uint32_t service_control)
{
    AstraServiceReady message;
    uint32_t carried[2];

    (void)memset(&message, 0, sizeof(message));
    message.header.total_size = sizeof(message);
    message.header.header_size = ASTRA_MESSAGE_HEADER_SIZE;
    message.header.protocol = ASTRA_SERVICE_PROTOCOL;
    message.header.protocol_version = ASTRA_SERVICE_VERSION;
    message.header.operation = ASTRA_SERVICE_READY;
    message.status = status;
    carried[0] = service_handle;
    carried[1] = service_control;
    (void)astra_port_send(handle, &message, sizeof(message),
                          status == ASTRA_STATUS_OK ? carried : NULL,
                          status == ASTRA_STATUS_OK ? 2u : 0u);
}

int astra_main(const AstraStartupInfo *startup)
{
    const AstraStartupCapability *capabilities;
    const AstraStartupCapability *bootstrap;
    const AstraStartupCapability *event_target;
    uint32_t status = ASTRA_STATUS_OK;

    if (!astra_startup_validate(startup) ||
        startup->capabilities_address == 0u)
        return ASTRA_STATUS_INVALID;
    capabilities = (const AstraStartupCapability *)(uintptr_t)
        startup->capabilities_address;
    bootstrap = capability(startup, capabilities,
                           ASTRA_CAPABILITY_SERVICE_READY);
    event_target = capability(startup, capabilities,
                              ASTRA_CAPABILITY_EVENT_TARGET);
    if (bootstrap == NULL || event_target == NULL)
        return ASTRA_STATUS_BAD_HANDLE;
    event_target_handle = event_target->handle;
    /* First dynamic-library consumer on a cold boot seeds the kernel cache. */
    status = astra_process_filesystem_open_bootstrap(&process_filesystem,
                                                     startup);
    if (status == ASTRA_VFS_OK)
        status = events_start(startup->process_handle);
    ready(bootstrap->handle, status,
          status == ASTRA_STATUS_OK ? events_handle : 0u,
          status == ASTRA_STATUS_OK ? control_handle : 0u);
    (void)astra_close(bootstrap->handle);
    if (status != ASTRA_STATUS_OK)
        return (int)status;
    for (;;) {
        uint32_t waits[] = {control_receive, events_receive};
        uint64_t deadline;
        uint32_t wait_status;

        events_pump();
        deadline = astra_clock_monotonic() + EVENTS_TRACE_POLL_NS;
        wait_status = astra_wait_multiple(waits, 2u, deadline, NULL, NULL);
        if (wait_status != ASTRA_SYSCALL_OK &&
            wait_status != ASTRA_SYSCALL_TIMED_OUT)
            return ASTRA_STATUS_PEER_DEAD;
    }
}

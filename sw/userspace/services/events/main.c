/*
 * The protected events service. Every caller goes through the storage
 * protocol; this process owns the trace drain, bounded store and persistence.
 *
 * It is also the log service (docs/SYSTEM_LOG.md sections 3 and 5): it
 * publishes LOG, maps every process's log ring, and appends their lines and
 * the kernel ring's text to STORE:syslog and WORK:.astra/syslog, served back
 * as EVENTS:log/syslog.
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
#include <astra/clock.h>
#include <astra/event.h>
#include <astra/limits.h>
#include <astra/log_clients.h>
#include <astra/log_file.h>
#include <astra/log_format.h>
#include <astra/log_ring.h>
#include <astra/message_abi.h>
#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/service.h>
#include <astra/status.h>
#include <astra/string.h>
#include <astra/time_page.h>
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

/*
 * The LOG port carries setups and doorbells. A full queue means a wake is
 * already pending, so it only has to be deep enough for a launch burst.
 */
#define LOG_PORT_MESSAGES 16u
#define LOG_MAX_BYTES (1024u * 1024u)
#define LOG_KEEP 3u
#define LOG_PATH_MAX 64u
/* Kernel-ring text lines collect here; each client drain pass appends after
 * them, so a pass is one write per file. */
#define LOG_KERNEL_BYTES ASTRA_LOG_DRAIN_OUT_BYTES
/* Drain passes per pump before the ports are answered again. */
#define LOG_DRAIN_PASSES 8u

ASTRA_PROGRAM("events-service", 0, 3, 0, "Barry Walker",
              "Copyright 2026 Barry Walker");

/*
 * 256 records is 18 KiB of BSS, and the whole store: four tiers and the boot
 * ring inside one budget, which is the single number the design says a person
 * configures. It is provisional by design -- §8.4's eviction accounting is
 * what will correct it against a real workload rather than an opinion.
 */
#define EVENTS_RECORD_MAX 256u

static AstraEventStored event_records[EVENTS_RECORD_MAX];
static AstraEventStored previous_records[EVENTS_RECORD_MAX];
static struct {
    uint8_t bytes[ASTRA_EVENT_SNAPSHOT_HEADER_SIZE + sizeof(event_records)];
    uint32_t length;
} snapshot_buffer;
static void *catalog_bytes;
static AstraEventCatalog catalog;
static AstraEventStore store;
static AstraEventStore previous_store;
static AstraEventsBackend backend;
static AstraVfsService service;
static AstraVfsSessionSlot service_sessions[ASTRA_VFS_SESSION_MAX];
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

static uint32_t log_handle;        /* published as LOG */
static uint32_t log_receive;
static AstraLogClients log_clients;
static uint8_t log_gone[ASTRA_LOG_CLIENTS_MAX]; /* producer died: last drain */
static AstraLogFile log_store;     /* /store/syslog */
static AstraLogFile log_host;      /* /work/.astra/syslog */
static char log_batch[LOG_KERNEL_BYTES + ASTRA_LOG_DRAIN_OUT_BYTES];
static uint32_t log_used;          /* kernel text waiting in log_batch */
static uint32_t log_day;           /* YYYYMMDD of the newest dated line */
static int log_more;               /* the drain stopped with work left */
/* One kernel-ring text line, joined from its CONTINUED chunks. */
static struct {
    uint32_t process;
    uint32_t thread;
    uint32_t level;
    uint32_t length;
    uint64_t monotonic;
    char text[ASTRA_LOG_TEXT_MAX];
} kernel_line;

enum {
    EVENTS_FAIL_STORE = ASTRA_STATUS_PROGRAM_FIRST,
    EVENTS_FAIL_PREVIOUS_STORE,
    EVENTS_FAIL_BACKEND,
    EVENTS_FAIL_SERVICE,
    EVENTS_FAIL_PORT,
    EVENTS_FAIL_TRANSPORT,
    EVENTS_FAIL_CONTROL_PORT,
    EVENTS_FAIL_LOG_PORT
};

static const char *const snapshot_path[] = {
    "/store/store.0", "/store/store.1"
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

        if (astra_filesystem_read_at(
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

        if (astra_filesystem_write_at(
                &snapshot->file, done, in + done, length - done, &moved) !=
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

    status = astra_filesystem_open(
        &process_filesystem.filesystem, snapshot_path[bank],
        ASTRA_VFS_OPEN_READ, &snapshot.file);
    if (status != ASTRA_VFS_OK)
        return 0;
    status = astra_filesystem_file_info(&snapshot.file,
                                                   &file_info);
    if (status != ASTRA_VFS_OK || file_info.byte_size > UINT32_MAX) {
        (void)astra_filesystem_close(&snapshot.file);
        return 0;
    }
    status = astra_event_snapshot_probe(snapshot_read, &snapshot,
                                        (uint32_t)file_info.byte_size, info) ?
        ASTRA_VFS_OK : ASTRA_VFS_ERR_INVALID;
    (void)astra_filesystem_close(&snapshot.file);
    return status == ASTRA_VFS_OK;
}

static int
load_snapshot(uint32_t bank, AstraEventSnapshotInfo *info)
{
    SnapshotFile snapshot = {ASTRA_FILE_INIT};
    AstraFileInfo file_info = ASTRA_FILE_INFO_INIT;
    int loaded;

    if (astra_filesystem_open(
            &process_filesystem.filesystem, snapshot_path[bank],
            ASTRA_VFS_OPEN_READ, &snapshot.file) != ASTRA_VFS_OK)
        return 0;
    if (astra_filesystem_file_info(&snapshot.file, &file_info) !=
            ASTRA_VFS_OK || file_info.byte_size > UINT32_MAX) {
        (void)astra_filesystem_close(&snapshot.file);
        return 0;
    }
    loaded = astra_event_snapshot_load(&previous_store, snapshot_read,
                                       &snapshot,
                                       (uint32_t)file_info.byte_size, info);
    (void)astra_filesystem_close(&snapshot.file);
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
    if (astra_filesystem_open(
            &process_filesystem.filesystem, snapshot_path[bank], flags,
            &snapshot.file) != ASTRA_VFS_OK) {
        persistence_ready = 0;
        return;
    }
    saved = snapshot_write(&snapshot, snapshot_buffer.bytes,
                           snapshot_buffer.length);
    if (saved && astra_filesystem_sync(&snapshot.file) !=
                     ASTRA_VFS_OK) {
        saved = 0;
    }
    if (astra_filesystem_close(&snapshot.file) != ASTRA_VFS_OK) {
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
 * The catalog off /system, or nothing. Nothing is survivable: every event still
 * renders, as its message id, which is honest and still greppable -- and it is
 * what a machine whose system volume predates the build would otherwise get
 * wrong by rendering somebody else's text under this build's ids.
 */
static void
load_catalog(void)
{
    uint32_t length = 0u;

    astra_runtime_deallocate(catalog_bytes);
    catalog_bytes = NULL;
    catalog = (AstraEventCatalog){0};

    if (process_filesystem.filesystem._private_assigns == NULL) {
        catalog_status = ASTRA_VFS_ERR_NOT_FOUND;
        return;
    }
    catalog_status = astra_process_read_file_alloc(
        &process_filesystem, "/system/astra_events.cat", &catalog_bytes, &length);
    if (catalog_status != ASTRA_VFS_OK) {
        return;
    }
    if (!astra_event_catalog_init(&catalog, catalog_bytes, length,
                                  ASTRA_EVENT_CATALOG_BASE)) {
        /* Read, but not this build's catalog: refused whole rather than half. */
        catalog_status = ASTRA_VFS_ERR_INVALID;
    }
}

/* ------------------------------------------------------------------ log */

/* The two syslog directories; each AstraLogFileOps context is one of them. */
static char log_store_dir[] = "/store/";
static char log_host_dir[] = "/work/.astra/";

static int
log_path(char *out, const void *directory, const char *name)
{
    out[0] = '\0';
    return astra_string_concat(out, LOG_PATH_MAX, directory) &&
           astra_string_concat(out, LOG_PATH_MAX, name);
}

static int
log_size(void *context, const char *name, uint64_t *bytes)
{
    char path[LOG_PATH_MAX];
    AstraFileInfo info = ASTRA_FILE_INFO_INIT;
    uint32_t status;

    if (!log_path(path, context, name))
        return 0;
    status = astra_filesystem_stat(&process_filesystem.filesystem, path,
                                   &info);
    if (status == ASTRA_VFS_ERR_NOT_FOUND) {
        *bytes = 0u;
        return 1;
    }
    if (status != ASTRA_VFS_OK)
        return 0;
    *bytes = info.byte_size;
    return 1;
}

static int
log_append(void *context, const char *name, const void *bytes,
           uint32_t length)
{
    char path[LOG_PATH_MAX];
    AstraFile file = ASTRA_FILE_INIT;
    const uint8_t *in = bytes;
    uint32_t done = 0u;

    if (!log_path(path, context, name) ||
        astra_filesystem_open(&process_filesystem.filesystem, path,
                              ASTRA_VFS_OPEN_WRITE | ASTRA_VFS_OPEN_CREATE |
                                  ASTRA_VFS_OPEN_APPEND,
                              &file) != ASTRA_VFS_OK)
        return 0;
    while (done < length) {
        uint32_t moved = 0u;

        if (astra_filesystem_write(&file, in + done, length - done,
                                   &moved) != ASTRA_VFS_OK || moved == 0u)
            break;
        done += moved;
    }
    return astra_filesystem_close(&file) == ASTRA_VFS_OK && done == length;
}

static int
log_rename(void *context, const char *from, const char *to)
{
    char source[LOG_PATH_MAX];
    char target[LOG_PATH_MAX];
    uint32_t status;

    if (!log_path(source, context, from) || !log_path(target, context, to))
        return 0;
    status = astra_filesystem_rename(&process_filesystem.filesystem, source,
                                     target);
    return status == ASTRA_VFS_OK || status == ASTRA_VFS_ERR_NOT_FOUND;
}

static const AstraLogFileOps log_store_ops = {
    log_store_dir, log_size, log_append, log_rename
};
static const AstraLogFileOps log_host_ops = {
    log_host_dir, log_size, log_append, log_rename
};

/* EVENTS:log/syslog: the boot volume's copy, read through. */
static int
syslog_read(void *context, uint64_t offset, void *buffer, uint32_t length,
            uint32_t *moved)
{
    AstraFile file = ASTRA_FILE_INIT;
    uint32_t status;

    (void)context;
    *moved = 0u;
    status = astra_filesystem_open(&process_filesystem.filesystem,
                                   "/store/syslog", ASTRA_VFS_OPEN_READ,
                                   &file);
    if (status == ASTRA_VFS_ERR_NOT_FOUND)
        return 1;
    if (status != ASTRA_VFS_OK)
        return 0;
    status = astra_filesystem_read_at(&file, offset, buffer, length, moved);
    (void)astra_filesystem_close(&file);
    return status == ASTRA_VFS_OK;
}

/*
 * One batch, one append per file. A file that cannot be written drops the
 * batch rather than stalling the drain: log_file.c retries opening it on the
 * next one, and a log that cannot be written is never a service failure.
 */
static void
log_write(const char *bytes, uint32_t length, uint32_t day)
{
    if (length == 0u)
        return;
    (void)astra_log_file_append(&log_store, bytes, length, day);
    (void)astra_log_file_append(&log_host, bytes, length, day);
}

static void
log_line_time(AstraLogLine *line, uint64_t monotonic,
              const AstraTimeSample *now)
{
    line->monotonic_ns = monotonic;
    line->wall_ns = now->wall_ns - now->monotonic_ns + monotonic;
    line->utc_offset = now->utc_offset;
    line->zone = now->zone;
    line->wall_valid = now->wall_valid != 0u;
}

/* `--- boot <time> build <id> ---`: where one boot's lines begin. */
static void
log_boot_marker(void)
{
    static const char hex[] = "0123456789abcdef";
    static const char build[] = " build ";
    static const char tail[] = " ---\n";
    AstraTimeSample now;
    AstraLogLine line = {0};
    /* 9 + a 33-byte timestamp (log_format.h) + 7 + 8 + 5. */
    char text[64] = "--- boot ";
    uint32_t used = 9u;

    astra_time_now(&now);
    log_line_time(&line, now.monotonic_ns, &now);
    used += astra_log_format_time(text + used, 33u, &line);
    for (uint32_t at = 0u; build[at] != '\0'; ++at)
        text[used++] = build[at];
    for (uint32_t shift = 32u; shift != 0u; shift -= 4u)
        text[used++] = hex[((uint32_t)ASTRA_BUILD_ID >> (shift - 4u)) & 0xfu];
    for (uint32_t at = 0u; tail[at] != '\0'; ++at)
        text[used++] = tail[at];
    if (astra_log_local_day(&line) != 0u)
        log_day = astra_log_local_day(&line);
    log_write(text, used, log_day);
}

static const char *
log_ident(uint32_t pid)
{
    if (pid == 0u)
        return "kernel";
    for (uint32_t index = 0u; index < ASTRA_LOG_CLIENTS_MAX; ++index)
        if (log_clients.client[index].used &&
            log_clients.client[index].pid == pid)
            return log_clients.client[index].ident;
    return ""; /* the formatter writes "?" */
}

/* The joined kernel-ring line, rendered into the batch. Debug is live
 * commentary and is not stored, as event_store.c does not store it. */
static void
kernel_line_end(const AstraTimeSample *now)
{
    AstraLogLine line;
    uint32_t length = kernel_line.length;

    kernel_line.length = 0u;
    while (length != 0u && (kernel_line.text[length - 1u] == '\n' ||
                            kernel_line.text[length - 1u] == '\r'))
        --length;
    if (length == 0u || kernel_line.level == ASTRA_EVENT_LEVEL_DEBUG)
        return;
    if (ASTRA_LOG_LINE_BYTES(length) > LOG_KERNEL_BYTES - log_used) {
        log_write(log_batch, log_used, log_day);
        log_used = 0u;
    }
    log_line_time(&line, kernel_line.monotonic, now);
    line.level = (uint8_t)kernel_line.level;
    line.stream = kernel_line.process == 0u ? ASTRA_LOG_STREAM_KERNEL :
                                              ASTRA_LOG_STREAM_LOG;
    line.truncated = 0u;
    line.pid = kernel_line.process;
    line.ident = log_ident(kernel_line.process);
    line.text = kernel_line.text;
    line.length = length;
    log_used += astra_log_format(log_batch + log_used,
                                 LOG_KERNEL_BYTES - log_used, &line);
    if (astra_log_local_day(&line) != 0u)
        log_day = astra_log_local_day(&line);
}

/*
 * A text record from the kernel ring: LOG_WRITE from code without a log
 * ring, chunked ASTRA_EVENT_ARGUMENT_MAX bytes at a time with CONTINUED on
 * all but the last.
 *
 * ponytail: one chain is joined at a time, keyed by process and thread; a
 * chunk from anyone else ends the open line early, so two LOG_WRITE lines
 * interleaved in the ring come out split. Keep one chain per thread if that
 * shows up in a real log.
 */
static void
kernel_text(const AstraEventDrained *record, const AstraTimeSample *now)
{
    uint32_t length = record->payload_length;

    if ((record->flags & ASTRA_EVENT_FLAG_INLINE_STRING) == 0u)
        return;
    if (length > ASTRA_EVENT_ARGUMENT_MAX)
        length = ASTRA_EVENT_ARGUMENT_MAX;
    if (kernel_line.length != 0u &&
        (kernel_line.process != record->process ||
         kernel_line.thread != record->thread ||
         kernel_line.length + length > ASTRA_LOG_TEXT_MAX))
        kernel_line_end(now);
    if (kernel_line.length == 0u) {
        kernel_line.process = record->process;
        kernel_line.thread = record->thread;
        kernel_line.level = record->flags & ASTRA_EVENT_LEVEL_MASK;
        kernel_line.monotonic =
            (((uint64_t)record->timestamp_high << 32) |
             record->timestamp_low) * ASTRA_CLOCK_MONOTONIC_RESOLUTION_NS;
    }
    (void)memcpy(kernel_line.text + kernel_line.length, record->payload,
                 length);
    kernel_line.length += length;
    if ((record->flags & ASTRA_EVENT_FLAG_CONTINUED) == 0u)
        kernel_line_end(now);
}

/*
 * Take every setup off the LOG port. Doorbells only wake the service: the
 * drain reads every ring anyway. Nothing a sender put on the port can wedge
 * it -- a message too large for a setup is received whole and discarded.
 */
static void
log_accept(void)
{
    static uint8_t junk[LOG_PORT_MESSAGES * sizeof(AstraLogSetup)];
    static uint32_t junk_handles[ASTRA_MESSAGE_HANDLES_MAX];

    for (;;) {
        AstraLogSetup setup;
        uint32_t handles[2] = {0u, 0u};
        uint32_t size = 0u;
        uint32_t count = 0u;
        uint32_t sender = 0u;
        void *address = NULL;
        uint32_t mapped = 0u;
        uint32_t status = astra_port_receive_from(
            log_receive, &setup, sizeof(setup), handles, 2u, &size, &count,
            &sender);

        if (status == ASTRA_SYSCALL_BUFFER_TOO_SMALL) {
            if (astra_port_receive_from(log_receive, junk, sizeof(junk),
                                        junk_handles,
                                        ASTRA_MESSAGE_HANDLES_MAX, &size,
                                        &count, &sender) != ASTRA_SYSCALL_OK)
                return;
            for (uint32_t index = 0u; index < count; ++index)
                (void)astra_close(junk_handles[index]);
            continue;
        }
        if (status != ASTRA_SYSCALL_OK)
            return;
        if (count == 2u &&
            astra_rt_area_map(handles[1],
                              ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE,
                              &address, &mapped) == ASTRA_SYSCALL_OK &&
            astra_log_clients_add(&log_clients, sender, &setup, size,
                                  address, mapped, handles[0],
                                  handles[1]) != NULL)
            continue;
        if (address != NULL)
            (void)astra_rt_area_unmap(address);
        for (uint32_t index = 0u; index < count; ++index)
            (void)astra_close(handles[index]);
    }
}

/* Publishes how far the service has read. Never notifies the kernel ring
 * object (docs/SYSTEM_LOG.md section 3, Doorbell). */
static void
log_consumed(void *context, AstraLogClient *client, uint32_t position)
{
    (void)context;
    __atomic_store_n(&((volatile AstraBulkRingHeader *)(uintptr_t)
                       client->ring)->consumer_position,
                     position, __ATOMIC_RELEASE);
}

static void
log_release(AstraLogClient *client)
{
    (void)astra_rt_area_unmap((void *)(uintptr_t)client->ring);
    (void)astra_close(client->consumer_handle);
    (void)astra_close(client->area_handle);
    client->used = 0u;
}

/*
 * Every ring, drained into the batch after the kernel text, until a pass
 * moves nothing and nothing is pending -- or LOG_DRAIN_PASSES, after which
 * the ports are answered and the service polls rather than sleeps.
 *
 * A producer that died is found first (its consumer endpoint reports
 * PEER_DEAD; the kernel ring object is never notified, so it is never
 * otherwise ready), drained to the end with everyone else, then released.
 */
static void
log_drain(void)
{
    uint32_t pending = 0u;

    for (uint32_t index = 0u; index < ASTRA_LOG_CLIENTS_MAX; ++index) {
        const AstraLogClient *client = &log_clients.client[index];
        uint32_t status;

        if (!client->used || client->dead || log_gone[index])
            continue;
        status = astra_wait_one(client->consumer_handle, 0u /* poll */,
                                NULL);
        log_gone[index] = status == ASTRA_SYSCALL_PEER_DEAD ||
                          status == ASTRA_SYSCALL_CLOSED;
    }
    log_more = 0;
    for (uint32_t pass = 0u;; ++pass) {
        uint32_t moved;

        if (pass == LOG_DRAIN_PASSES) {
            log_more = 1;
            break;
        }
        moved = astra_log_clients_drain(&log_clients, log_batch + log_used,
                                        sizeof(log_batch) - log_used,
                                        log_consumed, NULL, &log_day,
                                        &pending);
        log_write(log_batch, log_used + moved, log_day);
        log_used = 0u;
        if (moved == 0u && pending == 0u)
            break;
    }
    for (uint32_t index = 0u; index < ASTRA_LOG_CLIENTS_MAX; ++index) {
        AstraLogClient *client = &log_clients.client[index];

        if (client->used && (client->dead || (log_gone[index] && !log_more))) {
            log_release(client);
            log_gone[index] = 0u;
        }
    }
}

static void events_pump(void);

static uint32_t
events_start(uint32_t process_handle)
{
    if (events_ready) {
        return ASTRA_STATUS_OK;
    }
    if (process_filesystem.filesystem._private_assigns == NULL) {
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
    {
        void *file_storage = NULL;
        uint32_t file_capacity = 0u;

        if (!astra_vfs_port_quota_storage(sizeof(AstraVfsOpenFile),
                                           &file_storage, &file_capacity) ||
            !astra_vfs_service_init(
                &service, astra_events_backend_ops(), &backend,
                service_sessions, ASTRA_VFS_SESSION_MAX, file_storage,
                file_capacity)) {
            return EVENTS_FAIL_SERVICE;
        }
    }
    /*
     * A port of its own, because EVENTS: is a second service and a child is
     * granted it separately from the volume. Reading history across a process
     * boundary is the same protocol as reading a file, which is the point.
     */
    if (astra_rt_port_create(EVENTS_PORT_MESSAGES,
                          EVENTS_PORT_MESSAGES *
                          (uint32_t)sizeof(AstraVfsRenameRequestMessage),
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
    if (astra_rt_port_create(LOG_PORT_MESSAGES,
                             LOG_PORT_MESSAGES *
                                 (uint32_t)sizeof(AstraLogSetup),
                             &log_receive, &log_handle) != ASTRA_SYSCALL_OK)
        return EVENTS_FAIL_LOG_PORT;
    /*
     * Neither file is a condition of service. One that will not open now is
     * retried on every append; until then its lines are dropped, and EVENTS:
     * is served regardless.
     */
    (void)astra_log_file_open(&log_store, &log_store_ops, "syslog",
                              LOG_MAX_BYTES, LOG_KEEP);
    (void)astra_filesystem_mkdir(&process_filesystem.filesystem,
                                 "/work/.astra");
    (void)astra_log_file_open(&log_host, &log_host_ops, "syslog",
                              LOG_MAX_BYTES, LOG_KEEP);
    log_boot_marker();
    backend.log_read = syslog_read;
    backend.log_context = NULL;
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
        log_accept();
    }
    /* Static, not automatic: keep one page of transfer storage off the stack. */
    static AstraEventDrained drained[ASTRA_MEMORY_PAGE_SIZE /
                                      sizeof(AstraEventDrained)];
    const uint32_t drain_capacity =
        (uint32_t)(sizeof(drained) / sizeof(drained[0]));
    uint32_t passes = 0u;
    int changed = 0;
    AstraTimeSample now;

    if (!drain_ready) {
        if (events_ready)
            log_drain();
        return;
    }
    astra_time_now(&now);
    /*
     * Bounded per call. A burst costs several passes rather than a stall,
     * because the drain runs on the thread a person is typing at and logging
     * may never be the reason something else stops.
     */
    while (passes < 4u) {
        uint32_t copied = 0u;
        uint32_t lost = 0u;

        if (astra_trace_read(debug_handle, &drain_cursor, drained,
                             drain_capacity, &copied, &lost) !=
                ASTRA_SYSCALL_OK) {
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
            kernel_text(&drained[index], &now);
        }
        changed |= copied != 0u;
        if (copied < drain_capacity) {
            break;
        }
        ++passes;
    }
    log_drain();
    if (changed) {
        save_snapshot();
    }
}

int astra_main(const AstraStartupInfo *startup)
{
    const AstraStartupCapability *bootstrap;
    const AstraStartupCapability *event_target;
    uint32_t published[3];
    uint32_t status = ASTRA_STATUS_OK;

    if (!astra_startup_validate(startup) ||
        startup->capabilities_address == 0u)
        return ASTRA_STATUS_INVALID;
    bootstrap = astra_startup_capability(startup,
                                         ASTRA_CAPABILITY_SERVICE_READY);
    event_target = astra_startup_capability(startup,
                                            ASTRA_CAPABILITY_EVENT_TARGET);
    if (bootstrap == NULL || event_target == NULL)
        return ASTRA_STATUS_BAD_HANDLE;
    event_target_handle = event_target->handle;
    status = astra_process_filesystem_open(&process_filesystem, startup);
    if (status == ASTRA_VFS_OK)
        status = events_start(startup->process_handle);
    published[0] = events_handle;
    published[1] = control_handle;
    published[2] = log_handle;
    (void)astra_service_ready(bootstrap->handle, status, published, 3u);
    (void)astra_close(bootstrap->handle);
    if (status != ASTRA_STATUS_OK)
        return (int)status;
    for (;;) {
        /* Doorbells arrive on log_receive. The deadline is the backstop
         * sweep, and when producers that died are reaped. */
        uint32_t waits[] = {control_receive, events_receive, log_receive};
        uint64_t deadline;
        uint32_t wait_status;

        events_pump();
        if (log_more)
            continue; /* rings still hold lines: no sleeping on them */
        deadline = astra_clock_monotonic() + EVENTS_TRACE_POLL_NS;
        wait_status = astra_wait_multiple(waits, 3u, deadline, NULL, NULL);
        if (wait_status != ASTRA_SYSCALL_OK &&
            wait_status != ASTRA_SYSCALL_TIMED_OUT)
            return ASTRA_STATUS_PEER_DEAD;
    }
}

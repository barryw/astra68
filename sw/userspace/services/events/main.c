/*
 * The protected events service. Every caller goes through the storage
 * protocol; this process owns the trace drain, bounded store and persistence.
 *
 * It is also the log service (docs/SYSTEM_LOG.md sections 3 and 5): it
 * publishes LOG, maps every process's log ring, and appends their lines and
 * the kernel ring's text to STORE:syslog and WORK:.astra/syslog, served back
 * as EVENTS:log/syslog.
 *
 * Nothing here emits an event or logs a line. An events service that logs
 * about logging is how a logging subsystem takes a machine down, and the rule
 * has to be structural rather than careful -- so this file includes neither
 * astra/event_emit.h nor astra/log.h, never calls astra_log_line or
 * astra_log_write (or anything built on them), and must not grow any of
 * those.
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
#include <astra/log_names.h>
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
/*
 * Kernel-ring text lines wait here, joined but unformatted, until a drain
 * pass merges them with the rings by time. A full queue is drained on the
 * spot, so these bound memory, not what is kept.
 */
#define LOG_KERNEL_LINES 128u
#define LOG_KERNEL_TEXT (16u * 1024u)
/* Drain passes per pump before the ports are answered again. */
#define LOG_DRAIN_PASSES 8u
/* The initial user image -- the supervisor -- is process 1, and the kernel
 * never hands 1 out again: ids wrap to 2 (sw/kernel/process.c). */
#define LOG_SUPERVISOR_PID 1u

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
static char log_batch[ASTRA_LOG_DRAIN_OUT_BYTES];
static uint32_t log_day;           /* YYYYMMDD of the newest dated line */
static int log_more;               /* the drain stopped with work left */
/* READY has been sent: the drain writes. Boot services wait on READY, so
 * nothing before it does file I/O it could put off. */
static int log_live;
/*
 * The resume point (log_begin, log_state_save). Kernel-ring records not
 * after `log_after` were written by an earlier instance, or belong to an
 * earlier boot; 0 means none did. `log_resume.sequence` is the last record
 * whose text is in the file, or that wrote nothing, and `log_resume.mark`
 * that record (or, until one is written, this boot's first); `log_seen` the
 * last record read.
 */
static uint32_t log_after;
static AstraLogResume log_resume;
static AstraLogMark log_seen;
static int log_restarted;
static int log_state_dirty;
static uint64_t log_state_saved_ns; /* monotonic time of the last save */
static AstraLogNames log_names;    /* pid -> name, from PROCESS_START */
static uint8_t log_state_bytes[ASTRA_LOG_STATE_BYTES_MAX];
/* The kernel-ring queue: lines in ring order, their text packed in order,
 * each named when it was read and with its last record's sequence. */
static AstraLogLine log_kernel[LOG_KERNEL_LINES];
static char log_kernel_ident[LOG_KERNEL_LINES][ASTRA_LOG_IDENT_MAX];
static AstraLogMark log_kernel_mark[LOG_KERNEL_LINES];
static char log_kernel_text[LOG_KERNEL_TEXT];
static uint32_t log_kernel_count;
static uint32_t log_kernel_bytes;
/* One kernel-ring text line, joined from its CONTINUED chunks. */
static struct {
    AstraLogMark mark;             /* the last chunk's */
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

/* EVENTS:log/syslog and syslog.N: the boot volume's copies, read through.
 * Names as log_file.c rotate() makes them. */
static const char *const syslog_names[ASTRA_EVENTS_LOG_KEEP + 1u] = {
    "syslog", "syslog.1", "syslog.2", "syslog.3"
};
_Static_assert(LOG_KEEP == ASTRA_EVENTS_LOG_KEEP,
               "EVENTS:log serves every rotation the service keeps");

static int
syslog_read(void *context, uint32_t index, uint64_t offset, void *buffer,
            uint32_t length, uint32_t *moved)
{
    char path[LOG_PATH_MAX];
    AstraFile file = ASTRA_FILE_INIT;
    uint32_t status;

    (void)context;
    *moved = 0u;
    if (index > ASTRA_EVENTS_LOG_KEEP ||
        !log_path(path, log_store_dir, syslog_names[index]))
        return 0;
    status = astra_filesystem_open(&process_filesystem.filesystem, path,
                                   ASTRA_VFS_OPEN_READ, &file);
    if (status == ASTRA_VFS_ERR_NOT_FOUND)
        return 1;
    if (status != ASTRA_VFS_OK)
        return 0;
    status = astra_filesystem_read_at(&file, offset, buffer, length, moved);
    (void)astra_filesystem_close(&file);
    return status == ASTRA_VFS_OK;
}

static int
syslog_size(void *context, uint32_t index, uint64_t *bytes)
{
    (void)context;
    if (index > ASTRA_EVENTS_LOG_KEEP)
        return 0;
    return log_size(log_store_dir, syslog_names[index], bytes);
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

/*
 * `--- boot <time> ---` where one boot's lines begin, or
 * `--- events service restarted <time> ---` where a restarted instance's do.
 * No build id: nothing reachable without new ABI says which build this is
 * (ASTRA_BUILD_ID is not defined for userspace, and the hardware build id
 * is only in the kernel's boot info), and a constant would say nothing.
 */
static void
log_marker(int restarted)
{
    static const char tail[] = " ---\n";
    const char *head = restarted ? "--- events service restarted " :
                                   "--- boot ";
    AstraTimeSample now;
    AstraLogLine line = {0};
    /* 29 + a 33-byte timestamp (log_format.h) + 5. */
    char text[72];
    uint32_t used = 0u;

    astra_time_now(&now);
    log_line_time(&line, now.monotonic_ns, &now);
    while (head[used] != '\0') {
        text[used] = head[used];
        ++used;
    }
    used += astra_log_format_time(text + used, 33u, &line);
    for (uint32_t at = 0u; tail[at] != '\0'; ++at)
        text[used++] = tail[at];
    if (astra_log_local_day(&line) != 0u)
        log_day = astra_log_local_day(&line);
    log_write(text, used, log_day);
}

/*
 * After a restart marker: how many kernel-ring records the ring overwrote
 * before this instance could read them. Written as the service's own line,
 * straight to the file like the marker -- never through LOG. pid 0: the
 * service has no way to learn its own pid, and the ident says who it is.
 */
static void
log_lost(uint32_t lost)
{
    static const char tail[] =
        " kernel-ring records (not necessarily log lines) lost while the"
        " events service was down";
    AstraTimeSample now;
    AstraLogLine line = {0};
    /* 10 digits (a uint32_t) plus sizeof(tail) - 1, with room to spare. */
    char text[112];
    char digits[10];
    char out[192]; /* time, level, ident[pid], stream, text */
    uint32_t used = 0u;
    uint32_t count = 0u;

    do {
        digits[count++] = (char)('0' + lost % 10u);
        lost /= 10u;
    } while (lost != 0u);
    while (count != 0u)
        text[used++] = digits[--count];
    for (uint32_t at = 0u; tail[at] != '\0'; ++at)
        text[used++] = tail[at];
    astra_time_now(&now);
    log_line_time(&line, now.monotonic_ns, &now);
    line.level = ASTRA_EVENT_LEVEL_WARNING;
    line.stream = ASTRA_LOG_STREAM_LOG;
    line.ident = "events";
    line.text = text;
    line.length = used;
    log_write(out, astra_log_format(out, sizeof(out), &line), log_day);
}

/*
 * The program a kernel-ring line came from: `kernel` for pid 0, otherwise
 * the basename of the name its PROCESS_START gave it -- the same thing a
 * ring's setup ident is. "" (the formatter writes `?`) only for a pid the
 * table does not know: started before the ring's oldest record, or
 * forgotten (log_names.h).
 */
static const char *
log_ident(uint32_t pid)
{
    const char *name;

    if (pid == 0u)
        return "kernel";
    name = astra_log_names_find(&log_names, pid);
    if (name != NULL)
        return name;
    /* Forgotten, or started before the ring's oldest record, but it set up
     * a log ring: the ident it gave there. */
    for (uint32_t index = 0u; index < ASTRA_LOG_CLIENTS_MAX; ++index)
        if (log_clients.client[index].used &&
            log_clients.client[index].pid == pid)
            return log_clients.client[index].ident;
    return pid == LOG_SUPERVISOR_PID ? "supervisor" : "";
}

/*
 * The resume point, written whole to a temporary file and renamed over the
 * last one, so a reader sees one or the other, never a torn file.
 *
 * Not synced, and saved at most once per EVENTS_TRACE_POLL_NS (events_pump).
 * The state matters only within one boot -- it says whether a starting
 * service is a restart of this boot's -- and within a boot the storage
 * service's view of the volume is coherent whether or not a block reached
 * the medium. Across a power loss a stale or torn state either fails its
 * CRC or names a ring record the next boot does not hold, and either way
 * that boot is a boot. A state that lags the file by up to a second only
 * widens what a restart writes twice. Syncing it, once per process start
 * and per drained kernel line, bought nothing for that cost.
 *
 * @return Nonzero once the file is in place.
 */
static int
log_state_save(void)
{
    AstraFile file = ASTRA_FILE_INIT;
    uint32_t length;
    uint32_t done = 0u;
    int saved;

    log_state_saved_ns = astra_clock_monotonic();
    length = astra_log_state_encode(&log_resume, &log_names,
                                    log_state_bytes,
                                    sizeof(log_state_bytes));
    if (length == 0u ||
        astra_filesystem_open(&process_filesystem.filesystem,
                              "/store/syslog.state.tmp",
                              ASTRA_VFS_OPEN_WRITE | ASTRA_VFS_OPEN_CREATE |
                                  ASTRA_VFS_OPEN_TRUNCATE,
                              &file) != ASTRA_VFS_OK)
        return 0;
    while (done < length) {
        uint32_t moved = 0u;

        if (astra_filesystem_write(&file, log_state_bytes + done,
                                   length - done, &moved) != ASTRA_VFS_OK ||
            moved == 0u)
            break;
        done += moved;
    }
    saved = done == length;
    if (astra_filesystem_close(&file) != ASTRA_VFS_OK || !saved)
        return 0;
    return astra_filesystem_rename(&process_filesystem.filesystem,
                                   "/store/syslog.state.tmp",
                                   "/store/syslog.state") == ASTRA_VFS_OK;
}

/* Save a changed resume point, at most once per EVENTS_TRACE_POLL_NS; the
 * main loop's deadline wakes the service for one that is waiting. */
static void
log_state_flush(void)
{
    if (log_state_dirty &&
        astra_clock_monotonic() - log_state_saved_ns >= EVENTS_TRACE_POLL_NS &&
        log_state_save())
        log_state_dirty = 0;
}

/* Whether this kernel-ring record is this instance's to log. */
static int
log_wanted(const AstraEventDrained *record)
{
    return log_after == 0u ||
           astra_log_sequence_after(record->sequence, log_after);
}

/* Drop the first `taken` queued kernel lines; the rest move to the front. */
static void
log_kernel_take(uint32_t taken)
{
    uint32_t offset;

    if (taken == 0u)
        return;
    if (taken >= log_kernel_count) {
        log_kernel_count = 0u;
        log_kernel_bytes = 0u;
        return;
    }
    offset = (uint32_t)(log_kernel[taken].text - log_kernel_text);
    log_kernel_count -= taken;
    log_kernel_bytes -= offset;
    (void)memmove(log_kernel_text, log_kernel_text + offset,
                  log_kernel_bytes);
    (void)memmove(log_kernel, log_kernel + taken,
                  log_kernel_count * sizeof(log_kernel[0]));
    (void)memmove(log_kernel_ident, log_kernel_ident + taken,
                  log_kernel_count * sizeof(log_kernel_ident[0]));
    (void)memmove(log_kernel_mark, log_kernel_mark + taken,
                  log_kernel_count * sizeof(log_kernel_mark[0]));
    for (uint32_t index = 0u; index < log_kernel_count; ++index) {
        log_kernel[index].text -= offset;
        log_kernel[index].ident = log_kernel_ident[index];
    }
}

static void log_consumed(void *context, AstraLogClient *client,
                         uint32_t position);

/*
 * One drain pass: the queued kernel lines and every ring, merged by time
 * into one batch and one append per file. The kernel lines it took move the
 * resume point.
 */
static uint32_t
log_pass(uint32_t *pending)
{
    uint32_t taken = 0u;
    uint32_t moved;

    moved = astra_log_clients_drain(&log_clients, log_kernel,
                                    log_kernel_count, &taken, log_batch,
                                    sizeof(log_batch), log_consumed, NULL,
                                    &log_day, pending);
    log_write(log_batch, moved, log_day);
    if (taken != 0u) {
        log_resume.mark = log_kernel_mark[taken - 1u];
        log_resume.sequence = log_resume.mark.sequence;
        /*
         * Kernel-ring text just left for the file: save the resume point now
         * rather than waiting for the 1 s rate limit, so a restart requested
         * in that window cannot find a stale saved point and re-write what
         * this pass already wrote. The only duplicate window left is a crash
         * between this append and the rename inside log_state_save.
         */
        log_state_dirty = !log_state_save();
    }
    log_kernel_take(taken);
    return moved;
}

/* The joined kernel-ring line, queued for the next drain pass. Debug is
 * live commentary and is not stored, as event_store.c does not store it. */
static void
kernel_line_end(const AstraTimeSample *now)
{
    AstraLogLine *line;
    uint32_t length = kernel_line.length;

    kernel_line.length = 0u;
    while (length != 0u && (kernel_line.text[length - 1u] == '\n' ||
                            kernel_line.text[length - 1u] == '\r'))
        --length;
    if (length == 0u || kernel_line.level == ASTRA_EVENT_LEVEL_DEBUG)
        return;
    /* Full: drain now. Every pass takes at least one queued line. */
    while (log_kernel_count != 0u &&
           (log_kernel_count == LOG_KERNEL_LINES ||
            length > LOG_KERNEL_TEXT - log_kernel_bytes)) {
        uint32_t pending = 0u;

        (void)log_pass(&pending);
    }
    {
        const char *ident = log_ident(kernel_line.process);
        uint32_t at = 0u;

        while (ident[at] != '\0' && at + 1u < ASTRA_LOG_IDENT_MAX) {
            log_kernel_ident[log_kernel_count][at] = ident[at];
            ++at;
        }
        log_kernel_ident[log_kernel_count][at] = '\0';
    }
    log_kernel_mark[log_kernel_count] = kernel_line.mark;
    line = &log_kernel[log_kernel_count];
    line->ident = log_kernel_ident[log_kernel_count];
    ++log_kernel_count;
    log_line_time(line, kernel_line.monotonic, now);
    line->level = (uint8_t)kernel_line.level;
    line->stream = kernel_line.process == 0u ? ASTRA_LOG_STREAM_KERNEL :
                                               ASTRA_LOG_STREAM_LOG;
    line->truncated = 0u;
    line->pid = kernel_line.process;
    line->text = log_kernel_text + log_kernel_bytes;
    line->length = length;
    (void)memcpy(log_kernel_text + log_kernel_bytes, kernel_line.text,
                 length);
    log_kernel_bytes += length;
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
static uint64_t
record_monotonic(const AstraEventDrained *record)
{
    return (((uint64_t)record->timestamp_high << 32) |
            record->timestamp_low) * ASTRA_CLOCK_MONOTONIC_RESOLUTION_NS;
}

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
        kernel_line.monotonic = record_monotonic(record);
    }
    (void)memcpy(kernel_line.text + kernel_line.length, record->payload,
                 length);
    kernel_line.length += length;
    kernel_line.mark = astra_log_mark(record);
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

        if (status == ASTRA_SYSCALL_WOULD_BLOCK)
            return;
        /* Anything else left queued would wake this loop forever: take it
         * whole and discard it. */
        if (status != ASTRA_SYSCALL_OK) {
            if (astra_port_receive_from(log_receive, junk, sizeof(junk),
                                        junk_handles,
                                        ASTRA_MESSAGE_HANDLES_MAX, &size,
                                        &count, &sender) != ASTRA_SYSCALL_OK)
                return;
            for (uint32_t index = 0u; index < count; ++index)
                (void)astra_close(junk_handles[index]);
            continue;
        }
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
 * The queued kernel lines and every ring, merged pass by pass, until a pass
 * moves nothing, nothing is pending and no kernel line waits -- or
 * LOG_DRAIN_PASSES, after which the ports are answered and the service polls
 * rather than sleeps. Nothing is written before READY (log_live): until
 * then the queue only fills, and is drained early only if it is full.
 *
 * A producer that died is found first (its consumer endpoint reports
 * PEER_DEAD; the kernel ring object is never notified, so it is never
 * otherwise ready), drained to the end with everyone else, then released
 * as soon as its own ring is empty.
 */
static void
log_drain(void)
{
    uint32_t pending = 0u;

    if (!log_live)
        return;

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
        moved = log_pass(&pending);
        if (moved == 0u && pending == 0u && log_kernel_count == 0u)
            break;
    }
    for (uint32_t index = 0u; index < ASTRA_LOG_CLIENTS_MAX; ++index) {
        AstraLogClient *client = &log_clients.client[index];

        /* A gone producer writes nothing more: once this table has read up
         * to its last published slot and announced its last drops, it is
         * done, whatever else is left. */
        if (client->used &&
            (client->dead ||
             (log_gone[index] && astra_log_client_caught_up(client)))) {
            log_release(client);
            log_gone[index] = 0u;
        }
    }
}

static void events_pump(void);

/* Static, not automatic: keep one page of transfer storage off the stack. */
static AstraEventDrained drained[ASTRA_MEMORY_PAGE_SIZE /
                                  sizeof(AstraEventDrained)];
#define DRAINED_CAPACITY ((uint32_t)(sizeof(drained) / sizeof(drained[0])))

/*
 * Where this instance's kernel-ring text begins, and whether it is a boot or
 * a restart (docs/SYSTEM_LOG.md section 5, Boot).
 *
 * The saved resume point says how far it got and names one ring record of
 * the boot that wrote it (astra_log_state_restart). That record still in
 * the ring, unchanged and in this boot's span, is a restart however the
 * wall clock moved; the wall-clock boot id decides only once the ring has
 * overwritten it. Same boot: a restart, which takes the saved names and
 * carries on after the saved sequence -- so the previous instance's
 * undrained lines and its death report are written, and nothing twice, and
 * what the ring overwrote meanwhile is counted (log_lost; it includes what
 * the dead instance wrote after its last save). Otherwise a boot: from this
 * boot's first record. The ring outlives a warm reset, so a previous boot's
 * records may still be in it; astra_log_boot_scan finds where this one
 * begins.
 */
static int
log_begin(void)
{
    AstraLogBootScan scan = {0};
    AstraLogResume saved = {0};
    AstraTimeSample now;
    uint8_t *bytes = NULL;
    uint32_t length = 0u;
    uint32_t cursor = 0u;
    uint32_t copied = 0u;
    uint32_t lost = 0u;
    int decoded = 0;
    int restarted = 0;

    astra_time_now(&now);
    if (astra_process_read_file_alloc(&process_filesystem,
                                      "/store/syslog.state",
                                      (void **)&bytes, &length) ==
        ASTRA_VFS_OK) {
        decoded = astra_log_state_decode(bytes, length, &saved, &log_names);
        astra_runtime_deallocate(bytes);
    }
    scan.want = decoded ? saved.mark.sequence : 0u;
    do {
        if (astra_trace_read(debug_handle, &cursor, drained,
                             DRAINED_CAPACITY, &copied, &lost) !=
            ASTRA_SYSCALL_OK)
            break;
        for (uint32_t index = 0u; index < copied; ++index)
            astra_log_boot_scan(&scan, &drained[index]);
    } while (copied == DRAINED_CAPACITY);
    log_resume.boot = astra_log_boot_id(now.wall_ns, now.monotonic_ns,
                                        now.wall_valid != 0u);
    restarted = decoded &&
                astra_log_state_restart(&saved, log_resume.boot, &scan);
    lost = 0u;
    if (restarted) {
        log_after = saved.sequence;
        log_resume.mark = saved.mark;
        /* What the ring overwrote after the saved point: the kernel counts
         * it for a reader resuming there. */
        cursor = saved.sequence;
        if (astra_trace_read(debug_handle, &cursor, drained, 1u, &copied,
                             &lost) != ASTRA_SYSCALL_OK)
            lost = 0u;
    } else {
        astra_log_names_reset(&log_names);
        log_after = scan.first != 0u ? scan.first - 1u : 0u;
        log_resume.mark = scan.anchor;
    }
    log_resume.sequence = log_after;
    log_state_dirty = !log_state_save();
    log_restarted = restarted;
    log_marker(restarted);
    if (lost != 0u)
        log_lost(lost);
    return restarted;
}

static uint32_t
events_start(const AstraStartupInfo *startup)
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
    debug_handle = startup->process_handle;
    (void)log_begin();
    backend.log_read = syslog_read;
    backend.log_size = syslog_size;
    backend.log_context = NULL;
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
    const uint32_t drain_capacity = DRAINED_CAPACITY;
    uint32_t passes = 0u;
    int changed = 0;
    AstraTimeSample now;

    if (!drain_ready) {
        if (events_ready)
            log_drain();
        log_state_flush(); /* the main loop's deadline waits on it */
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
        /* The tail of an open name chain may be among what was lost. */
        if (lost != 0u)
            log_names.chain_open = 0u;
        for (uint32_t index = 0u; index < copied; ++index) {
            const AstraEventDrained *record = &drained[index];
            int wanted = log_wanted(record);

            if (wanted)
                log_seen = astra_log_mark(record);
            /* A process's name, not something it said: the table only. */
            if (record->message == ASTRA_EVENT_MESSAGE_PROCESS_START) {
                if (wanted && astra_log_names_record(&log_names, record))
                    log_state_dirty = 1;
                continue;
            }
            astra_event_store_append(&store, record);
            if (wanted)
                kernel_text(record, &now);
        }
        changed |= copied != 0u;
        if (copied < drain_capacity) {
            break;
        }
        ++passes;
    }
    log_drain();
    /* Nothing read is waiting to be written: resume after all of it. */
    if (log_kernel_count == 0u && kernel_line.length == 0u &&
        !log_names.chain_open && log_seen.sequence != 0u &&
        log_resume.sequence != log_seen.sequence) {
        log_resume.sequence = log_seen.sequence;
        log_resume.mark = log_seen;
        log_state_dirty = 1;
    }
    log_state_flush();
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
        status = events_start(startup);
    published[0] = events_handle;
    published[1] = control_handle;
    published[2] = log_handle;
    (void)astra_service_ready(bootstrap->handle, status, published, 3u);
    (void)astra_close(bootstrap->handle);
    if (status != ASTRA_STATUS_OK)
        return (int)status;
    /* Names learned so far are the boot set (log_names.h). */
    if (!log_restarted) {
        astra_log_names_pin(&log_names);
        log_state_dirty = 1;
    }
    log_live = 1;
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
        /* A changed resume point waits at most one interval to be saved. */
        if (log_state_dirty &&
            log_state_saved_ns + EVENTS_TRACE_POLL_NS < deadline)
            deadline = log_state_saved_ns + EVENTS_TRACE_POLL_NS;
        wait_status = astra_wait_multiple(waits, 3u, deadline, NULL, NULL);
        if (wait_status != ASTRA_SYSCALL_OK &&
            wait_status != ASTRA_SYSCALL_TIMED_OUT)
            return ASTRA_STATUS_PEER_DEAD;
    }
}

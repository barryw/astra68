#ifndef ASTRA_VFS_SERVICE_CORE_H
#define ASTRA_VFS_SERVICE_CORE_H

#include <stdint.h>

#include <astra/process.h>
#include <astra/vfs_backend.h>
#include <astra/vfs_service.h>

/*
 * The storage service, with no transport in it.
 *
 * astra_vfs_service_dispatch() turns one request record into one reply record.
 * It never touches a port, a syscall, or a clock, which is what lets the whole
 * of it run on the host under sanitizers against a fake backend. Binding it to
 * real ports is a separate file, the way sw/userspace/input separates its core
 * from its port sink.
 *
 * Allocation-free. The caller supplies zero-filled file storage charged to
 * the service's own memory budget. The core grows into that storage lazily and
 * reports ASTRA_VFS_ERR_LIMIT when the budget is exhausted.
 *
 * Two properties this core exists to enforce, which no backend should have to
 * think about:
 *
 *  - a handle is a slot plus a generation, so a stale or forged handle is
 *    refused rather than silently reaching whatever now holds the slot;
 *  - a file handle belongs to the session that opened it, so one client cannot
 *    read another's file by guessing a number.
 */

/* One service session for every grant a full process table can legally hold. */
#define ASTRA_VFS_SESSION_MAX \
    (ASTRA_PROCESS_COUNT_MAX * ASTRA_LAUNCH_GRANT_MAX)

typedef struct AstraVfsOpenFile {
    uintptr_t node;
    uint32_t session;       /* the owning session id, 0 when the slot is free */
    uint32_t flags;
    uint16_t generation;
    uint16_t kind;
} AstraVfsOpenFile;

typedef struct AstraVfsSessionSlot {
    uint32_t id;            /* 0 when free */
    uint32_t owner;         /* authenticated transport owner, 0 for local */
    uint32_t open_files;
    uint16_t generation;
    uint16_t version;       /* the version agreed at HELLO */
    char rename_from[ASTRA_VFS_PATH_MAX];
    uint8_t rename_pending;
} AstraVfsSessionSlot;

typedef struct AstraVfsServiceStats {
    uint32_t requests;
    uint32_t replies_failed;      /* requests answered with a non-OK status */
    uint32_t protocol_rejects;    /* malformed records or version mismatch */
    uint32_t sessions_opened;
    uint32_t sessions_closed;
    uint32_t files_opened;
    uint32_t files_closed;
    uint32_t stale_handles;       /* generation mismatches, i.e. real misuse */
    uint32_t cross_session_denied;
    uint32_t cross_owner_denied;
    uint32_t owner_session_quota_denied;
    uint32_t owner_quota_denied;
    uint32_t peak_open_files;
    uint32_t peak_sessions;
} AstraVfsServiceStats;

typedef struct AstraVfsService {
    AstraVfsBackend backend;
    AstraVfsSessionSlot sessions[ASTRA_VFS_SESSION_MAX];
    AstraVfsOpenFile *files;
    uint32_t file_capacity;
    uint32_t file_high_water;
    AstraVfsServiceStats stats;
    uint32_t next_session;
    uint16_t open_sessions;
    uint32_t open_files;
} AstraVfsService;

/* Returns 0 when the backend or caller-owned file storage is invalid. */
int astra_vfs_service_init(AstraVfsService *service,
                           const AstraVfsBackendOps *ops, void *context,
                           AstraVfsOpenFile *files, uint32_t file_capacity);

/*
 * One request in, one reply out. Always writes a complete reply, including for
 * a malformed request: a client that sent nonsense still gets an answer rather
 * than a timeout it cannot distinguish from a dead service.
 */
void astra_vfs_service_dispatch(AstraVfsService *service, uint32_t operation,
                                const AstraVfsRequest *request,
                                AstraVfsReply *reply);
void astra_vfs_service_dispatch_from(
    AstraVfsService *service, uint32_t owner, uint32_t operation,
    const AstraVfsRequest *request, AstraVfsReply *reply);
int astra_vfs_service_session_owned(const AstraVfsService *service,
                                    uint32_t session, uint32_t owner);

/*
 * Open, read whole, close -- as one call. `node_size` is filled even when the
 * file does not fit `capacity`, so a caller that guessed too small learns what
 * to allocate without a second round trip.
 */
uint32_t astra_vfs_service_read_path(AstraVfsService *service,
                                     const char *path, void *buffer,
                                     uint32_t capacity, uint32_t *moved,
                                     uint64_t *node_size);

/*
 * A read that answers into the caller's buffer rather than into a reply record.
 *
 * ASTRA_VFS_IO_MAX is 192 bytes because a reply carries its payload inline.
 * Nothing about a filesystem needs that bound -- a backend read takes a length
 * -- but every read in the system went through the inline path, so the
 * shared-area transfer was 86 backend calls and two byte-loop copies of every
 * byte per 16 KiB. This is the same read, with the same session, access and
 * kind checks, and without the clamp. `moved` is short at end of file.
 */
uint32_t astra_vfs_service_read_into(AstraVfsService *service,
                                     uint32_t session, AstraVfsFile file,
                                     uint64_t offset, void *buffer,
                                     uint32_t length, uint32_t *moved);

/* The write-side twin: bytes come from the session's bound transfer area. */
uint32_t astra_vfs_service_write_from(AstraVfsService *service,
                                      uint32_t session, AstraVfsFile file,
                                      uint64_t offset, const void *buffer,
                                      uint32_t length, uint32_t *moved);

/* Packs directory records into an arbitrary bounded buffer. */
uint32_t astra_vfs_service_readdir_into(
    AstraVfsService *service, const char *path, uint64_t cursor,
    uint32_t entry_limit, uint8_t *buffer, uint32_t capacity,
    uint32_t *used, uint64_t *next);

/*
 * Releases everything a session held. Called when a client dies; the port
 * layer learns that from peer-dead and the core does not need to know how.
 */
void astra_vfs_service_release_session(AstraVfsService *service,
                                       uint32_t session);

const AstraVfsServiceStats *astra_vfs_service_stats(
    const AstraVfsService *service);

#endif

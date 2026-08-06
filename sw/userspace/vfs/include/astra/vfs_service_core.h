#ifndef ASTRA_VFS_SERVICE_CORE_H
#define ASTRA_VFS_SERVICE_CORE_H

#include <stdint.h>

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
 * Allocation-free. Sessions and open files live in fixed tables sized here, so
 * a service that runs out says ASTRA_VFS_ERR_LIMIT instead of failing an
 * allocation it cannot report. Same reasoning as the bounded allocator the
 * filesystem itself runs on.
 *
 * Two properties this core exists to enforce, which no backend should have to
 * think about:
 *
 *  - a handle is a slot plus a generation, so a stale or forged handle is
 *    refused rather than silently reaching whatever now holds the slot;
 *  - a file handle belongs to the session that opened it, so one client cannot
 *    read another's file by guessing a number.
 */

#define ASTRA_VFS_SESSION_MAX 8u
#define ASTRA_VFS_FILE_MAX 16u

typedef struct AstraVfsOpenFile {
    uintptr_t node;
    uint32_t session;       /* the owning session id, 0 when the slot is free */
    uint32_t flags;
    uint16_t generation;
    uint16_t kind;
} AstraVfsOpenFile;

typedef struct AstraVfsSessionSlot {
    uint32_t id;            /* 0 when free */
    uint16_t generation;
    uint16_t version;       /* the version agreed at HELLO */
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
    uint32_t peak_open_files;
    uint32_t peak_sessions;
} AstraVfsServiceStats;

typedef struct AstraVfsService {
    AstraVfsBackend backend;
    AstraVfsSessionSlot sessions[ASTRA_VFS_SESSION_MAX];
    AstraVfsOpenFile files[ASTRA_VFS_FILE_MAX];
    AstraVfsServiceStats stats;
    uint32_t next_session;
    uint16_t open_sessions;
    uint16_t open_files;
} AstraVfsService;

/* Returns 0 when `backend` or its op table is incomplete. */
int astra_vfs_service_init(AstraVfsService *service,
                           const AstraVfsBackendOps *ops, void *context);

/*
 * One request in, one reply out. Always writes a complete reply, including for
 * a malformed request: a client that sent nonsense still gets an answer rather
 * than a timeout it cannot distinguish from a dead service.
 */
void astra_vfs_service_dispatch(AstraVfsService *service, uint32_t operation,
                                const AstraVfsRequest *request,
                                AstraVfsReply *reply);

/*
 * Releases everything a session held. Called when a client dies; the port
 * layer learns that from peer-dead and the core does not need to know how.
 */
void astra_vfs_service_release_session(AstraVfsService *service,
                                       uint32_t session);

const AstraVfsServiceStats *astra_vfs_service_stats(
    const AstraVfsService *service);

#endif

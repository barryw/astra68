#ifndef ASTRA_VFS_SERVICE_CORE_H
#define ASTRA_VFS_SERVICE_CORE_H

#include <stdint.h>
#include <stddef.h>

#include <astra/limits.h>
#include <astra/vfs_backend.h>
#include <astra/vfs_service.h>
#include <astra/vfs_client.h>
#include <astra/vfs_path_payload.h>

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

/* A transported session owns kernel handles; the 8-bit handle ABI is final. */
#define ASTRA_VFS_SESSION_MAX ASTRA_HANDLE_COUNT_MAX

struct AstraVfsOpenFile;

/* One node-wide backing revision and logical view, held by its native writer.
 * Page bytes and dirty/snapshot generations remain in VM-owned areas. */
typedef struct AstraVfsFileCache {
    AstraVfsNodeInfo backing;
    uint64_t size;
    uint64_t modified_ns;
    AstraVfsFile writer;
    uint32_t error;
    uint8_t written;
    /* Shared WRITE authority can outlive the description that exposed it. */
    uint8_t external_writes;
} AstraVfsFileCache;

#define ASTRA_VFS_CACHE_EXTERNAL_WRITES 1u

/* A retry conflict must not hide the writeback failure that left bytes dirty. */
static inline void astra_vfs_file_cache_record_status(AstraVfsFileCache *cache,
    uint32_t status)
{
    if (cache != NULL && (status != ASTRA_VFS_ERR_BUSY || cache->error == ASTRA_VFS_OK))
        cache->error = status;
}

/* Exact nodes in one backend incarnation share an I/O owner. Unknown nodes
 * share only through description transfer. Cells use the file-storage budget. */
typedef struct AstraVfsFileIO {
    struct AstraVfsOpenFile *active;
    volatile uint32_t sequence __attribute__((aligned(4)));
    uint32_t references;
    uint16_t waiters;
    uint64_t incarnation;
    AstraVfsNodeMeta identity;
    AstraVfsFileCache *cache;
} AstraVfsFileIO;

typedef struct AstraVfsOpenFile {
    AstraVfsBackend backend;
    /* A pending transfer owns a description too, in the same bounded table. */
    union {
        uintptr_t node;
        uint64_t token;
    };
    uint32_t session;       /* the owning session id, 0 when the slot is free */
    uint32_t flags;
    AstraVfsFileIO io_storage;
    AstraVfsFileIO *io;
    uint16_t generation;    /* 0 after exhaustion; never issued or reused */
    uint16_t kind;
    uint8_t state;
    uint16_t retained;     /* cache owners holding this backend description */
} AstraVfsOpenFile;

static inline int astra_vfs_open_file_active(const AstraVfsOpenFile *file)
{
    return file->io != NULL && file->io->active == file;
}

enum {
    ASTRA_VFS_FILE_FREE = 0,
    ASTRA_VFS_FILE_OPENING,
    ASTRA_VFS_FILE_OPEN,
    ASTRA_VFS_FILE_CLOSING,
    ASTRA_VFS_FILE_DETACHED,
    ASTRA_VFS_FILE_SHARED,
    ASTRA_VFS_FILE_TRANSFERRING
};

typedef struct AstraVfsSessionSlot {
    AstraVfsBackend backend;
    uint32_t id;            /* 0 when free */
    uint32_t owner;         /* authenticated transport owner, 0 for local */
    uint32_t grant;         /* the grant it said HELLO on, 0 for none */
    uint32_t open_files;
    uint16_t generation;
    uint16_t version;       /* the version agreed at HELLO */
    uint16_t inflight;
    uint8_t closing;
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
    uint32_t last_failed_operation;
    uint32_t last_failed_status;
    uint32_t last_failed_owner;
} AstraVfsServiceStats;

/* Monotonic per-service mutation generations. A consumer that sees a change
 * or a broker sequence gap refreshes its own file/directory view. */
typedef struct AstraVfsChangeSnapshot {
    uint64_t nodes;
    uint64_t entries;
} AstraVfsChangeSnapshot;

typedef int (*AstraVfsStateAcquire)(void *context);
typedef void (*AstraVfsStateRelease)(void *context);
/* Called with the state lock held and returns with it held. */
typedef int (*AstraVfsStateWait)(void *context,
                                volatile uint32_t *sequence,
                                uint32_t expected);
typedef void (*AstraVfsStateWake)(void *context,
                                 volatile uint32_t *sequence);
/* Called under the state lock after session/file holder admission, ownership
 * changes and retirement. It must not reenter this service. */
typedef void (*AstraVfsHoldersChanged)(void *context);

/* Runs outside the state lock with this file's node I/O owner held. The owned
 * WRITE reference may be adopted by clearing it; otherwise VFS releases it.
 * Use backing operations for preparation/writeback, never reenter node I/O. */
typedef uint32_t (*AstraVfsCacheWrite)(void *context,
    const AstraVfsOpenFile *file, AstraVfsFile *owned,
    uint64_t offset, uint32_t flags, uint64_t max_end,
    const void *buffer, uint32_t length, uint32_t *moved, uint64_t *position);

/* Reads the canonical byte view outside the state lock with node I/O held.
 * The owner honors sequential cursor semantics and confirmed error prefixes.
 * Backing-page preparation deliberately bypasses this front end. */
typedef uint32_t (*AstraVfsCacheRead)(void *context,
    const AstraVfsOpenFile *file, uint64_t offset, uint32_t flags,
    void *buffer, uint32_t length, uint32_t *moved, uint64_t *position);

/* Holds node I/O across snapshots and completion. NULL flushes all; otherwise
 * scan calls consume the shared remaining budget. Incomplete work returns BUSY.
 * Success promises clean VM pages and a complete backing EOF. */
typedef uint32_t (*AstraVfsCacheSync)(void *context,
    const AstraVfsOpenFile *file, uint32_t *remaining);

/* Refresh backing metadata and mapped extents under node I/O, with the state
 * lock released. The core applies logical cache metadata after this returns. */
typedef uint32_t (*AstraVfsCacheStat)(void *context,
    const AstraVfsOpenFile *file, AstraVfsNodeInfo *info);

/* Publish a read-only view while node I/O fences metadata and page ownership.
 * The temporary READ reference may be adopted, as with the write callback. */
typedef uint32_t (*AstraVfsFileAreaCreate)(void *context,
    const AstraVfsOpenFile *file, AstraVfsFile *owned,
    const AstraVfsNodeInfo *info, uint64_t base, uint32_t span, uint32_t *area);

typedef struct AstraVfsCacheOps {
    AstraVfsCacheWrite write;
    AstraVfsCacheSync sync;
    AstraVfsCacheRead read;
    AstraVfsCacheStat stat;
} AstraVfsCacheOps;

/* Called under the service state lock, without reentry or backend I/O.
 * Successful acquire/retain owns one reference; release consumes it.
 * HELLO replacement acquires before releasing the previous session binding.
 * Every file keeps its own binding through retained/cache references. */
typedef struct AstraVfsBackendBindingOps {
    uint32_t (*acquire)(void *context, uint32_t owner, uint32_t grant,
                        AstraVfsBackend *backend);
    uint32_t (*retain)(void *context, const AstraVfsBackend *backend);
    void (*release)(void *context, const AstraVfsBackend *backend);
    /* Same nonzero value for every root of this mount incarnation; no I/O. */
    uint64_t (*incarnation)(void *context, const AstraVfsBackend *backend);
} AstraVfsBackendBindingOps;

#ifdef ASTRA_VFS_TEST_HOOKS
enum {
    ASTRA_VFS_TEST_BEFORE_STATE_ACQUIRE = 0,
    ASTRA_VFS_TEST_AFTER_STATE_RELEASE,
    ASTRA_VFS_TEST_AFTER_SESSION_RESERVE,
    ASTRA_VFS_TEST_DURING_CLOSE,
    ASTRA_VFS_TEST_BEFORE_REPLY,
    ASTRA_VFS_TEST_TRANSITION_COUNT
};
typedef void (*AstraVfsTestTransition)(void *context, uint32_t transition);
#endif

typedef struct AstraVfsService {
    AstraVfsBackend backend;
    const AstraVfsBackendBindingOps *backend_bindings;
    void *backend_binding_context;
    AstraVfsSessionSlot *sessions;
    uint32_t session_capacity;
    AstraVfsOpenFile *files;
    uint32_t file_capacity;
    uint32_t file_high_water;
    AstraVfsServiceStats stats;
    AstraVfsChangeSnapshot changes;
    uint32_t next_session;  /* 0 after exhaustion; live sessions remain valid */
    uint16_t open_sessions;
    uint32_t open_files;
    uint32_t owner_session_limit;
    uint32_t owner_file_limit;
    AstraVfsStateAcquire state_acquire;
    AstraVfsStateRelease state_release;
    AstraVfsStateWait state_wait;
    AstraVfsStateWake state_wake;
    void *state_lock_context;
    AstraVfsHoldersChanged holders_changed;
    void *holders_changed_context;
    const AstraVfsCacheOps *cache_ops;
    void *cache_context;
    uint32_t cached_nodes;
    /* Cache-aware path transactions reuse the native I/O wait/wake owner. */
    AstraVfsFileIO namespace_io;
#ifdef ASTRA_VFS_TEST_HOOKS
    AstraVfsTestTransition test_transition;
    void *test_transition_context;
#endif
} AstraVfsService;

/* Returns 0 when the backend or caller-owned file storage is invalid. */
int astra_vfs_service_init(AstraVfsService *service,
                           const AstraVfsBackendOps *ops, void *context,
                           AstraVfsSessionSlot *sessions,
                           uint32_t session_capacity,
                           AstraVfsOpenFile *files, uint32_t file_capacity);
int astra_vfs_service_set_state_lock(AstraVfsService *service,
                                     AstraVfsStateAcquire acquire,
                                     AstraVfsStateRelease release,
                                     void *context);
int astra_vfs_service_set_state_wait(AstraVfsService *service,
                                     AstraVfsStateWait wait,
                                     AstraVfsStateWake wake);
/* Configure before the service admits sessions. */
int astra_vfs_service_set_holders_changed(AstraVfsService *service,
    AstraVfsHoldersChanged changed, void *context);
/* Configure before admission; ops must outlive the service. Ordinary and
 * retained writes use this owner. An absent sync callback leaves pending
 * cache flushes BUSY. An absent read callback refuses attached cache metadata
 * instead of exposing stale backing bytes. Backing-page I/O bypasses the
 * front end. */
int astra_vfs_service_set_cache_ops(AstraVfsService *service,
    const AstraVfsCacheOps *ops, void *context);

/* Cache preparation requires exact node identity and runs in the writer hook
 * with the node I/O owner held; unknown identities cannot cohere path metadata.
 * First preparation attaches caller-owned zeroed storage and adopts owned;
 * storage must outlive attachment. Later writers may pass NULL storage and
 * keep their temporary pin. EXTERNAL_WRITES retains backing-generated writeback
 * timestamps: later mapped stores must not inherit an older ordinary-write time.
 * Publish only after accepting bytes into VM pages. */
uint32_t astra_vfs_service_cache_prepare(AstraVfsService *service,
    const AstraVfsOpenFile *file, AstraVfsFile *owned, uint32_t flags,
    AstraVfsFileCache *storage, AstraVfsFileCache *snapshot);
uint32_t astra_vfs_service_cache_publish(AstraVfsService *service,
    const AstraVfsOpenFile *file, uint64_t size, uint64_t modified_ns);
/* Output snapshots are changed only on success. Writer is borrowed; the
 * retained argument keeps the I/O cell alive. */
uint32_t astra_vfs_service_cache_snapshot(AstraVfsService *service,
    AstraVfsFile retained, AstraVfsFileCache *snapshot, uint32_t *sequence);
/* Retire only after all VM pages are clean and no external writable mapping
 * can dirty them. The snapshot sequence fences intervening native I/O.
 * Detachment clears caller storage before releasing the backing writer. */
uint32_t astra_vfs_service_cache_retire(AstraVfsService *service,
    AstraVfsFile retained, uint32_t sequence);
/* Configure before admitting sessions or files. Without bindings, the
 * service's single backend remains the owner of every session and file. */
int astra_vfs_service_set_backend_bindings(AstraVfsService *service,
    const AstraVfsBackendBindingOps *bindings, void *context);
/* Snapshots committed mutation generations under the service state lock. */
int astra_vfs_service_change_snapshot(AstraVfsService *service,
    AstraVfsChangeSnapshot *snapshot);
/* Per-owner sessions in this service; zero disables the bound. */
int astra_vfs_service_set_owner_sessions(AstraVfsService *service,
                                         uint32_t sessions);
/* Open and pending-transfer references per native owner; zero disables it. */
int astra_vfs_service_set_owner_files(AstraVfsService *service, uint32_t files);
#ifdef ASTRA_VFS_TEST_HOOKS
void astra_vfs_service_set_test_transition(AstraVfsService *service,
                                           AstraVfsTestTransition transition,
                                           void *context);
#endif

/*
 * One request in, one reply out. Always writes a complete reply, including for
 * a malformed request: a client that sent nonsense still gets an answer rather
 * than a timeout it cannot distinguish from a dead service.
 */
void astra_vfs_service_dispatch(AstraVfsService *service, uint32_t operation,
                                const AstraVfsRequest *request,
                                const AstraVfsTransfer *transfer,
                                AstraVfsReply *reply);
void astra_vfs_service_dispatch_from(
    AstraVfsService *service, uint32_t owner, uint32_t operation,
    const AstraVfsRequest *request, const AstraVfsTransfer *transfer,
    AstraVfsReply *reply);
/*
 * A request that arrived on a grant's port (docs/FILESYSTEM_PRODUCTION_PRD.md
 * FS15). HELLO binds the new session to `grant` and answers `features`; every
 * other operation is refused ACCESS for a session bound to another grant.
 * Grant 0 is no grant: astra_vfs_service_dispatch_from().
 */
void astra_vfs_service_dispatch_grant(
    AstraVfsService *service, uint32_t owner, uint32_t grant,
    uint32_t features, uint32_t operation, const AstraVfsRequest *request,
    const AstraVfsTransfer *transfer,
    AstraVfsReply *reply);
/* Nonzero when `session` is open, belongs to `owner` and is bound to
 * `grant` (0 for a session opened on no grant). */
int astra_vfs_service_session_bound(const AstraVfsService *service,
                                    uint32_t session, uint32_t owner,
                                    uint32_t grant);
/* The grant `session` is bound to; 0 for no session or no grant. */
uint32_t astra_vfs_service_session_grant(const AstraVfsService *service,
                                         uint32_t session);
/* Releases every session bound to `grant`: its last holder has gone. */
void astra_vfs_service_release_grant(AstraVfsService *service,
                                     uint32_t grant);
int astra_vfs_service_session_capacity_reached(
    const AstraVfsService *service);

/* Borrow a caller-owned transfer without consuming it. The callback runs
 * outside the state lock with its source session and description pinned.
 * It may retain a separate external lease; it must not keep the slot pointer. */
typedef uint32_t (*AstraVfsSharedOperation)(void *context, const AstraVfsOpenFile *file);
uint32_t astra_vfs_service_borrow_share(AstraVfsService *service,
    uint32_t owner, uint64_t token, AstraVfsSharedOperation operation, void *context);

/*
 * Open, read whole, close -- as one call. `node_size` is filled even when the
 * file does not fit `capacity`, so a caller that guessed too small learns what
 * to allocate without a second round trip.
 * Uses a native description charged to the caller; size is refreshed after
 * acquiring node I/O, which stays held through all short reads. Session death
 * cannot release the rooted backend before the temporary description closes.
 */
uint32_t astra_vfs_service_read_path(AstraVfsService *service,
                                     uint32_t session, const char *path, void *buffer,
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
 * SEQUENTIAL uses the backend's shared description position; positioned reads
 * leave that cursor unchanged. `position` reports the resulting byte offset.
 */
uint32_t astra_vfs_service_read_position_into(AstraVfsService *service,
                                     uint32_t session, AstraVfsFile file,
                                     uint64_t offset, uint32_t flags, void *buffer,
                                     uint32_t length, uint32_t *moved, uint64_t *position);

/* Cache owners retain an open description across client close after checking
 * nonzero READ/WRITE access. The token keeps the description's original access;
 * each operation checks its own permission. Each retain needs one release.
 * Tokens stay internal to VFS. */
uint32_t astra_vfs_service_retain_file(AstraVfsService *service,
    uint32_t session, AstraVfsFile file, uint32_t access, AstraVfsFile *retained);
/* Retain the borrowed callback file while it owns node I/O. The reference keeps
 * its original access/backend through close, without reopening a path. */
uint32_t astra_vfs_service_retain_active(AstraVfsService *service,
    const AstraVfsOpenFile *active, AstraVfsFile *copy);
/* Any returned area is caller-owned, including on cleanup failure. */
uint32_t astra_vfs_service_file_area(AstraVfsService *service,
    uint32_t session, AstraVfsFile file, uint64_t base, uint32_t span,
    AstraVfsFileAreaCreate create, void *context,
    uint32_t *area, uint64_t *file_size);
uint32_t astra_vfs_service_read_retained(AstraVfsService *service,
    AstraVfsFile retained, uint64_t offset, void *buffer,
    uint32_t length, uint32_t *moved);
/* One aligned pager page from the retained description. A complete expected
 * identity guards shared cache objects; NULL means this open has its own
 * cache object. Short backend reads are joined, and size/identity are
 * rechecked before a caller may publish the page. An independent retained
 * reference pins I/O without taking the description's exclusive cursor lock.
 * Overlapping description I/O returns BUSY instead of publishing its page.
 * access is exactly READ for readers or WRITE for internal partial-write
 * preparation. WRITE uses the backend's read_for_write, never client READ. */
uint32_t astra_vfs_service_read_page_retained(AstraVfsService *service,
    AstraVfsFile retained, uint32_t access, const AstraVfsNodeMeta *expected_identity,
    uint64_t offset, void *page, uint32_t *valid_bytes);
/* Shared page reader for a service-owned backend description. The caller
 * authorizes the operation and keeps `node` open until this call returns. */
uint32_t astra_vfs_backend_read_page(const AstraVfsBackend *backend,
    uintptr_t node, uint32_t access, const AstraVfsNodeMeta *expected_identity,
    const AstraVfsNodeInfo *expected_revision, uint64_t offset,
    void *page, uint32_t *valid_bytes);
/* Write and sync one positioned snapshot, trimmed to logical_size. Expected
 * describes the current backing revision, independently of cached logical EOF.
 * Writing can extend backing EOF through this page but never truncates it.
 * Caller
 * retains/serializes the node and reconciles external changes before cleaning
 * its VM snapshot. Short writes are joined; cursor/append state is untouched.
 * `moved` reports validated accepted bytes even on error. `current` is zero
 * unless a checked post-I/O stat succeeded, including after partial failure.
 * Optional modified time is persisted before sync; filesystem precision applies.
 * An error never authorizes cleaning the VM snapshot. */
uint32_t astra_vfs_backend_write_page(const AstraVfsBackend *backend,
    uintptr_t node, const AstraVfsNodeInfo *expected, uint64_t offset, uint64_t logical_size,
    const AstraVfsNodeInfo *modified, const void *page, uint32_t *moved, AstraVfsNodeInfo *current);
/* With active NULL, acquire the node across write/sync/stat. Cache callbacks
 * pass their held active file instead; the retained WRITE description must
 * share its I/O owner, which this helper then leaves held. Accepted prefixes
 * count as mutations. This is a private service callback contract. */
uint32_t astra_vfs_service_write_page_retained(AstraVfsService *service,
    AstraVfsFile retained, const AstraVfsOpenFile *active,
    const AstraVfsNodeInfo *expected, uint64_t offset, uint64_t logical_size,
    const void *page, uint32_t *moved, AstraVfsNodeInfo *current);
uint32_t astra_vfs_service_stat_retained(AstraVfsService *service,
    AstraVfsFile retained, AstraVfsNodeInfo *info);
uint32_t astra_vfs_service_write_retained(AstraVfsService *service,
    AstraVfsFile retained, uint64_t offset, uint64_t max_end,
    const void *buffer, uint32_t length, uint32_t *moved);
/* Bounded maintenance never waits for an already-active node I/O owner. */
uint32_t astra_vfs_service_sync_retained(AstraVfsService *service,
    AstraVfsFile retained, uint32_t *remaining);
/* Clears the owned reference when consumed, including backend close errors.
 * A failed wait/lock leaves it owned so its caller can retry cleanup. */
uint32_t astra_vfs_service_release_retained(AstraVfsService *service,
    AstraVfsFile *retained);

/* The write-side twin: bytes come from the session's bound transfer area. */
uint32_t astra_vfs_service_write_from(AstraVfsService *service,
                                      uint32_t session, AstraVfsFile file,
                                      uint64_t offset, uint64_t max_end, const void *buffer,
                                      uint32_t length, uint32_t *moved);
uint32_t astra_vfs_service_write_position_from(
    AstraVfsService *service, uint32_t session, AstraVfsFile file,
    uint64_t offset, uint32_t flags, uint64_t max_end, const void *buffer, uint32_t length,
    uint32_t *moved, uint64_t *position);

/* Packs directory records into an arbitrary bounded buffer. */
uint32_t astra_vfs_service_readdir_into(
    AstraVfsService *service, uint32_t session, const char *path, uint64_t cursor,
    uint32_t entry_limit, uint8_t *buffer, uint32_t capacity,
    uint32_t *used, uint64_t *next);
uint32_t astra_vfs_service_readdir_file_into(
    AstraVfsService *service, uint32_t session, AstraVfsFile directory,
    const char *path, uint64_t cursor, uint32_t entry_limit, uint8_t *buffer,
    uint32_t capacity, uint32_t *used, uint64_t *next);

/*
 * Releases everything a session held. Called when a client dies; the port
 * layer learns that from peer-dead and the core does not need to know how.
 */
void astra_vfs_service_release_session(AstraVfsService *service,
                                       uint32_t session);
/* Retry closing sessions after transport reply lanes have already retired. */
void astra_vfs_service_reap(AstraVfsService *service);

/*
 * The transport must have stopped accepting requests and joined its workers.
 * VFS closes every session, then flushes and unmounts the mounted filesystem.
 * A read-only backend may leave flush NULL; it is a successful no-op. Neither
 * VFS nor its callers know whether the volume is ext4, AstraFS, CDFS, or RAM.
 */
typedef struct AstraVfsMountOps {
    uint32_t (*flush)(void *context);
    uint32_t (*unmount)(void *context);
} AstraVfsMountOps;

uint32_t astra_vfs_service_shutdown(AstraVfsService *service,
                                     const AstraVfsMountOps *mount,
                                     void *context);

const AstraVfsServiceStats *astra_vfs_service_stats(
    const AstraVfsService *service);

#endif

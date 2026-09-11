/** @file vfs_client.h @brief Concurrent Filesystem Kit transport client. */
#ifndef ASTRA_VFS_CLIENT_H
#define ASTRA_VFS_CLIENT_H

#include <stdint.h>

#ifndef __cplusplus
#include <stdatomic.h>
/** C atomic storage qualifier used by the shared C/C++ ABI. */
#define ASTRA_VFS_ATOMIC(type) _Atomic(type)
#else
/* C++ owns only the storage; filesystem.library performs atomic access. */
/** C++ storage spelling for fields atomically accessed by the library. */
#define ASTRA_VFS_ATOMIC(type) type
#endif

#include <astra/limits.h>
#include <astra/vfs_service.h>

/*
 * The client side of the storage protocol: the Kit half.
 *
 * This exists so a caller writes astra_vfs_open() instead of building a
 * record, and so that the day the service moves into its own process the
 * caller does not change at all. Everything it does is marshal a request,
 * hand it to a transport, and unpack the reply.
 *
 * The transport is a callback rather than a port handle because that is the
 * whole point of the exercise. Today the service is a call away in the same
 * process; tomorrow it is a port away in another one. Only the function behind
 * this pointer changes, and no caller of the Kit is recompiled for it.
 *
 * See docs/DRIVER_AND_SERVICE_ARCHITECTURE.md 9.2: this file's version and the
 * protocol's version are different numbers on purpose.
 */

struct AstraVfsClient;
struct AstraVfsBackendOps;

/**
 * Exchange one VFS request with a backend transport.
 * @param context Transport-owned context.
 * @param operation ASTRA_VFS_OP_* operation.
 * @param request Request to send.
 * @param reply Receives the service reply.
 * @return ASTRA_VFS_* transport status.
 */
typedef uint32_t (*AstraVfsTransport)(void *context, uint32_t operation,
                                      const AstraVfsRequest *request,
                                      AstraVfsReply *reply);
/**
 * Locate the shared payload area for a connected client.
 * @param client Connected client.
 * @param capacity Receives payload capacity in bytes.
 * @return Borrowed payload bytes, or NULL when no area is available.
 */
typedef const uint8_t *(*AstraVfsAreaPayload)(
    const struct AstraVfsClient *client, uint32_t *capacity);

/** Per-thread request/reply scratch owned by a VFS client. */
typedef struct AstraVfsCallState {
    /** @cond ASTRA_INTERNAL */
    AstraVfsRequest request;
    AstraVfsRenameRequest rename_request;
    AstraVfsReply reply;
    /** @endcond */
} AstraVfsCallState;

/**
 * Acquire scratch state dedicated to the calling thread.
 * @param client Connected client.
 * @return Borrowed per-thread state, or NULL when unavailable.
 */
typedef AstraVfsCallState *(*AstraVfsCallAcquire)(
    struct AstraVfsClient *client);

/** Private per-thread state for the port transport. */
typedef struct AstraVfsPortThreadState {
    /** @cond ASTRA_INTERNAL */
    uint32_t owner_thread;
    AstraVfsCallState client_call;
    AstraVfsRequestMessageBuffer outgoing;
    AstraVfsReplyMessage incoming;
    AstraVfsRequest area_request;
    AstraVfsRequest request;
    AstraVfsReply reply;
    /** @endcond */
} AstraVfsPortThreadState;

/** Private capability lane assigned to one calling thread. */
typedef struct AstraVfsPortLane {
    /** @cond ASTRA_INTERNAL */
    uint32_t owner_thread;
    uint32_t session;
    uint32_t reply_receive;
    uint32_t reply_source;
    uint32_t reply_send;
    uint32_t area;
    uint32_t area_send;
    void *area_address;
    uint32_t area_size;
    uint32_t direct_area;
    void *direct_address;
    uint32_t direct_size;
    /** @endcond */
} AstraVfsPortLane;
/** Optional transport accelerator used behind the VFS client contract. */
typedef struct AstraVfsPortAcceleratorOps {
    /** Establish an accelerated device session. */
    uint32_t (*connect)(struct AstraVfsClient *client, uint32_t device);
    /** Disconnect a clean accelerated session. */
    void (*disconnect)(struct AstraVfsClient *client);
    /** Abandon a failed accelerated session without further I/O. */
    void (*abandon)(struct AstraVfsClient *client);
    /** Exchange one metadata request and reply. */
    uint32_t (*transport)(struct AstraVfsClient *client, uint32_t operation,
                          const AstraVfsRequest *request,
                          AstraVfsReply *reply);
    /** Exchange one request carrying bulk data. */
    uint32_t (*bulk)(struct AstraVfsClient *client, uint32_t operation,
                     const AstraVfsRequest *request, void *buffer,
                     uint32_t capacity, AstraVfsReply *reply);
} AstraVfsPortAcceleratorOps;

/** Connected VFS client with independent per-thread in-flight state. */
typedef struct AstraVfsClient {
    /** @cond ASTRA_INTERNAL */
    AstraVfsTransport transport;
    void *context;
    AstraVfsAreaPayload area_payload;
    AstraVfsCallAcquire call_acquire;
    ASTRA_VFS_ATOMIC(uint32_t) session;
    ASTRA_VFS_ATOMIC(uint16_t) version; /* the version agreed at connect */
    /*
     * What the owner of this client is currently doing. Stamped on every
     * request so one request is one story across every process it touches.
     * Zero until somebody sets it, which reads as no activity.
     */
    uint32_t activity;
    /* Private state used by astra_vfs_port_connect/transport. */
    uint8_t port_area_capable;
    uint8_t port_direct_detached;
    uint32_t port_service;
    void *port_direct_address;
    uint32_t port_direct_area;
    uint32_t port_direct_device;
    uint32_t port_direct_session;
    uint32_t port_direct_lock;
    const struct AstraVfsBackendOps *direct_backend_ops;
    void *direct_backend_context;
    uint32_t (*direct_backend_enter)(struct AstraVfsClient *client);
    void (*direct_backend_leave)(struct AstraVfsClient *client);
    uint32_t port_connect_lock;
    volatile uint32_t port_connecting;
    volatile uint32_t port_inflight;
    volatile uint32_t port_inflight_waiters;
    volatile uint32_t port_lifecycle;
    volatile uint32_t port_thread_lock;
    volatile uint32_t port_lane_lock;
    const AstraVfsPortAcceleratorOps *port_accelerator_ops;
    AstraVfsPortThreadState *port_thread_states;
    uint32_t port_thread_capacity;
    AstraVfsPortLane port_lanes[ASTRA_PROCESS_THREAD_COUNT_MAX];
    /*
     * The in-flight records live here rather than on the caller's stack, and
     * that is a requirement rather than a preference: a user thread gets one
     * 4 KiB stack, and at 224 bytes each a request/reply pair per frame sank
     * the shell -> Kit -> service -> backend -> lwext4 chain into a fault at
     * the first command. Holding them in the client costs the same memory once
     * instead of once per call depth, and makes it caller-owned and countable
     * like every other buffer in this system.
     *
     * Retained for the process-state ABI while marshalling moves to
     * thread-local call records. New code must not use these as in-flight
     * storage: one process session is intentionally shared by many threads.
     */
    AstraVfsCallState call;
    /** @endcond */
} AstraVfsClient;

#undef ASTRA_VFS_ATOMIC

/**
 * Performs the HELLO handshake and records the agreed version. Returns an
 * ASTRA_VFS_* status; the client is unusable unless this returns ASTRA_VFS_OK.
 * @param client Unconnected client storage to initialize.
 * @param transport Request transport callback.
 * @param context Borrowed transport context retained until disconnect.
 * @return ASTRA_VFS_* status.
 */
uint32_t astra_vfs_connect(AstraVfsClient *client, AstraVfsTransport transport,
                           void *context);
/**
 * Disconnect a client and invalidate its negotiated session.
 * @param client Connected client.
 * @return ASTRA_VFS_* status.
 */
uint32_t astra_vfs_disconnect(AstraVfsClient *client);

/**
 * Open a path with backend-default creation permissions.
 * @param client Connected client.
 * @param path NUL-terminated mount-relative UTF-8 path.
 * @param flags ASTRA_VFS_OPEN_* flags.
 * @param file Receives an open file token.
 * @param size Receives byte length.
 * @param kind Receives ASTRA_VFS_NODE_* kind.
 * @return ASTRA_VFS_* status.
 */
uint32_t astra_vfs_open(AstraVfsClient *client, const char *path,
                        uint32_t flags, AstraVfsFile *file,
                        uint64_t *size, uint16_t *kind);
/**
 * Open a path with explicit creation permissions.
 * @param client Connected client.
 * @param path NUL-terminated mount-relative UTF-8 path.
 * @param flags ASTRA_VFS_OPEN_* flags.
 * @param create_mode POSIX permission bits used only when creating.
 * @param file Receives an open file token.
 * @param size Receives byte length.
 * @param kind Receives ASTRA_VFS_NODE_* kind.
 * @return ASTRA_VFS_* status.
 */
uint32_t astra_vfs_open_mode(AstraVfsClient *client, const char *path,
                             uint32_t flags, uint16_t create_mode,
                             AstraVfsFile *file, uint64_t *size,
                             uint16_t *kind);
/**
 * Close an open backend file token.
 * @param client Connected client.
 * @param file Open file token.
 * @return ASTRA_VFS_* status.
 */
uint32_t astra_vfs_close(AstraVfsClient *client, AstraVfsFile file);

/**
 * Reads and writes are short by design: one message carries at most
 * ASTRA_VFS_IO_MAX bytes. `moved` always reports what actually happened, and a
 * caller that needs more loops. Writing that loop now is not wasted work --
 * when bulk transfer moves to shared rings a transfer can still be short, and
 * a caller that already loops does not change.
 * @param client Connected client.
 * @param file Open file token.
 * @param offset Byte offset.
 * @param buffer Receives at most `length` bytes.
 * @param length Requested byte count.
 * @param moved Receives bytes read.
 * @return ASTRA_VFS_* status.
 */
uint32_t astra_vfs_read(AstraVfsClient *client, AstraVfsFile file,
                        uint64_t offset, void *buffer, uint32_t length,
                        uint32_t *moved);
/**
 * Write bytes at an explicit file offset.
 * @param client Connected client.
 * @param file Open file token.
 * @param offset Byte offset.
 * @param buffer Source bytes.
 * @param length Requested byte count.
 * @param moved Receives bytes written.
 * @return ASTRA_VFS_* status.
 */
uint32_t astra_vfs_write(AstraVfsClient *client, AstraVfsFile file,
                         uint64_t offset, const void *buffer, uint32_t length,
                         uint32_t *moved);
/**
 * Write bytes and return the backend-selected resulting position.
 * @param client Connected client.
 * @param file Open file token.
 * @param offset Byte offset or append sentinel defined by the wire ABI.
 * @param buffer Source bytes.
 * @param length Requested byte count.
 * @param moved Receives bytes written.
 * @param position Receives the resulting file position.
 * @return ASTRA_VFS_* status.
 */
uint32_t astra_vfs_write_position(AstraVfsClient *client, AstraVfsFile file,
                                  uint64_t offset, const void *buffer,
                                  uint32_t length, uint32_t *moved,
                                  uint64_t *position);
/**
 * Make an open file's completed writes durable.
 * @param client Connected client.
 * @param file Open file token.
 * @return ASTRA_VFS_* status.
 */
uint32_t astra_vfs_sync(AstraVfsClient *client, AstraVfsFile file);
/**
 * Set an open file's byte length.
 * @param client Connected client.
 * @param file Open file token.
 * @param size New byte length.
 * @return ASTRA_VFS_* status.
 */
uint32_t astra_vfs_truncate(AstraVfsClient *client, AstraVfsFile file,
                            uint64_t size);

/**
 * Read basic metadata for a path, following its final symbolic link.
 * @param client Connected client.
 * @param path Mount-relative UTF-8 path.
 * @param size Receives byte length.
 * @param kind Receives ASTRA_VFS_NODE_* kind.
 * @return ASTRA_VFS_* status.
 */
uint32_t astra_vfs_stat(AstraVfsClient *client, const char *path,
                        uint64_t *size, uint16_t *kind);
/**
 * One entry, resuming from `cursor`; zero begins a scan and `*next` is what to
 * pass for the entry after this one. Returns ASTRA_VFS_ERR_NOT_FOUND once a
 * scan passes the last entry, which is how a listing ends rather than a
 * failure.
 * @param client Connected client.
 * @param path Mount-relative directory path.
 * @param cursor Opaque cursor, or zero to begin.
 * @param name Receives a NUL-terminated UTF-8 leaf name.
 * @param capacity Bytes available in `name`.
 * @param kind Receives ASTRA_VFS_NODE_* kind.
 * @param next Receives the next opaque cursor, or zero at end.
 * @return ASTRA_VFS_* status.
 */
uint32_t astra_vfs_readdir(AstraVfsClient *client, const char *path,
                           uint64_t cursor, char *name, uint32_t capacity,
                           uint16_t *kind, uint64_t *next);

/** Directory entry and metadata returned by batched enumeration. */
typedef struct AstraVfsDirEntry {
    char name[ASTRA_VFS_NAME_MAX]; /**< NUL-terminated UTF-8 leaf name. */
    uint64_t size; /**< File length in bytes. */
    int64_t mtime; /**< Modification time in Unix seconds. */
    uint32_t uid; /**< Owning user identifier, or zero when unavailable. */
    uint32_t gid; /**< Owning group identifier, or zero when unavailable. */
    uint16_t kind; /**< ASTRA_VFS_NODE_* kind. */
    uint16_t mode; /**< POSIX permission and type bits. */
    uint16_t nlink; /**< Hard-link count, or zero when unavailable. */
    uint16_t reserved; /**< Must be zero. */
} AstraVfsDirEntry;

/**
 * Fills up to `capacity` entries in one service exchange. `*next == 0` after
 * a nonempty result means the service reached the end of the directory.
 * Version 2/3 peers transparently return one entry at a time.
 */
/**
 * Read full metadata for a path, following its final symbolic link.
 * @param client Connected client.
 * @param path Mount-relative UTF-8 path.
 * @param meta Receives metadata.
 * @return ASTRA_VFS_* status.
 */
uint32_t astra_vfs_stat_meta(AstraVfsClient *client, const char *path,
                             AstraVfsDirEntry *meta);

/**
 * Read a batch from a directory path.
 * @param client Connected client.
 * @param path Mount-relative directory path.
 * @param cursor Opaque cursor, or zero to begin.
 * @param entries Receives at most `capacity` entries.
 * @param capacity Number of entries available.
 * @param count Receives entries written.
 * @param next Receives the next cursor, or zero at end.
 * @return ASTRA_VFS_* status.
 */
uint32_t astra_vfs_readdir_batch(AstraVfsClient *client, const char *path,
                                  uint64_t cursor, AstraVfsDirEntry *entries,
                                  uint32_t capacity, uint32_t *count,
                                  uint64_t *next);
/**
 * Read a batch using an already-open directory token.
 * @param client Connected client.
 * @param directory Open directory token.
 * @param path Mount-relative directory path for legacy peers.
 * @param cursor Opaque cursor, or zero to begin.
 * @param entries Receives at most `capacity` entries.
 * @param capacity Number of entries available.
 * @param count Receives entries written.
 * @param next Receives the next cursor, or zero at end.
 * @return ASTRA_VFS_* status.
 */
uint32_t astra_vfs_readdir_file_batch(
    AstraVfsClient *client, AstraVfsFile directory, const char *path,
    uint64_t cursor, AstraVfsDirEntry *entries, uint32_t capacity,
    uint32_t *count, uint64_t *next);
/**
 * Create a directory with backend-default permissions.
 * @param client Connected client.
 * @param path Mount-relative UTF-8 path.
 * @return ASTRA_VFS_* status.
 */
uint32_t astra_vfs_mkdir(AstraVfsClient *client, const char *path);
/**
 * Create a directory with explicit permission bits.
 * @param client Connected client.
 * @param path Mount-relative UTF-8 path.
 * @param create_mode POSIX permission bits.
 * @return ASTRA_VFS_* status.
 */
uint32_t astra_vfs_mkdir_mode(AstraVfsClient *client, const char *path,
                              uint16_t create_mode);
/**
 * Remove one directory entry.
 * @param client Connected client.
 * @param path Mount-relative UTF-8 path.
 * @return ASTRA_VFS_* status.
 */
uint32_t astra_vfs_unlink(AstraVfsClient *client, const char *path);
/**
 * Atomically rename one path within a mounted backend.
 * @param client Connected client.
 * @param from Existing mount-relative path.
 * @param to Destination mount-relative path.
 * @return ASTRA_VFS_* status.
 */
uint32_t astra_vfs_rename(AstraVfsClient *client, const char *from,
                          const char *to);
/**
 * Change a node's permission bits.
 * @param client Connected client.
 * @param path Mount-relative UTF-8 path.
 * @param mode New POSIX permission bits.
 * @return ASTRA_VFS_* status.
 */
uint32_t astra_vfs_chmod(AstraVfsClient *client, const char *path,
                         uint16_t mode);
/**
 * Read a symbolic link without following it.
 * @param client Connected client.
 * @param path Mount-relative link path.
 * @param buffer Receives target bytes without an implied terminator.
 * @param capacity Bytes available in `buffer`.
 * @param length Receives target byte length.
 * @return ASTRA_VFS_* status.
 */
uint32_t astra_vfs_readlink(AstraVfsClient *client, const char *path,
                            void *buffer, uint32_t capacity,
                            uint32_t *length);
/**
 * Create a symbolic link.
 * @param client Connected client.
 * @param target Link target stored verbatim as UTF-8.
 * @param path Mount-relative path of the new link.
 * @return ASTRA_VFS_* status.
 */
uint32_t astra_vfs_symlink(AstraVfsClient *client, const char *target,
                           const char *path);
/**
 * Create a hard link within a mounted backend.
 * @param client Connected client.
 * @param from Existing mount-relative path.
 * @param to Mount-relative path of the new link.
 * @return ASTRA_VFS_* status.
 */
uint32_t astra_vfs_link(AstraVfsClient *client, const char *from,
                        const char *to);

#endif

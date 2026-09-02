#ifndef ASTRA_VFS_CLIENT_H
#define ASTRA_VFS_CLIENT_H

#include <stdatomic.h>
#include <stdint.h>

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

typedef uint32_t (*AstraVfsTransport)(void *context, uint32_t operation,
                                      const AstraVfsRequest *request,
                                      AstraVfsReply *reply);
typedef const uint8_t *(*AstraVfsAreaPayload)(const struct AstraVfsClient *,
                                              uint32_t *capacity);

typedef struct AstraVfsCallState {
    AstraVfsRequest request;
    AstraVfsRenameRequest rename_request;
    AstraVfsReply reply;
} AstraVfsCallState;

typedef AstraVfsCallState *(*AstraVfsCallAcquire)(
    struct AstraVfsClient *client);

typedef struct AstraVfsPortThreadState {
    uint32_t owner_thread;
    AstraVfsCallState client_call;
    AstraVfsRequestMessageBuffer outgoing;
    AstraVfsReplyMessage incoming;
    AstraVfsRequest area_request;
    AstraVfsRequest request;
    AstraVfsReply reply;
} AstraVfsPortThreadState;

typedef struct AstraVfsPortLane {
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
} AstraVfsPortLane;
typedef struct AstraVfsPortAcceleratorOps {
    uint32_t (*connect)(struct AstraVfsClient *client, uint32_t device);
    void (*disconnect)(struct AstraVfsClient *client);
    void (*abandon)(struct AstraVfsClient *client);
    uint32_t (*transport)(struct AstraVfsClient *client, uint32_t operation,
                          const AstraVfsRequest *request,
                          AstraVfsReply *reply);
    uint32_t (*bulk)(struct AstraVfsClient *client, uint32_t operation,
                     const AstraVfsRequest *request, void *buffer,
                     uint32_t capacity, AstraVfsReply *reply);
} AstraVfsPortAcceleratorOps;

typedef struct AstraVfsClient {
    AstraVfsTransport transport;
    void *context;
    AstraVfsAreaPayload area_payload;
    AstraVfsCallAcquire call_acquire;
    _Atomic(uint32_t) session;
    _Atomic(uint16_t) version; /* the version agreed at connect */
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
} AstraVfsClient;

/*
 * Performs the HELLO handshake and records the agreed version. Returns an
 * ASTRA_VFS_* status; the client is unusable unless this returns ASTRA_VFS_OK.
 */
uint32_t astra_vfs_connect(AstraVfsClient *client, AstraVfsTransport transport,
                           void *context);
uint32_t astra_vfs_disconnect(AstraVfsClient *client);

uint32_t astra_vfs_open(AstraVfsClient *client, const char *path,
                        uint32_t flags, AstraVfsFile *file,
                        uint64_t *size, uint16_t *kind);
uint32_t astra_vfs_open_mode(AstraVfsClient *client, const char *path,
                             uint32_t flags, uint16_t create_mode,
                             AstraVfsFile *file, uint64_t *size,
                             uint16_t *kind);
uint32_t astra_vfs_close(AstraVfsClient *client, AstraVfsFile file);

/*
 * Reads and writes are short by design: one message carries at most
 * ASTRA_VFS_IO_MAX bytes. `moved` always reports what actually happened, and a
 * caller that needs more loops. Writing that loop now is not wasted work --
 * when bulk transfer moves to shared rings a transfer can still be short, and
 * a caller that already loops does not change.
 */
uint32_t astra_vfs_read(AstraVfsClient *client, AstraVfsFile file,
                        uint64_t offset, void *buffer, uint32_t length,
                        uint32_t *moved);
uint32_t astra_vfs_write(AstraVfsClient *client, AstraVfsFile file,
                         uint64_t offset, const void *buffer, uint32_t length,
                         uint32_t *moved);
uint32_t astra_vfs_write_position(AstraVfsClient *client, AstraVfsFile file,
                                  uint64_t offset, const void *buffer,
                                  uint32_t length, uint32_t *moved,
                                  uint64_t *position);
uint32_t astra_vfs_sync(AstraVfsClient *client, AstraVfsFile file);
uint32_t astra_vfs_truncate(AstraVfsClient *client, AstraVfsFile file,
                            uint64_t size);

uint32_t astra_vfs_stat(AstraVfsClient *client, const char *path,
                        uint64_t *size, uint16_t *kind);
/*
 * One entry, resuming from `cursor`; zero begins a scan and `*next` is what to
 * pass for the entry after this one. Returns ASTRA_VFS_ERR_NOT_FOUND once a
 * scan passes the last entry, which is how a listing ends rather than a
 * failure.
 */
uint32_t astra_vfs_readdir(AstraVfsClient *client, const char *path,
                           uint64_t cursor, char *name, uint32_t capacity,
                           uint16_t *kind, uint64_t *next);

typedef struct AstraVfsDirEntry {
    char name[ASTRA_VFS_NAME_MAX];
    uint64_t size;
    int64_t mtime;
    uint32_t uid;
    uint32_t gid;
    uint16_t kind;
    uint16_t mode;
    uint16_t nlink;
    uint16_t reserved;
} AstraVfsDirEntry;

/*
 * Fills up to `capacity` entries in one service exchange. `*next == 0` after
 * a nonempty result means the service reached the end of the directory.
 * Version 2/3 peers transparently return one entry at a time.
 */
uint32_t astra_vfs_stat_meta(AstraVfsClient *client, const char *path,
                             AstraVfsDirEntry *meta);

uint32_t astra_vfs_readdir_batch(AstraVfsClient *client, const char *path,
                                  uint64_t cursor, AstraVfsDirEntry *entries,
                                  uint32_t capacity, uint32_t *count,
                                  uint64_t *next);
uint32_t astra_vfs_readdir_file_batch(
    AstraVfsClient *client, AstraVfsFile directory, const char *path,
    uint64_t cursor, AstraVfsDirEntry *entries, uint32_t capacity,
    uint32_t *count, uint64_t *next);
uint32_t astra_vfs_mkdir(AstraVfsClient *client, const char *path);
uint32_t astra_vfs_mkdir_mode(AstraVfsClient *client, const char *path,
                              uint16_t create_mode);
uint32_t astra_vfs_unlink(AstraVfsClient *client, const char *path);
uint32_t astra_vfs_rename(AstraVfsClient *client, const char *from,
                          const char *to);
uint32_t astra_vfs_chmod(AstraVfsClient *client, const char *path,
                         uint16_t mode);
uint32_t astra_vfs_readlink(AstraVfsClient *client, const char *path,
                            void *buffer, uint32_t capacity,
                            uint32_t *length);
uint32_t astra_vfs_symlink(AstraVfsClient *client, const char *target,
                           const char *path);

#endif

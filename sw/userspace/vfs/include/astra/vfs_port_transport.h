#ifndef ASTRA_VFS_PORT_TRANSPORT_H
#define ASTRA_VFS_PORT_TRANSPORT_H

/*
 * The storage protocol across a process boundary.
 *
 * Both ends of one crossing, in one file, because they are one agreement: what
 * the client puts on the wire and what the service takes off it cannot be
 * changed apart. `astra_vfs_local_transport` stays beside this and is not
 * deprecated by it -- a service in the caller's own process should not pay for
 * a port it does not need, and the host tests are built on it.
 *
 * The Kit's transport callback was written for this day. No client of the
 * storage protocol changes to cross a process: it is handed a different
 * transport at connect and everything above is untouched.
 */

#include <stdint.h>

#include <astra/vfs_client.h>
#include <astra/vfs_service.h>
#include <astra/vfs_service_core.h>

typedef struct AstraVfsPortExecLane {
    uint32_t owner_thread;
    uint32_t session;
    uint32_t reply_receive;
    uint32_t reply_source;
    uint32_t reply_send;
    uint32_t area;
    uint32_t area_send;
    uint32_t area_size;
} AstraVfsPortExecLane;

/* Shared production implementation for service state locks and file waits. */
int astra_vfs_state_lock_acquire(void *context);
void astra_vfs_state_lock_release(void *context);
int astra_vfs_state_futex_wait(void *context,
                               volatile uint32_t *sequence,
                               uint32_t expected);
void astra_vfs_state_futex_wake(void *context,
                                volatile uint32_t *sequence);

/*
 * The client half. Use astra_vfs_port_connect(): version 20+ retains one reply
 * channel and transfer area per calling thread inside a shared session.
 * Versions 3 through 19 retain one channel per session; version 2 remains the
 * per-request fallback for a rolling update against an older service.
 *
 * The return value is the transport's verdict, never the filesystem's: a peer
 * that has gone is ASTRA_VFS_ERR_PEER, and a request that was answered is
 * ASTRA_VFS_OK with the answer in `reply->status`. Collapsing the two would
 * lose the only distinction a caller actually needs from a transport.
 */
uint32_t astra_vfs_port_transport(void *context, uint32_t operation,
                                  const AstraVfsRequest *request,
                                  AstraVfsReply *reply);
/* Lifecycle guard used by an in-process backend bound to this transport. */
uint32_t astra_vfs_port_client_enter(AstraVfsClient *client);
void astra_vfs_port_client_leave(AstraVfsClient *client);
AstraVfsCallState *astra_vfs_port_call_acquire(AstraVfsClient *client);
/*
 * Astra executables use native TLS for call scratch. A shared library, whose
 * dynamic TLS ABI is intentionally not part of Astra yet, supplies one record
 * for each thread the process can actually own. Records are selected by the
 * generation-safe kernel thread handle; no request is serialized through a
 * process-global fallback.
 */
int astra_vfs_port_set_thread_storage(
    AstraVfsClient *client, AstraVfsPortThreadState *states,
    uint32_t capacity);
uint32_t astra_vfs_port_connect(AstraVfsClient *client, uint32_t service);
uint32_t astra_vfs_port_connect_with_accelerator(
    AstraVfsClient *client, uint32_t service,
    const AstraVfsPortAcceleratorOps *accelerator);
/* Version 8: defer HELLO and fuse it with the first path operation. */
uint32_t astra_vfs_port_connect_lazy(AstraVfsClient *client,
                                     uint32_t service);
uint32_t astra_vfs_port_connect_lazy_with_accelerator(
    AstraVfsClient *client, uint32_t service,
    const AstraVfsPortAcceleratorOps *accelerator);
/* Drop this process's transport handles without sending BYE.  A fork child
 * inherited the handles, but not ownership of the parent's service session. */
void astra_vfs_port_abandon(AstraVfsClient *client);
/*
 * Open, read whole, close -- one round trip -- answered in the transfer area.
 *
 * This is the shape almost every read in the system actually wants: a manifest,
 * an icon, a library image, a program. Doing it as three operations cost three
 * round trips, and a round trip is milliseconds. Same borrow lifetime as
 * astra_vfs_port_read_borrow: valid until the next operation on this thread's
 * lane.
 *
 * Answers ASTRA_VFS_ERR_UNSUPPORTED against a peer older than version 5, and
 * ASTRA_VFS_ERR_LIMIT for a file larger than the area, filling `node_size` so
 * the caller can fall back knowing the size.
 */
uint32_t astra_vfs_port_read_path(AstraVfsClient *client, const char *path,
                                  const uint8_t **bytes, uint32_t *moved,
                                  uint64_t *node_size);
/* Version 7: whole files up to ASTRA_VFS_IO_MAX in the normal reply. */
uint32_t astra_vfs_port_read_path_inline(AstraVfsClient *client,
                                         const char *path,
                                         const uint8_t **bytes,
                                         uint32_t *moved,
                                         uint64_t *node_size);

/*
 * A bulk read that hands back the transfer area instead of copying out of it.
 *
 * `astra_vfs_port_read_bulk` reads into the shared area and then copies every
 * byte into the caller's buffer, which for a program image or a library is a
 * second full pass over the data and was measured at about a third of the cost
 * of reading one. A caller that only needs to look at the bytes -- parse a
 * manifest, hand an image to the library loader -- can read them where they
 * already are.
 *
 * The pointer is valid until the next operation on the calling thread's lane,
 * because that lane reuses the area. Copy anything that has to outlive that.
 *
 * Refuses a length the area cannot hold; the caller falls back to read_bulk.
 */
uint32_t astra_vfs_port_read_borrow(AstraVfsClient *client, AstraVfsFile file,
                                    uint64_t offset, uint32_t length,
                                    const uint8_t **bytes, uint32_t *moved);

uint32_t astra_vfs_port_read_bulk(AstraVfsClient *client, AstraVfsFile file,
                                  uint64_t offset, void *buffer,
                                  uint32_t length, uint32_t *moved);

/* Version 9: one write from the caller's bound transfer area. */
uint32_t astra_vfs_port_write_bulk(AstraVfsClient *client, AstraVfsFile file,
                                   uint64_t offset, const void *buffer,
                                   uint32_t length, uint32_t *moved);
/* Version 21: bulk write plus its atomic post-write position. */
uint32_t astra_vfs_port_write_bulk_position(
    AstraVfsClient *client, AstraVfsFile file, uint64_t offset,
    uint32_t flags, const void *buffer, uint32_t length, uint32_t *moved,
    uint64_t *position);

/* Payload produced by the calling thread's most recent area operation. */
const uint8_t *astra_vfs_port_call_area(const AstraVfsClient *client,
                                        uint32_t *capacity);
uint32_t astra_vfs_port_exec_lane_export(AstraVfsClient *client,
                                         AstraVfsPortExecLane *state);
uint32_t astra_vfs_port_exec_lane_import(AstraVfsClient *client,
                                         const AstraVfsPortExecLane *state);

/*
 * Maps one reserved area as a lazily committed service table. Physical pages
 * are charged to the service owner only when records are touched; the owner's
 * area quota and the VFS handle width are the only ceilings.
 */
int astra_vfs_port_quota_storage(uint32_t element_size, void **storage,
                                 uint32_t *capacity);

/*
 * The service half: a receive-dispatch-reply pump, run from whichever loop
 * hosts the service. A pump rather than a loop for the same reason everything
 * else here is one -- the supervisor serves its own children, so a service that
 * blocked in its own receive would stop the child it is serving.
 */
typedef struct AstraVfsPortWorker {
    AstraVfsRequestMessageBuffer incoming;
    AstraVfsReplyMessage outgoing;
} AstraVfsPortWorker;

typedef struct AstraVfsPortService {
    uint32_t receive;
    AstraVfsService *service;
    uint32_t requests;   /* dispatched and answered */
    uint32_t refused;    /* received, and not this protocol */
    uint32_t dropped;    /* answered nobody: the reply had nowhere to go */
    /*
     * The last thing a receive said that was not "the port is empty". A pump
     * that stopped for any other reason used to leave no trace at all, and a
     * service that silently stops receiving looks exactly like one nobody is
     * calling -- which cost an afternoon telling those two apart.
     */
    uint32_t stalled;
    uint32_t accelerator;
    uint32_t reply_sessions[ASTRA_VFS_SESSION_MAX];
    uint32_t reply_lanes[ASTRA_VFS_SESSION_MAX];
    uint32_t reply_handles[ASTRA_VFS_SESSION_MAX];
    uint32_t area_handles[ASTRA_VFS_SESSION_MAX];
    uint8_t *area_addresses[ASTRA_VFS_SESSION_MAX];
    uint32_t area_sizes[ASTRA_VFS_SESSION_MAX];
    uint16_t reply_references[ASTRA_VFS_SESSION_MAX];
    uint8_t reply_closing[ASTRA_VFS_SESSION_MAX];
    /* Scratch for the single-thread pump adapter. Real workers own theirs. */
    AstraVfsPortWorker adapter_worker;
    AstraVfsStateAcquire state_acquire;
    AstraVfsStateRelease state_release;
    void *state_lock_context;
} AstraVfsPortService;

int astra_vfs_port_service_init(AstraVfsPortService *host, uint32_t receive,
                                AstraVfsService *service);
int astra_vfs_port_service_set_state_lock(AstraVfsPortService *host,
                                          AstraVfsStateAcquire acquire,
                                          AstraVfsStateRelease release,
                                          void *context);
int astra_vfs_port_service_set_accelerator(AstraVfsPortService *host,
                                           uint32_t accelerator);

/* Handles at most `budget` requests and returns how many it answered. */
uint32_t astra_vfs_port_service_pump(AstraVfsPortService *host,
                                     uint32_t budget);
uint32_t astra_vfs_port_service_worker_pump(AstraVfsPortService *host,
                                            AstraVfsPortWorker *worker,
                                            uint32_t budget);

#endif

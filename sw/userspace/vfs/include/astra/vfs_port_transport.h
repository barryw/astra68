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

/*
 * The client half. Use astra_vfs_port_connect(): version 3 transfers one reply
 * send handle during HELLO and reuses the receive handle for the session.
 * Version 2 remains the per-request fallback for a rolling update against an
 * older service.
 *
 * The return value is the transport's verdict, never the filesystem's: a peer
 * that has gone is ASTRA_VFS_ERR_PEER, and a request that was answered is
 * ASTRA_VFS_OK with the answer in `reply->status`. Collapsing the two would
 * lose the only distinction a caller actually needs from a transport.
 */
uint32_t astra_vfs_port_transport(void *context, uint32_t operation,
                                  const AstraVfsRequest *request,
                                  AstraVfsReply *reply);
uint32_t astra_vfs_port_connect(AstraVfsClient *client, uint32_t service);
uint32_t astra_vfs_port_read_bulk(AstraVfsClient *client, AstraVfsFile file,
                                  uint64_t offset, void *buffer,
                                  uint32_t length, uint32_t *moved);

/*
 * The service half: a receive-dispatch-reply pump, run from whichever loop
 * hosts the service. A pump rather than a loop for the same reason everything
 * else here is one -- the supervisor serves its own children, so a service that
 * blocked in its own receive would stop the child it is serving.
 */
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
    uint32_t reply_sessions[ASTRA_VFS_SESSION_MAX];
    uint32_t reply_handles[ASTRA_VFS_SESSION_MAX];
    uint32_t area_handles[ASTRA_VFS_SESSION_MAX];
    uint8_t *area_addresses[ASTRA_VFS_SESSION_MAX];
    uint32_t area_sizes[ASTRA_VFS_SESSION_MAX];
} AstraVfsPortService;

int astra_vfs_port_service_init(AstraVfsPortService *host, uint32_t receive,
                                AstraVfsService *service);

/* Handles at most `budget` requests and returns how many it answered. */
uint32_t astra_vfs_port_service_pump(AstraVfsPortService *host,
                                     uint32_t budget);

#endif

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

#include <astra/vfs_service.h>
#include <astra/vfs_service_core.h>

/*
 * The client half. `context` is a pointer to the send handle the launch
 * granted -- a `uint32_t *`, not a type of its own, because a handle is the
 * whole of what this end knows.
 *
 * A reply port per request. Attaching a handle to a port message *moves* it,
 * so a cached reply handle names nothing after its first use; two syscalls per
 * request is what that costs, against a round trip that already crosses a
 * process. The reply channel being a capability handed over per call is also
 * what lets the service answer exactly the caller that asked and hold no
 * authority afterwards.
 *
 * The return value is the transport's verdict, never the filesystem's: a peer
 * that has gone is ASTRA_VFS_ERR_PEER, and a request that was answered is
 * ASTRA_VFS_OK with the answer in `reply->status`. Collapsing the two would
 * lose the only distinction a caller actually needs from a transport.
 */
uint32_t astra_vfs_port_transport(void *context, uint32_t operation,
                                  const AstraVfsRequest *request,
                                  AstraVfsReply *reply);

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
} AstraVfsPortService;

int astra_vfs_port_service_init(AstraVfsPortService *host, uint32_t receive,
                                AstraVfsService *service);

/* Handles at most `budget` requests and returns how many it answered. */
uint32_t astra_vfs_port_service_pump(AstraVfsPortService *host,
                                     uint32_t budget);

#endif

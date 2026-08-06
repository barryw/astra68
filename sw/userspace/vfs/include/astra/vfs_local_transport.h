#ifndef ASTRA_VFS_LOCAL_TRANSPORT_H
#define ASTRA_VFS_LOCAL_TRANSPORT_H

#include <astra/vfs_client.h>
#include <astra/vfs_service_core.h>

/*
 * Connects a client to a service living in the same process. `context` is the
 * AstraVfsService. Pass this to astra_vfs_connect() and the caller is written
 * against the protocol from the first line, before a process boundary exists
 * to put underneath it.
 */
uint32_t astra_vfs_local_transport(void *context, uint32_t operation,
                                   const AstraVfsRequest *request,
                                   AstraVfsReply *reply);

#endif

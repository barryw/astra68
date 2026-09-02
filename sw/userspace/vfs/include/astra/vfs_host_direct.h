#ifndef ASTRA_VFS_HOST_DIRECT_H
#define ASTRA_VFS_HOST_DIRECT_H

#include <stdint.h>

#include <astra/vfs_client.h>

uint32_t astra_vfs_host_direct_connect(AstraVfsClient *client,
                                       uint32_t device);
/* Resume the local service state retained across an in-place process exec. */
uint32_t astra_vfs_host_direct_resume(AstraVfsClient *client, uint32_t area,
                                      uint32_t device, uint32_t session);
/* Replace a fork child's cloned, unmapped accelerator with its own channel. */
uint32_t astra_vfs_host_direct_after_fork(AstraVfsClient *client);
void astra_vfs_host_direct_disconnect(AstraVfsClient *client);
void astra_vfs_host_direct_abandon(AstraVfsClient *client);
uint32_t astra_vfs_host_direct_transport(
    AstraVfsClient *client, uint32_t operation,
    const AstraVfsRequest *request, AstraVfsReply *reply);
uint32_t astra_vfs_host_direct_bulk(
    AstraVfsClient *client, uint32_t operation,
    const AstraVfsRequest *request, void *buffer, uint32_t capacity,
    AstraVfsReply *reply);
uint32_t astra_vfs_host_port_connect(AstraVfsClient *client,
                                     uint32_t service);
uint32_t astra_vfs_host_port_connect_lazy(AstraVfsClient *client,
                                          uint32_t service);

#endif

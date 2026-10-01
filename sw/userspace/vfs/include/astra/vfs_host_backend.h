#ifndef ASTRA_VFS_HOST_BACKEND_H
#define ASTRA_VFS_HOST_BACKEND_H

#include <astra/host.h>
#include <astra/vfs_backend.h>

typedef struct AstraVfsHostRequest {
    AstraHostCommand *command;
    uintptr_t private_lane;
    uint32_t private_capacity;
    uint32_t private_producer;
} AstraVfsHostRequest;

typedef struct AstraVfsHostTransport AstraVfsHostTransport;

typedef struct AstraVfsHostBackend {
    AstraVfsHostTransport *transport;
    uint32_t generation;
    /* The HostFS volume every command names (ASTRA_HOST_FS_VOLUME_*). */
    uint32_t volume;
} AstraVfsHostBackend;

int astra_vfs_host_init(AstraVfsHostBackend *backend,
                        AstraVfsHostTransport *transport,
                        uint32_t generation, uint32_t volume);
const AstraVfsBackendOps *astra_vfs_host_ops(void);

#endif

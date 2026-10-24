#ifndef ASTRA_VFS_HOST_BACKEND_H
#define ASTRA_VFS_HOST_BACKEND_H

#include <astra/host.h>
#include <astra/vfs_backend.h>
#include <astra/volume.h>
#include <astra/volume_service.h>

typedef struct AstraVfsHostRequest {
    AstraHostCommand *command;
    /* Path operands, copied into the data span by submit. */
    const char *path;
    uint32_t path_length;
    const char *path2;
    uint32_t path2_length;
    uintptr_t private_lane;
    uint32_t private_capacity;
    uint32_t private_producer;
} AstraVfsHostRequest;

typedef struct AstraVfsHostTransport AstraVfsHostTransport;

typedef struct AstraVfsHostBackend {
    AstraVfsHostTransport *transport;
    uint32_t generation;
    /* The provider session every command names (ASTRA_HOST_FS_SESSION_OPEN). */
    uint32_t session;
    /* The scope handle commands go through (HOST_SCOPE_ATTACH on first use
     * by a lane); 0: the transport's own device handle. */
    uint32_t scope;
    /* A rooted path answered LOST; handle-only loss leaves the session usable. */
    uint32_t lost;
} AstraVfsHostBackend;

int astra_vfs_host_init(AstraVfsHostBackend *backend,
                        AstraVfsHostTransport *transport,
                        uint32_t generation, uint32_t session);
const AstraVfsBackendOps *astra_vfs_host_ops(void);

/* The provider's control operations, on the transport's own (unbound)
 * channel: claim the filesystem service for this process (or release it),
 * and open or close a rooted session. A root session (parent 0) opens
 * `root` beneath `volume`; a derived one opens it beneath its parent's
 * root. Each answers an ASTRA_VFS_* status. */
uint32_t astra_vfs_host_claim(AstraVfsHostTransport *transport,
                              uint32_t generation, int release,
                              uint32_t *epoch);
uint32_t astra_vfs_host_session_open(AstraVfsHostTransport *transport,
                                     uint32_t generation, uint32_t parent,
                                     const AstraVolumeReference *volume, const char *root,
                                     uint16_t flags, uint32_t requester, uint32_t *session);
uint32_t astra_vfs_host_usage(AstraVfsHostTransport *transport, uint32_t generation,
    const AstraVolumeUsageQuery *query, AstraVolumeUsagePage *page);
uint32_t astra_vfs_host_usage_holders_changed(AstraVfsHostTransport *transport,
    uint32_t generation);
uint32_t astra_vfs_host_session_close(AstraVfsHostTransport *transport,
                                      uint32_t generation, uint32_t session);
/* Public metadata only; no filesystem claim, data session or root is lent. */
uint32_t astra_vfs_host_lookup_mount(AstraVfsHostTransport *transport, uint32_t generation,
                                     const char *name, uint32_t required_flags,
                                     AstraVolumeCatalogPage *scratch,
                                     AstraVolumeReference *volume);
uint32_t astra_vfs_host_catalog(AstraVfsHostTransport *transport, uint32_t generation,
                                const AstraVolumeCatalogQuery *query,
                                AstraVolumeCatalogPage *page);
uint32_t astra_vfs_host_volume_control(AstraVfsHostTransport *transport,
    uint32_t generation, uint32_t requester, const AstraVolumeControl *control,
    AstraVolumeReference *reference);
uint32_t astra_vfs_host_volume_format(AstraVfsHostTransport *transport,
    uint32_t generation, uint32_t requester, const AstraVolumeFormatRequest *format,
    uint32_t *outcome);
uint32_t astra_vfs_host_volume_partition(AstraVfsHostTransport *transport,
    uint32_t generation, uint32_t requester, const AstraVolumePartitionRequest *partition,
    uint32_t *outcome);

#endif

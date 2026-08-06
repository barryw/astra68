#ifndef ASTRA_SUPERVISOR_VFS_HOST_H
#define ASTRA_SUPERVISOR_VFS_HOST_H

#include <astra/vfs_client.h>

/*
 * Stands the storage service up over an already-mounted volume and opens the
 * supervisor's own session. Returns 0 when the backend, the service or the
 * protocol handshake refuses.
 */
int supervisor_vfs_start(const char *mount_point);

/* NULL until supervisor_vfs_start() has succeeded. */
AstraVfsClient *supervisor_vfs_client(void);

#endif

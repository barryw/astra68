/*
 * The storage service, hosted by whoever mounted the volume.
 *
 * This is the only file in the supervisor that knows a filesystem exists. It
 * binds lwext4 to the storage protocol's backend interface, stands the service
 * core on top, and hands out a connected client. The terminal above it names
 * no filesystem at all.
 *
 * The service is in this process because userspace cannot create a process --
 * no syscall does it. That is a deployment limit, not a design one: every
 * caller already goes through the protocol, so the day a loader exists, this
 * file is replaced by a launch and a port transport, and no caller changes.
 * See docs/DRIVER_AND_SERVICE_ARCHITECTURE.md 9.1.
 */

#include <vfs_host.h>

#include <astra/vfs_ext4_backend.h>
#include <astra/vfs_local_transport.h>
#include <astra/vfs_service_core.h>

static AstraVfsExt4Backend vfs_backend;
static AstraVfsService vfs_service;
static AstraVfsClient vfs_client;
static int vfs_ready;

int
supervisor_vfs_start(const char *mount_point)
{
    if (vfs_ready) {
        return 1;
    }
    if (!astra_vfs_ext4_init(&vfs_backend, mount_point)) {
        return 0;
    }
    if (!astra_vfs_service_init(&vfs_service, astra_vfs_ext4_ops(),
                                &vfs_backend)) {
        return 0;
    }
    /*
     * The supervisor's own client is opened here rather than by the terminal,
     * so that a failure to agree a protocol version is a start-up failure with
     * somewhere to report it, instead of the first `ls` returning something
     * unhelpful.
     */
    if (astra_vfs_connect(&vfs_client, astra_vfs_local_transport,
                          &vfs_service) != ASTRA_VFS_OK) {
        return 0;
    }
    vfs_ready = 1;
    return 1;
}

AstraVfsClient *
supervisor_vfs_client(void)
{
    return vfs_ready ? &vfs_client : NULL;
}

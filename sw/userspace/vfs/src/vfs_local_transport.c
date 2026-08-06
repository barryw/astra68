/*
 * The same-process transport.
 *
 * This is the analogue of sw/userspace/input's port sink, and it is
 * deliberately the most boring file in the module: it hands a request straight
 * to the service core and lets the reply come back.
 *
 * Its value is that it is replaceable. There is no way for userspace to create
 * a process yet -- no such syscall exists -- so the storage service currently
 * runs inside whoever mounted the volume. Every caller nonetheless goes through
 * the protocol, so the day a loader exists, a port transport is written beside
 * this one and the callers are not touched. The process boundary becomes a
 * deployment decision instead of a rewrite.
 *
 * The return value is the transport's own verdict, not the filesystem's. A
 * local call cannot fail to be delivered, so it always says OK and the answer
 * is in reply->status. A port transport would report a dead peer here instead,
 * which is exactly the distinction a caller needs and would not have if the
 * two were collapsed.
 */

#include <astra/vfs_local_transport.h>

#include <stddef.h>

uint32_t
astra_vfs_local_transport(void *context, uint32_t operation,
                          const AstraVfsRequest *request,
                          AstraVfsReply *reply)
{
    if (context == NULL) {
        return ASTRA_VFS_ERR_INVALID;
    }
    astra_vfs_service_dispatch((AstraVfsService *)context, operation, request,
                               reply);
    return ASTRA_VFS_OK;
}

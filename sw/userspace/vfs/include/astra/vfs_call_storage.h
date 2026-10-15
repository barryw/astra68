#ifndef ASTRA_VFS_CALL_STORAGE_H
#define ASTRA_VFS_CALL_STORAGE_H

#include <astra/runtime.h>
#include <astra/attributes.h>
#include <astra/vfs_client.h>

/* Operand/result memory belongs to one synchronous native call. TLS supplies
 * small control records only; retiring a client or thread cannot strand path
 * buffers. The transport must finish using the view before returning. */
static inline void astra_vfs_call_storage_close(AstraVfsCallState **owner)
{
    AstraVfsCallState *call = *owner;
    if (call == NULL)
        return;
    astra_runtime_deallocate(call->path_bytes);
    astra_runtime_deallocate(call->result_bytes);
    call->path_bytes = NULL;
    call->result_bytes = NULL;
    call->path_capacity = call->result_capacity = 0u;
    call->transfer = (AstraVfsTransfer){0};
}

#define ASTRA_AUTO_VFS_CALL_STORAGE ASTRA_CLEANUP(astra_vfs_call_storage_close)
#endif

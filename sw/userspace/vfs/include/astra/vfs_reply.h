#ifndef ASTRA_VFS_REPLY_H
#define ASTRA_VFS_REPLY_H

#include <astra/vfs_service.h>

/* Validate the fixed reply before either transport admission or native callers
 * publish or interpret any of its fields. */
static inline int astra_vfs_reply_valid(const AstraVfsReply *reply)
{
    return reply->size == (uint16_t)ASTRA_VFS_REPLY_SIZE &&
        reply->version == ASTRA_VFS_VERSION &&
        reply->reserved == 0u && reply->description_reserved == 0u &&
        (reply->meta.valid & ~ASTRA_VFS_META_ALL) == 0u &&
        reply->meta.reserved == 0u && reply->meta.identity_reserved == 0u &&
        (!(reply->meta.valid & ASTRA_VFS_META_VOLUME) || reply->meta.volume != 0u) &&
        (!(reply->meta.valid & ASTRA_VFS_META_ATIME) || reply->meta.atime_nsec < 1000000000u) &&
        (!(reply->meta.valid & ASTRA_VFS_META_MTIME) || reply->meta.mtime_nsec < 1000000000u) &&
        (!(reply->meta.valid & ASTRA_VFS_META_CTIME) || reply->meta.ctime_nsec < 1000000000u);
}

#endif

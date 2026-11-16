#ifndef ASTRA_VFS_REPLY_H
#define ASTRA_VFS_REPLY_H

#include <astra/vfs_service.h>
#include <string.h>

static inline void astra_vfs_reply_init(AstraVfsReply *reply,
    uint16_t version, uint32_t session)
{
    memset(reply, 0, sizeof(*reply));
    reply->size = ASTRA_VFS_REPLY_SIZE;
    reply->version = version;
    reply->session = session;
    reply->status = ASTRA_VFS_ERR_PROTOCOL;
}

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

static inline int astra_vfs_io_result_valid(uint32_t status, uint32_t flags,
    uint32_t length, uint32_t count, uint64_t position)
{
    return count <= length &&
        ((status != ASTRA_VFS_OK && count == 0u) ||
         (flags & ASTRA_VFS_IO_SEQUENTIAL) == 0u ||
         (position >= count && position <= INT64_MAX));
}

/* A validated reply may contain completed bytes alongside its error. Failed
 * transport/validation must clear the reply before reaching this helper. */
static inline uint32_t astra_vfs_reply_io_result(const AstraVfsReply *reply,
    uint32_t status, uint32_t flags, uint32_t length,
    uint32_t *moved, uint64_t *position)
{
    *moved = 0u;
    if (!astra_vfs_io_result_valid(status, flags, length, reply->count,
                                   reply->position))
        return ASTRA_VFS_ERR_PROTOCOL;
    if (status == ASTRA_VFS_OK || reply->count != 0u) {
        *moved = reply->count;
        *position = reply->position;
    }
    return status;
}

#endif

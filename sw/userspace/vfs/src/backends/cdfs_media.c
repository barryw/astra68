#include <astra/vfs_cdfs_backend.h>

#include <limits.h>
#include <string.h>

static odfs_err_t media_error(uint32_t status)
{
    if (status == ASTRA_VFS_OK) return ODFS_OK;
    if (status == ASTRA_VFS_ERR_PEER) return ODFS_ERR_MEDIA_CHANGED;
    return ODFS_ERR_IO;
}

uint32_t astra_cdfs_media_check(AstraCdfsMedia *media)
{
    AstraBlockGeometry now;
    AstraBlockStatus status;

    if (media == NULL) return ASTRA_VFS_ERR_INVALID;
    if (media->status != ASTRA_VFS_OK) return media->status;
    if (media->block == NULL) {
        AstraFileInfo info = ASTRA_FILE_INFO_INIT;
        uint32_t result = astra_filesystem_file_info(media->file, &info);
        if (result != ASTRA_VFS_OK) return result;
        if (info.byte_size != (uint64_t)media->sectors * ASTRA_CDFS_SECTOR_BYTES ||
            info.mtime != media->mtime)
            media->status = ASTRA_VFS_ERR_PEER;
        return media->status;
    }
    status = astra_block_query(media->block, &now);
    if (status == ASTRA_BLOCK_NO_MEDIA ||
        status == ASTRA_BLOCK_MEDIA_CHANGED ||
        (status == ASTRA_BLOCK_OK &&
         ((now.flags & ASTRA_BLOCK_FLAG_PRESENT) == 0u ||
          now.media_generation != media->geometry.media_generation ||
          now.sector_size != media->geometry.sector_size ||
          now.sector_count != media->geometry.sector_count))) {
        media->status = ASTRA_VFS_ERR_PEER;
        return media->status;
    }
    return status == ASTRA_BLOCK_OK ? ASTRA_VFS_OK : ASTRA_VFS_ERR_IO;
}

static odfs_err_t read_sectors(void *context, uint32_t lba,
                                uint32_t count, void *buffer)
{
    AstraCdfsMedia *media = context;
    uint8_t *out = buffer;
    uint32_t status = astra_cdfs_media_check(media);
    uint64_t offset = (uint64_t)lba * ASTRA_CDFS_SECTOR_BYTES;
    uint64_t bytes = (uint64_t)count * ASTRA_CDFS_SECTOR_BYTES;

    if (status != ASTRA_VFS_OK) return media_error(status);
    if (buffer == NULL || count == 0u || lba >= media->sectors ||
        count > media->sectors - lba || bytes > SIZE_MAX)
        return ODFS_ERR_RANGE;
    while (bytes != 0u) {
        uint32_t moved;
        if (media->block != NULL) {
            uint32_t sectors = media->geometry.max_transfer_sectors;
            uint64_t remaining = bytes / media->geometry.sector_size;
            AstraBlockStatus result;

            if ((uint64_t)sectors > remaining) sectors = (uint32_t)remaining;
            result = astra_block_read(media->block,
                offset / media->geometry.sector_size, sectors, out,
                ASTRA_DEADLINE_FOREVER);
            if (result == ASTRA_BLOCK_MEDIA_CHANGED ||
                result == ASTRA_BLOCK_NO_MEDIA) {
                media->status = ASTRA_VFS_ERR_PEER;
                return ODFS_ERR_MEDIA_CHANGED;
            }
            if (result != ASTRA_BLOCK_OK) return ODFS_ERR_IO;
            moved = sectors * media->geometry.sector_size;
        } else {
            uint32_t length = bytes > UINT32_MAX ? UINT32_MAX : (uint32_t)bytes;
            status = astra_filesystem_read_at(media->file, offset, out,
                                               length, &moved);
            if (status != ASTRA_VFS_OK) return media_error(status);
            if (moved == 0u || moved > length) return ODFS_ERR_IO;
        }
        offset += moved;
        out += moved;
        bytes -= moved;
    }
    return media_error(astra_cdfs_media_check(media));
}

static uint32_t sector_size(void *context)
{
    (void)context;
    return ASTRA_CDFS_SECTOR_BYTES;
}

static uint32_t sector_count(void *context)
{
    return ((AstraCdfsMedia *)context)->sectors;
}

static const odfs_media_ops_t media_ops = {
    .read_sectors = read_sectors,
    .sector_size = sector_size,
    .sector_count = sector_count
};

static uint32_t init_media(AstraCdfsMedia *media, uint64_t bytes)
{
    if (bytes == 0u || bytes % ASTRA_CDFS_SECTOR_BYTES != 0u ||
        bytes / ASTRA_CDFS_SECTOR_BYTES > UINT32_MAX)
        return ASTRA_VFS_ERR_INVALID;
    media->sectors = (uint32_t)(bytes / ASTRA_CDFS_SECTOR_BYTES);
    media->media.ops = &media_ops;
    media->media.ctx = media;
    return ASTRA_VFS_OK;
}

uint32_t astra_cdfs_media_block(AstraCdfsMedia *media, AstraBlockDevice *block)
{
    if (media == NULL || block == NULL) return ASTRA_VFS_ERR_INVALID;
    memset(media, 0, sizeof(*media));
    if (astra_block_query(block, &media->geometry) != ASTRA_BLOCK_OK)
        return ASTRA_VFS_ERR_IO;
    if ((media->geometry.flags & ASTRA_BLOCK_FLAG_PRESENT) == 0u)
        return ASTRA_VFS_ERR_PEER;
    if (media->geometry.sector_size > ASTRA_CDFS_SECTOR_BYTES ||
        media->geometry.sector_count > UINT64_MAX / media->geometry.sector_size)
        return ASTRA_VFS_ERR_INVALID;
    media->block = block;
    return init_media(media,
        media->geometry.sector_count * media->geometry.sector_size);
}

uint32_t astra_cdfs_media_file(AstraCdfsMedia *media, AstraFile *file)
{
    AstraFileInfo info = ASTRA_FILE_INFO_INIT;
    uint32_t status;

    if (media == NULL || file == NULL) return ASTRA_VFS_ERR_INVALID;
    memset(media, 0, sizeof(*media));
    status = astra_filesystem_file_info(file, &info);
    if (status != ASTRA_VFS_OK) return status;
    if (info.kind != ASTRA_VFS_KIND_FILE ||
        (info.open_flags & ASTRA_VFS_OPEN_READ) == 0u)
        return ASTRA_VFS_ERR_INVALID;
    media->file = file;
    media->mtime = info.mtime;
    return init_media(media, info.byte_size);
}

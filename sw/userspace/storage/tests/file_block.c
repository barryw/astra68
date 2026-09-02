/*
 * Host-only block backend over a file. See file_block.h for why it exists and
 * why it is not in src/.
 */

#define _FILE_OFFSET_BITS 64
#define _POSIX_C_SOURCE 200809L

#include "file_block.h"

#include <fcntl.h>
#include <stddef.h>
#include <sys/stat.h>
#include <unistd.h>

static uint64_t
byte_offset(const AstraFileBlock *file, uint64_t lba)
{
    return lba * (uint64_t)file->sector_size;
}

static AstraBlockStatus
file_query(void *context, AstraBlockGeometry *geometry)
{
    AstraFileBlock *file = context;

    if (file == NULL || geometry == NULL || file->descriptor < 0) {
        return ASTRA_BLOCK_INVALID_ARGUMENT;
    }
    geometry->sector_count = file->sector_count;
    geometry->sector_size = file->sector_size;
    geometry->max_transfer_sectors = file->max_transfer_sectors;
    geometry->queue_depth = 1u;
    geometry->media_generation = file->media_generation;
    geometry->flags = file->flags;
    return ASTRA_BLOCK_OK;
}

static AstraBlockStatus
file_read(void *context, uint64_t lba, uint32_t sector_count, void *buffer,
          uint64_t deadline)
{
    AstraFileBlock *file = context;
    unsigned char *out = buffer;
    uint64_t offset = byte_offset(file, lba);
    size_t remaining = (size_t)sector_count * file->sector_size;

    (void)deadline;
    while (remaining != 0u) {
        ssize_t got = pread(file->descriptor, out, remaining, (off_t)offset);

        if (got < 0) {
            return ASTRA_BLOCK_IO_ERROR;
        }
        if (got == 0) {
            /*
             * A sparse image may be shorter than its declared geometry.
             * Reading a hole past the end yields zeroes, which is what the
             * device would report for an unwritten sector.
             */
            while (remaining-- != 0u) {
                *out++ = 0u;
            }
            return ASTRA_BLOCK_OK;
        }
        out += (size_t)got;
        offset += (uint64_t)got;
        remaining -= (size_t)got;
    }
    return ASTRA_BLOCK_OK;
}

static AstraBlockStatus
file_write(void *context, uint64_t lba, uint32_t sector_count,
           const void *buffer, uint64_t deadline)
{
    AstraFileBlock *file = context;
    const unsigned char *in = buffer;
    uint64_t offset = byte_offset(file, lba);
    size_t remaining = (size_t)sector_count * file->sector_size;

    (void)deadline;
    while (remaining != 0u) {
        ssize_t put = pwrite(file->descriptor, in, remaining, (off_t)offset);

        if (put <= 0) {
            return ASTRA_BLOCK_IO_ERROR;
        }
        in += (size_t)put;
        offset += (uint64_t)put;
        remaining -= (size_t)put;
    }
    return ASTRA_BLOCK_OK;
}

static AstraBlockStatus
file_flush(void *context, uint64_t deadline)
{
    AstraFileBlock *file = context;

    (void)deadline;
    return fsync(file->descriptor) == 0 ? ASTRA_BLOCK_OK
                                        : ASTRA_BLOCK_IO_ERROR;
}

const AstraBlockBackend astra_file_block_backend = {
    .query = file_query,
    .read = file_read,
    .write = file_write,
    .flush = file_flush,
};

int
astra_file_block_open(AstraFileBlock *file, const char *path,
                      uint32_t sector_size, uint32_t max_transfer_sectors)
{
    struct stat status;

    if (file == NULL || path == NULL || sector_size == 0u) {
        return -1;
    }
    file->descriptor = open(path, O_RDWR);
    if (file->descriptor < 0) {
        return -1;
    }
    if (fstat(file->descriptor, &status) != 0) {
        (void)close(file->descriptor);
        file->descriptor = -1;
        return -1;
    }
    file->sector_size = sector_size;
    file->sector_count = (uint64_t)status.st_size / sector_size;
    file->max_transfer_sectors = max_transfer_sectors;
    file->media_generation = 1u;
    file->flags = ASTRA_BLOCK_FLAG_PRESENT;
    return file->sector_count != 0u ? 0 : -1;
}

void
astra_file_block_close(AstraFileBlock *file)
{
    if (file != NULL && file->descriptor >= 0) {
        (void)close(file->descriptor);
        file->descriptor = -1;
    }
}

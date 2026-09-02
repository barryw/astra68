#include <astra/ext4_port.h>

#include <ext4_errno.h>

#include <stddef.h>
#include <string.h>

/*
 * lwext4 recovers the port from the interface's user pointer. It is set once
 * in init and lwext4 never rewrites it, so a null here means a caller built an
 * ext4_blockdev by hand instead of through astra_ext4_port_init().
 */
static AstraExt4Port *
port_of(struct ext4_blockdev *blockdev)
{
    if (blockdev == NULL || blockdev->bdif == NULL) {
        return NULL;
    }
    return blockdev->bdif->p_user;
}

static uint64_t
transfer_deadline(const AstraExt4Port *port)
{
    if (port->transfer_timeout == 0u) {
        return 0u;
    }
    return astra_clock_read(port->device->clock, port->device->clock_context) +
           port->transfer_timeout;
}

static int
validate_window(AstraExt4Port *port, uint64_t block, uint64_t count)
{
    if (count > port->sector_count || block < port->first_sector ||
        block - port->first_sector > port->sector_count - count) {
        ++port->out_of_partition_refusals;
        port->last_status = ASTRA_BLOCK_OUT_OF_RANGE;
        return EINVAL;
    }
    return EOK;
}

int
astra_ext4_errno(AstraBlockStatus status)
{
    switch (status) {
    case ASTRA_BLOCK_OK:
        return EOK;
    case ASTRA_BLOCK_INVALID_ARGUMENT:
    case ASTRA_BLOCK_OUT_OF_RANGE:
    case ASTRA_BLOCK_TRANSFER_TOO_LARGE:
        return EINVAL;
    case ASTRA_BLOCK_NO_MEDIA:
    case ASTRA_BLOCK_MEDIA_CHANGED:
        /*
         * Both mean the volume lwext4 believes it mounted is not the volume
         * behind the device any more. ENODEV rather than EIO because retrying
         * the transfer cannot help; the mount has to be torn down.
         */
        return ENODEV;
    case ASTRA_BLOCK_READ_ONLY:
        return EROFS;
    case ASTRA_BLOCK_TIMED_OUT:
    case ASTRA_BLOCK_CANCELLED:
    case ASTRA_BLOCK_IO_ERROR:
    case ASTRA_BLOCK_CORRUPT:
        /*
         * Deliberately collapsed. lwext4 aborts the operation identically for
         * all four, and lwext4's own errno set has no ETIMEDOUT or ECANCELED
         * to map them onto. AstraExt4Port::last_status keeps the distinction
         * for whoever decides about retry.
         */
        return EIO;
    default:
        return EIO;
    }
}

/*
 * lwext4 asks for `count` blocks in one call and the facade will refuse
 * anything past max_transfer_sectors. Splitting here rather than propagating
 * the refusal keeps the cap where it belongs: it describes the transfer
 * engine, not the filesystem's right to read a range.
 */
static int
run_transfer(AstraExt4Port *port, void *read_buffer,
             const void *write_buffer, uint64_t block, uint32_t count)
{
    unsigned char *out = read_buffer;
    const unsigned char *in = write_buffer;
    uint32_t sector_bytes = port->device->geometry.sector_size;
    uint32_t chunk_max = port->device->geometry.max_transfer_sectors;

    if (chunk_max == 0u || sector_bytes == 0u) {
        port->last_status = ASTRA_BLOCK_INVALID_ARGUMENT;
        return EINVAL;
    }

    /*
     * The window is enforced here, before any address reaches the device.
     *
     * lwext4 has already added part_offset by this point, so `block` is an
     * absolute device address and the arithmetic that produced it is lwext4's.
     * Checking it against the window the caller declared is what makes a
     * miscomputed address, or a volume whose superblock lies about its size,
     * unable to reach the FAT partition that boots the machine. Without this
     * the only thing standing between a filesystem bug and an unbootable card
     * is that the bug happened not to point there.
     */
    if (validate_window(port, block, count) != EOK) {
        return EINVAL;
    }

    if (count > chunk_max) {
        ++port->split_transfers;
    }

    while (count != 0u) {
        uint32_t chunk = count < chunk_max ? count : chunk_max;
        uint64_t deadline = transfer_deadline(port);
        AstraBlockStatus status;

        if (out != NULL) {
            status = astra_block_read(port->device, block, chunk, out,
                                      deadline);
            out += (size_t)chunk * sector_bytes;
        } else {
            status = astra_block_write(port->device, block, chunk, in,
                                       deadline);
            in += (size_t)chunk * sector_bytes;
        }
        if (status != ASTRA_BLOCK_OK) {
            port->last_status = status;
            return astra_ext4_errno(status);
        }
        block += chunk;
        count -= chunk;
    }
    return EOK;
}

static int
port_open(struct ext4_blockdev *blockdev)
{
    AstraExt4Port *port = port_of(blockdev);
    AstraBlockGeometry geometry;
    AstraBlockStatus status;

    if (port == NULL) {
        return EINVAL;
    }
    /*
     * The facade has no open operation; the device is already leased by the
     * time a filesystem exists. Re-reading geometry is what "open" means here,
     * because it is the point at which a media change becomes visible.
     *
     * Queried into a local rather than straight into device->geometry: a
     * failed query must leave the device's recorded geometry as it was, not
     * half-overwritten with whatever the backend managed to fill in.
     */
    status = astra_block_query(port->device, &geometry);
    if (status != ASTRA_BLOCK_OK) {
        port->last_status = status;
        return astra_ext4_errno(status);
    }
    /*
     * Absence is reported as absence. It is distinguishable from a change and
     * the two call for different recovery, so collapsing them here would throw
     * away the only place the difference is visible.
     */
    if ((geometry.flags & ASTRA_BLOCK_FLAG_PRESENT) == 0u) {
        port->last_status = ASTRA_BLOCK_NO_MEDIA;
        return astra_ext4_errno(ASTRA_BLOCK_NO_MEDIA);
    }
    /*
     * A media change under a mounted volume invalidates every cached block
     * lwext4 holds. Reporting it here, where lwext4 will propagate the
     * failure, is the only honest answer: the port cannot re-validate a cache
     * it does not own.
     */
    if (geometry.media_generation != port->media_generation) {
        port->last_status = ASTRA_BLOCK_MEDIA_CHANGED;
        return astra_ext4_errno(ASTRA_BLOCK_MEDIA_CHANGED);
    }
    return EOK;
}

static int
port_close(struct ext4_blockdev *blockdev)
{
    AstraExt4Port *port = port_of(blockdev);

    if (port == NULL) {
        return EINVAL;
    }
    /*
     * The lease outlives the mount and detaching it is the service's decision,
     * not the filesystem's, so closing a volume must not release the device.
     */
    return EOK;
}

static int
port_bread(struct ext4_blockdev *blockdev, void *buffer, uint64_t block,
           uint32_t count)
{
    AstraExt4Port *port = port_of(blockdev);

    if (port == NULL || buffer == NULL) {
        return EINVAL;
    }
    if (count == 0u) {
        return EOK;
    }
    return run_transfer(port, buffer, NULL, block, count);
}

static int
port_bwrite(struct ext4_blockdev *blockdev, const void *buffer, uint64_t block,
            uint32_t count)
{
    AstraExt4Port *port = port_of(blockdev);

    if (port == NULL || buffer == NULL) {
        return EINVAL;
    }
    if (count == 0u) {
        return EOK;
    }
    return run_transfer(port, NULL, buffer, block, count);
}

static int
port_bwritev(struct ext4_blockdev *blockdev, const void *const *buffers,
             const uint32_t *counts, uint64_t block, uint32_t count)
{
    AstraExt4Port *port = port_of(blockdev);
    AstraBlockVector vector;
    AstraBlockStatus status;
    uint64_t sectors = 0u;
    uint32_t index;

    if (port == NULL || buffers == NULL || counts == NULL || count == 0u) {
        return EINVAL;
    }
    for (index = 0u; index < count; ++index) {
        if (buffers[index] == NULL || counts[index] == 0u ||
            sectors > UINT32_MAX - counts[index]) {
            return EINVAL;
        }
        sectors += counts[index];
    }
    if (validate_window(port, block, sectors) != EOK) {
        return EINVAL;
    }
    vector.buffers = buffers;
    vector.sector_counts = counts;
    vector.count = count;
    status = astra_block_writev(port->device, block, &vector,
                                transfer_deadline(port));
    if (status != ASTRA_BLOCK_OK) {
        port->last_status = status;
    }
    return astra_ext4_errno(status);
}

static int
port_flush(struct ext4_blockdev *blockdev)
{
    AstraExt4Port *port = port_of(blockdev);
    AstraBlockStatus status;

    if (port == NULL) {
        return EINVAL;
    }
    status = astra_block_flush(port->device, transfer_deadline(port));
    if (status != ASTRA_BLOCK_OK) {
        port->last_status = status;
    }
    return astra_ext4_errno(status);
}

static int
port_lock(struct ext4_blockdev *blockdev)
{
    AstraExt4Port *port = port_of(blockdev);

    if (port == NULL) {
        return EINVAL;
    }
    if (port->lock_depth != 0u) {
        ++port->reentry_refusals;
        return EBUSY;
    }
    port->lock_depth = 1u;
    return EOK;
}

static int
port_unlock(struct ext4_blockdev *blockdev)
{
    AstraExt4Port *port = port_of(blockdev);

    if (port == NULL) {
        return EINVAL;
    }
    if (port->lock_depth == 0u) {
        return EPERM;
    }
    port->lock_depth = 0u;
    return EOK;
}

AstraExt4Status
astra_ext4_port_init(AstraExt4Port *port, AstraBlockDevice *device,
                     const AstraExt4Partition *partition, void *sector_buffer,
                     uint32_t sector_buffer_bytes, uint64_t transfer_timeout)
{
    const AstraBlockGeometry *geometry;
    uint64_t first_sector = 0u;
    uint64_t sector_count;

    if (port == NULL || device == NULL || sector_buffer == NULL) {
        return ASTRA_EXT4_INVALID_ARGUMENT;
    }

    geometry = &device->geometry;
    if (geometry->sector_size == 0u || geometry->sector_count == 0u ||
        geometry->max_transfer_sectors == 0u) {
        return ASTRA_EXT4_GEOMETRY_UNSUPPORTED;
    }
    if ((geometry->flags & ASTRA_BLOCK_FLAG_PRESENT) == 0u) {
        return ASTRA_EXT4_NO_MEDIA;
    }
    if (sector_buffer_bytes < geometry->sector_size) {
        return ASTRA_EXT4_BUFFER_TOO_SMALL;
    }

    sector_count = geometry->sector_count;
    if (partition != NULL) {
        first_sector = partition->first_sector;
        if (first_sector >= geometry->sector_count) {
            return ASTRA_EXT4_PARTITION_INVALID;
        }
        sector_count = partition->sector_count != 0u
                           ? partition->sector_count
                           : geometry->sector_count - first_sector;
        if (sector_count > geometry->sector_count - first_sector) {
            return ASTRA_EXT4_PARTITION_INVALID;
        }
    }
    /*
     * A window smaller than one sector cannot hold a superblock, and one that
     * starts at sector 0 while shorter than the device is a partition overlaying
     * the partition table. Neither is a mount worth attempting.
     */
    if (sector_count == 0u) {
        return ASTRA_EXT4_PARTITION_INVALID;
    }

    memset(port, 0, sizeof(*port));

    port->device = device;
    port->transfer_timeout = transfer_timeout;
    port->last_status = ASTRA_BLOCK_OK;
    port->media_generation = geometry->media_generation;
    port->first_sector = first_sector;
    port->sector_count = sector_count;

    port->interface.open = port_open;
    port->interface.bread = port_bread;
    port->interface.bwrite = port_bwrite;
    port->interface.bwritev = port_bwritev;
    port->interface.flush = port_flush;
    port->interface.close = port_close;
    port->interface.lock = port_lock;
    port->interface.unlock = port_unlock;
    port->interface.ph_bsize = geometry->sector_size;
    port->interface.ph_bcnt = geometry->sector_count;
    port->interface.ph_bmax = geometry->max_transfer_sectors;
    port->interface.ph_bbuf = sector_buffer;
    port->interface.p_user = port;

    /*
     * ph_bcnt stays the whole device because lwext4 computes absolute
     * addresses from part_offset; part_size is what bounds the volume.
     */
    port->blockdev.bdif = &port->interface;
    port->blockdev.part_offset = first_sector * (uint64_t)geometry->sector_size;
    port->blockdev.part_size = sector_count * (uint64_t)geometry->sector_size;

    return ASTRA_EXT4_OK;
}

struct ext4_blockdev *
astra_ext4_port_blockdev(AstraExt4Port *port)
{
    return port != NULL ? &port->blockdev : NULL;
}

#ifndef ASTRA_EXT4_PORT_H
#define ASTRA_EXT4_PORT_H

#include <stdint.h>

#include <astra/block_device.h>

#include <ext4_blockdev.h>

/*
 * lwext4 bound to the Astra block facade.
 *
 * lwext4 wants a block device and a lock. `AstraBlockDevice` already supplies
 * the first, and it is transport ignorant by construction: the deterministic
 * memory backend and the lease-backed real device present the same interface.
 * Binding to the facade rather than to either backend is the whole reason
 * phase 3 built the facade, and it means the filesystem's own tests run on the
 * host against RAM while the identical code runs on hardware against the
 * device lease.
 *
 * The port owns three translations lwext4's interface cannot express:
 *
 *  - transfer size. lwext4 asks for as many blocks as it likes in one call;
 *    the facade refuses anything past the device's max_transfer_sectors. The
 *    port splits the request rather than failing it, because the cap is a
 *    property of the engine and not of the filesystem.
 *
 *  - deadlines. lwext4's interface has no timeout, so the port computes one
 *    per transfer from the device's own clock. Note that the lease backend
 *    currently derives its own wait deadline and ignores the one passed in;
 *    the value is still computed and passed because a backend that honours it
 *    must not need the port changed.
 *
 *  - failure. lwext4 speaks errno, the facade speaks AstraBlockStatus, and the
 *    mapping is lossy in one direction on purpose. Several distinct facade
 *    statuses become EIO because lwext4 would act identically on all of them.
 *    The exact status is kept in `last_status` so a caller deciding whether to
 *    retry can still tell a timeout from a bad sector.
 */

typedef enum AstraExt4Status {
    ASTRA_EXT4_OK = 0,
    ASTRA_EXT4_INVALID_ARGUMENT = 1,
    ASTRA_EXT4_BUFFER_TOO_SMALL = 2,
    ASTRA_EXT4_GEOMETRY_UNSUPPORTED = 3,
    ASTRA_EXT4_NO_MEDIA = 4,
    ASTRA_EXT4_PARTITION_INVALID = 5
} AstraExt4Status;

/*
 * The window on the device a mount is confined to.
 *
 * Astra's card carries a FAT boot partition alongside its own volumes, so a
 * mount is almost never the whole device. lwext4 biases every address by
 * `part_offset` itself, so the port's job is not to translate addresses — it is
 * to make the window an enforced boundary rather than an arithmetic
 * convention. A filesystem that miscomputes an address, or metadata on a
 * volume that lies about its own size, must not be able to reach the partition
 * holding the code that boots the machine.
 */
typedef struct AstraExt4Partition {
    uint64_t first_sector;
    uint64_t sector_count; /* 0 means "to the end of the device" */
} AstraExt4Partition;

typedef struct AstraExt4Port {
    /* Must stay first: lwext4 hands back &port->blockdev by address. */
    struct ext4_blockdev blockdev;
    struct ext4_blockdev_iface interface;

    AstraBlockDevice *device;

    /* Clock units added to "now" for each transfer's deadline. */
    uint64_t transfer_timeout;

    /* Exact facade status of the most recent failed transfer. */
    AstraBlockStatus last_status;

    /* Media generation at bind time; a change under a mount invalidates it. */
    uint32_t media_generation;

    /* The enforced window. Every transfer is checked against it. */
    uint64_t first_sector;
    uint64_t sector_count;

    /*
     * Transfers refused for falling outside the window. Any non-zero value is
     * a defect that would otherwise have written to another partition.
     */
    uint32_t out_of_partition_refusals;

    /*
     * Held across a block operation. lwext4's lock exists for multi-partition
     * use; Astra's service is single-threaded and synchronous, so the only way
     * to observe a nested lock is a bug. Counting and refusing it turns an
     * assumption into a check that fails loudly the day threads arrive.
     */
    uint32_t lock_depth;
    uint32_t reentry_refusals;

    /* Requests the port split because the device capped transfer length. */
    uint32_t split_transfers;
} AstraExt4Port;

/*
 * Binds `device` to a usable ext4_blockdev. `sector_buffer` becomes lwext4's
 * single-sector bounce buffer and must be at least one sector and aligned for
 * the device; the caller owns it so the port allocates nothing.
 *
 * `partition` confines the mount to a window of the device. Passing NULL means
 * the whole device, which is what a bare filesystem image wants and what a
 * partitioned card never does.
 *
 * `transfer_timeout` is in the units of the device's clock. Zero means no
 * deadline is imposed by the port.
 *
 * The device's geometry must already be populated by astra_block_query().
 */
AstraExt4Status astra_ext4_port_init(AstraExt4Port *port,
                                     AstraBlockDevice *device,
                                     const AstraExt4Partition *partition,
                                     void *sector_buffer,
                                     uint32_t sector_buffer_bytes,
                                     uint64_t transfer_timeout);

/* The handle to pass to ext4_device_register(). */
struct ext4_blockdev *astra_ext4_port_blockdev(AstraExt4Port *port);

/*
 * The facade-to-errno map, exposed because it is a contract rather than an
 * implementation detail: a VFS layer reporting a filesystem error needs to
 * agree with the port about what a given device failure means.
 */
int astra_ext4_errno(AstraBlockStatus status);

#endif

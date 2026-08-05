#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <astra/block_device.h>
#include <astra/ext4_port.h>
#include <astra/memory_block.h>

#include <ext4_errno.h>

/*
 * The port's own behaviour, driven through the ext4_blockdev callbacks
 * lwext4 would call. No lwext4 code is linked: this is about the three
 * translations the port owns — transfer splitting, the errno map, and the
 * lock — and those are exactly the parts a mount test would exercise only
 * incidentally.
 */

#define TEST_SECTOR_SIZE 512u
#define TEST_SECTORS 4096u
#define TEST_BYTES (TEST_SECTORS * TEST_SECTOR_SIZE)
#define TEST_MAX_TRANSFER 16u

static uint8_t storage[TEST_BYTES];
static uint8_t sector_buffer[TEST_SECTOR_SIZE];
static uint8_t transfer[64u * TEST_SECTOR_SIZE];
static uint8_t expected[64u * TEST_SECTOR_SIZE];

static uint64_t clock_ticks;

/*
 * A counted clock rather than a real one: deadlines are computed from it, and
 * a test that asserted on wall-clock values would be a flake generator.
 */
static uint64_t
ticks(void *context)
{
    (void)context;
    return ++clock_ticks;
}

static void
bring_up(AstraMemoryBlock *memory, AstraBlockDevice *device,
         AstraExt4Port *port)
{
    AstraBlockGeometry geometry;

    astra_memory_block_init(memory, storage, sizeof(storage),
                            TEST_SECTOR_SIZE, TEST_MAX_TRANSFER,
                            ASTRA_BLOCK_FLAG_PRESENT);
    astra_block_device_init(device, &astra_memory_block_backend, memory, ticks,
                            NULL);
    assert(astra_block_query(device, &geometry) == ASTRA_BLOCK_OK);
    assert(astra_ext4_port_init(port, device, sector_buffer,
                                sizeof(sector_buffer), 1000u) ==
           ASTRA_EXT4_OK);
}

static void
test_init_rejects_bad_bindings(void)
{
    AstraMemoryBlock memory;
    AstraBlockDevice device;
    AstraExt4Port port;
    AstraBlockGeometry geometry;

    assert(astra_ext4_port_init(NULL, &device, sector_buffer,
                                sizeof(sector_buffer), 0u) ==
           ASTRA_EXT4_INVALID_ARGUMENT);
    assert(astra_ext4_port_init(&port, NULL, sector_buffer,
                                sizeof(sector_buffer), 0u) ==
           ASTRA_EXT4_INVALID_ARGUMENT);
    assert(astra_ext4_port_init(&port, &device, NULL, TEST_SECTOR_SIZE, 0u) ==
           ASTRA_EXT4_INVALID_ARGUMENT);

    /* Geometry never queried: the device does not yet know its own shape. */
    astra_memory_block_init(&memory, storage, sizeof(storage),
                            TEST_SECTOR_SIZE, TEST_MAX_TRANSFER,
                            ASTRA_BLOCK_FLAG_PRESENT);
    astra_block_device_init(&device, &astra_memory_block_backend, &memory,
                            ticks, NULL);
    assert(astra_ext4_port_init(&port, &device, sector_buffer,
                                sizeof(sector_buffer), 0u) ==
           ASTRA_EXT4_GEOMETRY_UNSUPPORTED);

    assert(astra_block_query(&device, &geometry) == ASTRA_BLOCK_OK);
    assert(astra_ext4_port_init(&port, &device, sector_buffer,
                                TEST_SECTOR_SIZE - 1u, 0u) ==
           ASTRA_EXT4_BUFFER_TOO_SMALL);

    /* Media absent at bind time is refused rather than deferred to first I/O. */
    astra_memory_block_set_present(&memory, 0);
    assert(astra_block_query(&device, &geometry) == ASTRA_BLOCK_OK);
    assert(astra_ext4_port_init(&port, &device, sector_buffer,
                                sizeof(sector_buffer), 0u) ==
           ASTRA_EXT4_NO_MEDIA);
    astra_memory_block_set_present(&memory, 1);
}

static void
test_blockdev_shape(void)
{
    AstraMemoryBlock memory;
    AstraBlockDevice device;
    AstraExt4Port port;
    struct ext4_blockdev *blockdev;

    bring_up(&memory, &device, &port);
    blockdev = astra_ext4_port_blockdev(&port);

    assert(blockdev == &port.blockdev);
    assert(blockdev->bdif == &port.interface);
    assert(blockdev->bdif->p_user == &port);
    assert(blockdev->bdif->ph_bsize == TEST_SECTOR_SIZE);
    assert(blockdev->bdif->ph_bcnt == TEST_SECTORS);
    assert(blockdev->bdif->ph_bbuf == sector_buffer);
    assert(blockdev->part_offset == 0u);
    assert(blockdev->part_size == (uint64_t)TEST_BYTES);

    assert(astra_ext4_port_blockdev(NULL) == NULL);
}

/*
 * The reason the port exists in this shape: lwext4 asks for a run of blocks
 * with no knowledge of the transfer engine's cap, and the facade refuses
 * anything past it. A request four times the cap must still succeed, must move
 * exactly the right bytes, and must be visibly recorded as split.
 */
static void
test_transfer_is_split_not_refused(void)
{
    AstraMemoryBlock memory;
    AstraBlockDevice device;
    AstraExt4Port port;
    struct ext4_blockdev *blockdev;
    const uint32_t count = TEST_MAX_TRANSFER * 4u;
    const uint64_t block = 100u;
    size_t bytes = (size_t)count * TEST_SECTOR_SIZE;
    size_t index;

    bring_up(&memory, &device, &port);
    blockdev = astra_ext4_port_blockdev(&port);

    /* A bare facade call of this length is refused; that is the premise. */
    assert(astra_block_read(&device, block, count, transfer, 0u) ==
           ASTRA_BLOCK_TRANSFER_TOO_LARGE);

    for (index = 0u; index < bytes; ++index) {
        expected[index] = (uint8_t)(index * 7u + 3u);
    }
    assert(blockdev->bdif->bwrite(blockdev, expected, block, count) == EOK);
    assert(port.split_transfers == 1u);

    memset(transfer, 0xa5, bytes);
    assert(blockdev->bdif->bread(blockdev, transfer, block, count) == EOK);
    assert(port.split_transfers == 2u);
    assert(memcmp(transfer, expected, bytes) == 0);

    /* And the bytes landed at the right offset, not merely somewhere. */
    assert(memcmp(storage + (size_t)block * TEST_SECTOR_SIZE, expected,
                  bytes) == 0);

    /* Exactly at the cap is one transfer, not a split. */
    assert(blockdev->bdif->bread(blockdev, transfer, block,
                                 TEST_MAX_TRANSFER) == EOK);
    assert(port.split_transfers == 2u);

    /* A zero-length request is a no-op, not an error and not a transfer. */
    assert(blockdev->bdif->bread(blockdev, transfer, block, 0u) == EOK);
    assert(blockdev->bdif->bwrite(blockdev, expected, block, 0u) == EOK);
}

static void
test_failures_map_and_are_recorded(void)
{
    AstraMemoryBlock memory;
    AstraBlockDevice device;
    AstraExt4Port port;
    AstraBlockGeometry geometry;
    struct ext4_blockdev *blockdev;

    bring_up(&memory, &device, &port);
    blockdev = astra_ext4_port_blockdev(&port);

    /* Past the end of the volume. */
    assert(blockdev->bdif->bread(blockdev, transfer, TEST_SECTORS, 1u) ==
           EINVAL);
    assert(port.last_status == ASTRA_BLOCK_OUT_OF_RANGE);

    /* Straddling the end, which is the case a range check alone would miss. */
    assert(blockdev->bdif->bread(blockdev, transfer, TEST_SECTORS - 2u, 4u) ==
           EINVAL);
    assert(port.last_status == ASTRA_BLOCK_OUT_OF_RANGE);

    /* A device error mid-run. */
    astra_memory_block_fail_at(&memory, memory.operation_count + 1u);
    assert(blockdev->bdif->bread(blockdev, transfer, 0u, 1u) == EIO);
    assert(port.last_status == ASTRA_BLOCK_IO_ERROR);
    astra_memory_block_fail_at(&memory, 0u);

    /* A failure part-way through a split run still fails the whole request. */
    astra_memory_block_fail_at(&memory, memory.operation_count + 2u);
    assert(blockdev->bdif->bread(blockdev, transfer, 0u,
                                 TEST_MAX_TRANSFER * 3u) == EIO);
    assert(port.last_status == ASTRA_BLOCK_IO_ERROR);
    astra_memory_block_fail_at(&memory, 0u);

    /*
     * Media gone. The facade gates transfers on the geometry it last read, so
     * the change becomes visible at the next query rather than instantly; that
     * is the facade's contract and the port inherits it.
     */
    astra_memory_block_set_present(&memory, 0);
    assert(astra_block_query(&device, &geometry) == ASTRA_BLOCK_OK);
    assert(blockdev->bdif->bread(blockdev, transfer, 0u, 1u) == ENODEV);
    assert(port.last_status == ASTRA_BLOCK_NO_MEDIA);
    astra_memory_block_set_present(&memory, 1);
    assert(astra_block_query(&device, &geometry) == ASTRA_BLOCK_OK);

    /* Read-only media refuses writes but not reads. */
    memory.flags |= ASTRA_BLOCK_FLAG_READ_ONLY;
    assert(astra_block_query(&device, &geometry) == ASTRA_BLOCK_OK);
    assert(blockdev->bdif->bwrite(blockdev, transfer, 0u, 1u) == EROFS);
    assert(port.last_status == ASTRA_BLOCK_READ_ONLY);
    assert(blockdev->bdif->bread(blockdev, transfer, 0u, 1u) == EOK);
    memory.flags &= ~ASTRA_BLOCK_FLAG_READ_ONLY;
    assert(astra_block_query(&device, &geometry) == ASTRA_BLOCK_OK);

    /* Null arguments never reach the device. */
    assert(blockdev->bdif->bread(blockdev, NULL, 0u, 1u) == EINVAL);
    assert(blockdev->bdif->bwrite(blockdev, NULL, 0u, 1u) == EINVAL);
}

static void
test_errno_map_is_total(void)
{
    assert(astra_ext4_errno(ASTRA_BLOCK_OK) == EOK);
    assert(astra_ext4_errno(ASTRA_BLOCK_INVALID_ARGUMENT) == EINVAL);
    assert(astra_ext4_errno(ASTRA_BLOCK_OUT_OF_RANGE) == EINVAL);
    assert(astra_ext4_errno(ASTRA_BLOCK_TRANSFER_TOO_LARGE) == EINVAL);
    assert(astra_ext4_errno(ASTRA_BLOCK_NO_MEDIA) == ENODEV);
    assert(astra_ext4_errno(ASTRA_BLOCK_MEDIA_CHANGED) == ENODEV);
    assert(astra_ext4_errno(ASTRA_BLOCK_READ_ONLY) == EROFS);
    assert(astra_ext4_errno(ASTRA_BLOCK_TIMED_OUT) == EIO);
    assert(astra_ext4_errno(ASTRA_BLOCK_CANCELLED) == EIO);
    assert(astra_ext4_errno(ASTRA_BLOCK_IO_ERROR) == EIO);
    assert(astra_ext4_errno(ASTRA_BLOCK_CORRUPT) == EIO);
    /* Nothing maps to success by accident. */
    assert(astra_ext4_errno((AstraBlockStatus)999) == EIO);
}

/*
 * The service is single-threaded and synchronous, so a nested lock cannot
 * happen today. Refusing it rather than ignoring it is what makes the
 * assumption testable the day the service grows threads.
 */
static void
test_lock_refuses_reentry(void)
{
    AstraMemoryBlock memory;
    AstraBlockDevice device;
    AstraExt4Port port;
    struct ext4_blockdev *blockdev;

    bring_up(&memory, &device, &port);
    blockdev = astra_ext4_port_blockdev(&port);

    assert(blockdev->bdif->lock(blockdev) == EOK);
    assert(port.lock_depth == 1u);
    assert(blockdev->bdif->lock(blockdev) == EBUSY);
    assert(port.reentry_refusals == 1u);
    assert(blockdev->bdif->unlock(blockdev) == EOK);
    assert(port.lock_depth == 0u);

    /* Unlocking what was never locked is a defect, not a silent success. */
    assert(blockdev->bdif->unlock(blockdev) == EPERM);

    /* And the lock is reusable afterwards. */
    assert(blockdev->bdif->lock(blockdev) == EOK);
    assert(blockdev->bdif->unlock(blockdev) == EOK);
}

static void
test_open_detects_media_change(void)
{
    AstraMemoryBlock memory;
    AstraBlockDevice device;
    AstraExt4Port port;
    struct ext4_blockdev *blockdev;

    bring_up(&memory, &device, &port);
    blockdev = astra_ext4_port_blockdev(&port);

    assert(blockdev->bdif->open(blockdev) == EOK);

    /* Closing a volume must not disturb the lease the service owns. */
    assert(blockdev->bdif->close(blockdev) == EOK);
    assert(blockdev->bdif->bread(blockdev, transfer, 0u, 1u) == EOK);

    /* Ejecting and reinserting bumps the generation the port recorded. */
    astra_memory_block_set_present(&memory, 0);
    astra_memory_block_set_present(&memory, 1);
    assert(blockdev->bdif->open(blockdev) == ENODEV);
    assert(port.last_status == ASTRA_BLOCK_MEDIA_CHANGED);

    /* Open on absent media reports the absence, not a changed generation. */
    astra_memory_block_set_present(&memory, 0);
    assert(blockdev->bdif->open(blockdev) == ENODEV);
    assert(port.last_status == ASTRA_BLOCK_NO_MEDIA);
}

/* A hand-built ext4_blockdev has no port behind it and must be refused. */
static void
test_foreign_blockdev_is_refused(void)
{
    struct ext4_blockdev_iface interface;
    struct ext4_blockdev blockdev;
    AstraMemoryBlock memory;
    AstraBlockDevice device;
    AstraExt4Port port;
    struct ext4_blockdev *real;

    bring_up(&memory, &device, &port);
    real = astra_ext4_port_blockdev(&port);

    memset(&interface, 0, sizeof(interface));
    memset(&blockdev, 0, sizeof(blockdev));
    blockdev.bdif = &interface;

    assert(real->bdif->bread(&blockdev, transfer, 0u, 1u) == EINVAL);
    assert(real->bdif->bwrite(&blockdev, transfer, 0u, 1u) == EINVAL);
    assert(real->bdif->open(&blockdev) == EINVAL);
    assert(real->bdif->close(&blockdev) == EINVAL);
    assert(real->bdif->lock(&blockdev) == EINVAL);
    assert(real->bdif->unlock(&blockdev) == EINVAL);

    blockdev.bdif = NULL;
    assert(real->bdif->bread(&blockdev, transfer, 0u, 1u) == EINVAL);
    assert(real->bdif->bread(NULL, transfer, 0u, 1u) == EINVAL);
}

int
main(void)
{
    test_init_rejects_bad_bindings();
    test_blockdev_shape();
    test_transfer_is_split_not_refused();
    test_failures_map_and_are_recorded();
    test_errno_map_is_total();
    test_lock_refuses_reentry();
    test_open_detects_media_change();
    test_foreign_blockdev_is_refused();
    puts("astra ext4 port: PASS");
    return 0;
}

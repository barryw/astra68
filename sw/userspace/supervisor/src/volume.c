/*
 * Mounting a real ext4 volume from the device lease.
 *
 * This is the step that takes the filesystem off the host. Every other gate
 * mounts a volume held in RAM through the deterministic memory backend; here
 * lwext4 runs against the lease-backed device, which is the whole reason the
 * block facade was built with two interchangeable backends.
 *
 * The volume is found through the partition table rather than assumed to start
 * at sector 0. The card carries a FAT boot partition beside Astra's own, and
 * the port is given a window it may not address outside of, so a filesystem
 * defect cannot reach the partition that boots the machine.
 *
 * This is not a permanent home for filesystem policy. The supervisor verifies
 * what it was granted, and mounting the volume it was leased is the next thing
 * after proving a sector round-trip. The VFS service takes it over when it
 * exists.
 */

#include <volume.h>

#include <vfs_host.h>

#include <astra/alloc.h>
#include <astra/block.h>
#include <astra/bytes.h>
#include <astra/ext4_alloc.h>
#include <astra/ext4_port.h>
#include <astra/mbr.h>
#include <astra/runtime.h>
#include <astra/supervisor.h>

#include <ext4.h>

#define VOLUME_MOUNT_POINT "/vol/"
#define VOLUME_DEVICE_NAME "astra"
#define VOLUME_CHECK_PATH VOLUME_MOUNT_POINT "astra.chk"
#define VOLUME_CHECK_BYTES 4096u

/*
 * The arena the filesystem allocates from. The measured class table needs
 * 216,060 bytes; see sw/userspace/storage/src/ext4_alloc.c for why that number
 * is what it is, and why it is measured on LP32 rather than on a host.
 */
static AstraAllocScalar volume_arena[28000];
static AstraAllocator volume_allocator;

static AstraExt4Port volume_port;
/*
 * The window the check found. The check deliberately unmounts to prove the
 * volume comes back clean and leaves nothing allocated; a terminal needs it
 * mounted again afterwards, and it must be the same window.
 */
static AstraExt4Partition volume_window;
static AstraBlockDevice *volume_block;
static int volume_window_valid;
/*
 * Set while lwext4 holds the volume. It is what tells the caller its device and
 * lease are still spoken for: the port keeps a pointer to the device across a
 * mount, so releasing either while this is set leaves the filesystem addressing
 * memory nobody owns any more.
 */
static int volume_mounted;
static uint8_t volume_sector[ASTRA_BLOCK_SECTOR_BYTES];
static uint8_t volume_pattern[VOLUME_CHECK_BYTES];

static uint8_t
pattern_byte(uint32_t offset)
{
    return (uint8_t)((offset * 7u) + (offset >> 8) + 0x5au);
}

/*
 * Finds Astra's volume in the partition table. A card with no table, or with no
 * Linux partition on it, is not a failure — there is simply nothing to mount.
 */
static int
find_volume(AstraBlockDevice *block, AstraExt4Partition *window)
{
    AstraMbrTable table;
    const AstraMbrEntry *entry;

    if (astra_mbr_read(block, volume_sector, sizeof(volume_sector), &table,
                       0u) != ASTRA_BLOCK_OK) {
        return 0;
    }
    entry = astra_mbr_find(&table, ASTRA_MBR_LINUX);
    if (entry == NULL) {
        return 0;
    }
    window->first_sector = entry->first_sector;
    window->sector_count = entry->sector_count;
    return 1;
}

static uint32_t
write_and_verify(void)
{
    ext4_file file;
    size_t moved = 0u;
    uint32_t index;

    for (index = 0u; index < VOLUME_CHECK_BYTES; ++index) {
        volume_pattern[index] = pattern_byte(index);
    }

    if (ext4_fopen(&file, VOLUME_CHECK_PATH, "wb") != EOK) {
        return ASTRA_SUPERVISOR_FAIL_VOLUME;
    }
    if (ext4_fwrite(&file, volume_pattern, VOLUME_CHECK_BYTES, &moved) != EOK ||
        moved != VOLUME_CHECK_BYTES) {
        (void)ext4_fclose(&file);
        return ASTRA_SUPERVISOR_FAIL_VOLUME;
    }
    if (ext4_fclose(&file) != EOK) {
        return ASTRA_SUPERVISOR_FAIL_VOLUME;
    }

    /*
     * Reopened rather than rewound, so the read goes back through the inode and
     * the block cache instead of returning what is still in hand.
     */
    (void)memset(volume_pattern, 0, sizeof(volume_pattern));
    if (ext4_fopen(&file, VOLUME_CHECK_PATH, "rb") != EOK) {
        return ASTRA_SUPERVISOR_FAIL_VOLUME;
    }
    if (ext4_fsize(&file) != (uint64_t)VOLUME_CHECK_BYTES) {
        (void)ext4_fclose(&file);
        return ASTRA_SUPERVISOR_FAIL_VOLUME;
    }
    if (ext4_fread(&file, volume_pattern, VOLUME_CHECK_BYTES, &moved) != EOK ||
        moved != VOLUME_CHECK_BYTES) {
        (void)ext4_fclose(&file);
        return ASTRA_SUPERVISOR_FAIL_VOLUME;
    }
    (void)ext4_fclose(&file);

    for (index = 0u; index < VOLUME_CHECK_BYTES; ++index) {
        if (volume_pattern[index] != pattern_byte(index)) {
            return ASTRA_SUPERVISOR_FAIL_VOLUME;
        }
    }

    /* Leave the volume as it was found. */
    if (ext4_fremove(VOLUME_CHECK_PATH) != EOK) {
        return ASTRA_SUPERVISOR_FAIL_VOLUME;
    }
    return 0u;
}

int
supervisor_volume_is_mounted(void)
{
    return volume_mounted;
}

uint32_t
supervisor_verify_volume(AstraBlockDevice *block, int keep_mounted)
{
    AstraExt4Partition window;
    uint32_t failure;
    int rc;

    if (block == NULL) {
        return ASTRA_SUPERVISOR_FAIL_VOLUME;
    }
    if (!find_volume(block, &window)) {
        return 0u;
    }
    volume_window = window;
    volume_block = block;
    volume_window_valid = 1;
    (void)astra_progress(ASTRA_SUPERVISOR_STAGE_VOLUME_FOUND);

    if (astra_alloc_init(&volume_allocator, astra_ext4_alloc_classes,
                         ASTRA_EXT4_ALLOC_CLASS_COUNT, volume_arena,
                         sizeof(volume_arena)) != ASTRA_ALLOC_OK) {
        return ASTRA_SUPERVISOR_FAIL_VOLUME;
    }
    astra_ext4_alloc_bind(&volume_allocator);

    if (astra_ext4_port_init(&volume_port, block, &window, volume_sector,
                             sizeof(volume_sector), 0u) != ASTRA_EXT4_OK) {
        return ASTRA_SUPERVISOR_FAIL_VOLUME;
    }
    if (ext4_device_register(astra_ext4_port_blockdev(&volume_port),
                             VOLUME_DEVICE_NAME) != EOK) {
        return ASTRA_SUPERVISOR_FAIL_VOLUME;
    }
    if (ext4_mount(VOLUME_DEVICE_NAME, VOLUME_MOUNT_POINT, false) != EOK) {
        (void)ext4_device_unregister(VOLUME_DEVICE_NAME);
        return ASTRA_SUPERVISOR_FAIL_VOLUME;
    }
    /*
     * Recovery runs before anything is written. A volume left dirty by a power
     * cut is exactly the case this has to survive, and ENOTSUP only means the
     * volume carries no journal.
     */
    rc = ext4_recover(VOLUME_MOUNT_POINT);
    if (rc != EOK && rc != ENOTSUP) {
        (void)ext4_umount(VOLUME_MOUNT_POINT);
        (void)ext4_device_unregister(VOLUME_DEVICE_NAME);
        return ASTRA_SUPERVISOR_FAIL_VOLUME;
    }
    if (ext4_journal_start(VOLUME_MOUNT_POINT) != EOK) {
        (void)ext4_umount(VOLUME_MOUNT_POINT);
        (void)ext4_device_unregister(VOLUME_DEVICE_NAME);
        return ASTRA_SUPERVISOR_FAIL_VOLUME;
    }
    volume_mounted = 1;
    (void)astra_progress(ASTRA_SUPERVISOR_STAGE_VOLUME_MOUNTED);

    failure = write_and_verify();

    /*
     * A terminal needs the volume it is about to use, and lwext4 does not
     * take kindly to the same device being mounted a second time. Leaving it
     * mounted is also what a real boot does; the price is that the checks
     * below, which only mean anything once everything is released, are not
     * run in that case.
     */
    if (keep_mounted && failure == 0u) {
        /*
         * The volume stays mounted for the storage service, not for the
         * terminal: nothing above this line touches lwext4 any more, and the
         * terminal reaches the volume through the protocol like any other
         * client would.
         */
        if (!supervisor_vfs_start(VOLUME_MOUNT_POINT)) {
            return ASTRA_SUPERVISOR_FAIL_VOLUME;
        }
        (void)astra_progress(ASTRA_SUPERVISOR_STAGE_VOLUME_VERIFIED);
        return 0u;
    }

    (void)ext4_journal_stop(VOLUME_MOUNT_POINT);
    volume_mounted = 0;
    if (ext4_umount(VOLUME_MOUNT_POINT) != EOK) {
        failure = ASTRA_SUPERVISOR_FAIL_VOLUME;
    }
    (void)ext4_device_unregister(VOLUME_DEVICE_NAME);

    /*
     * An unmounted volume must leave nothing allocated, the arena must not have
     * been over-run, and the port must never have addressed outside its window.
     * Any of those is a defect worth failing the boot for.
     */
    if (astra_alloc_metrics(&volume_allocator)->live_blocks != 0u ||
        astra_alloc_metrics(&volume_allocator)->failures != 0u ||
        astra_alloc_valid(&volume_allocator) == 0 ||
        volume_port.out_of_partition_refusals != 0u) {
        failure = ASTRA_SUPERVISOR_FAIL_VOLUME;
    }
    if (failure == 0u) {
        (void)astra_progress(ASTRA_SUPERVISOR_STAGE_VOLUME_VERIFIED);
    }
    return failure;
}

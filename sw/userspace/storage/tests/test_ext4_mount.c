/*
 * lwext4 driven through the Astra port and the bounded allocator.
 *
 * The volume lives in the memory backend, so this is the whole filesystem
 * stack running against the same AstraBlockDevice facade the lease-backed
 * device presents. What it proves is that the port is complete: lwext4 mounts,
 * journals, writes, renames, unlinks and re-reads without ever seeing a libc
 * heap or a transfer longer than the device permits.
 *
 * The device is deliberately given a small max_transfer_sectors so that every
 * 4 KiB filesystem block crosses the port's splitting path. A cap that no real
 * request exceeded would leave that code untested here and discovered on
 * hardware.
 *
 * The image is written back on exit so e2fsck — which shares no code with
 * lwext4 — can judge the result.
 */

#define _POSIX_C_SOURCE 200809L

#include <stdint.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <astra/alloc.h>
#include <astra/block_device.h>
#include <astra/ext4_alloc.h>
#include <astra/ext4_port.h>
#include <astra/ext4_time.h>
#include <astra/mbr.h>
#include <astra/memory_block.h>

#include <ext4.h>
#include <ext4_debug.h>

#include "file_block.h"

#define MOUNT_POINT "/volume/"
#define SECTOR_SIZE 512u
#define MAX_TRANSFER_SECTORS 4u
#define BIG_FILE_BYTES (192u * 1024u)
#define MANY_FILES 200u

static uint8_t *storage;
static uint8_t *durable_storage;
static size_t storage_bytes;
static uint8_t sector_buffer[SECTOR_SIZE];

static uint32_t
host_clock_seconds(void)
{
    return (uint32_t)time(NULL);
}

/*
 * Partitioned mode wraps the plain ext4 image in the layout a real card
 * carries: a partition table, a boot partition standing in for FAT, and the
 * volume after it. The stand-in is filled with a known pattern rather than a
 * real FAT filesystem, because what is being checked is that not one byte of
 * it moves — and a pattern detects that more sharply than a filesystem would.
 *
 * This is the layout that makes the mount meaningful. Every other gate mounts
 * a whole-disk image, where a partition-offset bug cannot show itself.
 */
#define BOOT_FIRST_SECTOR 2048u
#define BOOT_SECTOR_COUNT 8192u /* 4 MiB standing in for the FAT volume */
#define BOOT_PATTERN_SEED 0x1cu

static const AstraExt4Partition *partition;
static AstraExt4Partition partition_window;
static int partitioned;
static uint64_t volume_first_sector;

static uint8_t
boot_pattern(size_t offset)
{
    return (uint8_t)((offset * 13u) + (offset >> 9) + BOOT_PATTERN_SEED);
}

static AstraMemoryBlock memory;
static AstraFileBlock file_backing;
static AstraBlockDevice device;
static AstraExt4Port port;

typedef struct ControlledBlock {
    pthread_mutex_t mutex;
    pthread_cond_t changed;
    uint64_t reads;
    int hold_next_read;
    int read_entered;
    int release_read;
} ControlledBlock;

static ControlledBlock controlled = {
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .changed = PTHREAD_COND_INITIALIZER,
};

static AstraBlockStatus
controlled_query(void *context, AstraBlockGeometry *geometry)
{
    (void)context;
    return astra_memory_block_backend.query(&memory, geometry);
}

static AstraBlockStatus
controlled_read(void *context, uint64_t lba, uint32_t count, void *buffer,
                uint64_t deadline)
{
    (void)context;
    if (pthread_mutex_lock(&controlled.mutex) != 0)
        abort();
    ++controlled.reads;
    if (controlled.hold_next_read) {
        controlled.hold_next_read = 0;
        controlled.read_entered = 1;
        (void)pthread_cond_broadcast(&controlled.changed);
        while (!controlled.release_read)
            (void)pthread_cond_wait(&controlled.changed, &controlled.mutex);
    }
    if (pthread_mutex_unlock(&controlled.mutex) != 0)
        abort();
    return astra_memory_block_backend.read(&memory, lba, count, buffer,
                                           deadline);
}

static AstraBlockStatus
controlled_write(void *context, uint64_t lba, uint32_t count,
                 const void *buffer, uint64_t deadline)
{
    (void)context;
    return astra_memory_block_backend.write(&memory, lba, count, buffer,
                                            deadline);
}

static AstraBlockStatus
controlled_flush(void *context, uint64_t deadline)
{
    AstraBlockStatus status;

    (void)context;
    status = astra_memory_block_backend.flush(&memory, deadline);
    if (status == ASTRA_BLOCK_OK && durable_storage != NULL) {
        memcpy(durable_storage, storage, storage_bytes);
    }
    return status;
}

static const AstraBlockBackend controlled_backend = {
    .query = controlled_query,
    .read = controlled_read,
    .write = controlled_write,
    .flush = controlled_flush,
};

static pthread_rwlock_t mount_lock = PTHREAD_RWLOCK_INITIALIZER;
static pthread_mutex_t cache_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t fill_lock = PTHREAD_MUTEX_INITIALIZER;

static void
host_mount_lock(void)
{
    if (pthread_rwlock_wrlock(&mount_lock) != 0)
        abort();
}

static void
host_mount_unlock(void)
{
    if (pthread_rwlock_unlock(&mount_lock) != 0)
        abort();
}

static void
host_read_lock(void)
{
    if (pthread_rwlock_rdlock(&mount_lock) != 0)
        abort();
}

static void
host_read_unlock(void)
{
    if (pthread_rwlock_unlock(&mount_lock) != 0)
        abort();
}

static void
host_cache_lock(void)
{
    if (pthread_mutex_lock(&cache_lock) != 0)
        abort();
}

static void
host_cache_unlock(void)
{
    if (pthread_mutex_unlock(&cache_lock) != 0)
        abort();
}

static void
host_fill_lock(void)
{
    if (pthread_mutex_lock(&fill_lock) != 0)
        abort();
}

static void
host_fill_unlock(void)
{
    if (pthread_mutex_unlock(&fill_lock) != 0)
        abort();
}

static const struct ext4_lock host_mount_locks = {
    .lock = host_mount_lock,
    .unlock = host_mount_unlock,
    .read_lock = host_read_lock,
    .read_unlock = host_read_unlock,
    .cache_lock = host_cache_lock,
    .cache_unlock = host_cache_unlock,
    .fill_lock = host_fill_lock,
    .fill_unlock = host_fill_unlock,
};

/*
 * "on-file" mode keeps the volume on disk instead of in RAM. It exists for one
 * question the RAM-backed gates cannot answer: how lwext4's memory demand
 * scales with volume size. A 32-bit process cannot hold a 200 GB volume, and
 * on LP32 a size_t byte offset cannot even address one.
 *
 * It gives up what the RAM path provides — byte-level inspection of regions
 * outside the volume — so it is a measurement mode, not a replacement gate.
 */
static int on_file;

static AstraAllocator allocator;

/*
 * The shipped class table is measured on LP32, which is what runs on hardware.
 * An LP64 host is a different workload against the same code: every lwext4
 * structure holding a pointer grows, so the 33..64-byte descriptors that
 * dominate the target's demand spill into the next class up, and the htree
 * sort array grows from 4,092 bytes to 5,456 and no longer fits a 4 KiB block
 * at all.
 *
 * So the host run uses a host-shaped table rather than widening the target's
 * budget to accommodate a machine Astra does not ship on. Both tables are
 * measured; neither is a guess. The target table stays the one thing a service
 * copies.
 */
#define HOST_CLASS_COUNT 6u

static const AstraAllocClass host_classes[HOST_CLASS_COUNT] = {
    {64u, 16u},    /* nothing lands here on LP64; kept so the shape matches */
    {128u, 900u},  /* measured peak_live=838 */
    {256u, CONFIG_BLOCK_DEV_CACHE_SIZE + 8u}, /* LP64 ext4_buf is 144 B */
    {2048u, 40u},  /* measured peak_live=29 */
    {4096u, CONFIG_BLOCK_DEV_CACHE_SIZE + 8u},
    {8192u, 2u},   /* the htree sort array, 5,456 bytes on LP64 */
};

static const AstraAllocClass *classes;
static uint32_t class_count;

/* Sized for whichever table is larger; the unused tail costs nothing to run. */
static AstraAllocScalar arena[ASTRA_EXT4_ARENA_BYTES /
                              sizeof(AstraAllocScalar)];

static int failures;

static uint64_t
nanoseconds(void *context)
{
    struct timespec now;

    (void)context;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return 0u;
    }
    return (uint64_t)now.tv_sec * 1000000000u + (uint64_t)now.tv_nsec;
}

static int
fail(const char *what, int rc)
{
    printf("FAIL %s rc=%d\n", what, rc);
    ++failures;
    return 1;
}

static uint8_t
pattern_byte(unsigned file_index, unsigned offset)
{
    return (uint8_t)((file_index * 31u) + (offset * 7u) + (offset >> 8));
}

static int
load_image(const char *path)
{
    FILE *image = fopen(path, "rb");
    long size;

    if (image == NULL) {
        return fail("fopen image", 0);
    }
    if (fseek(image, 0, SEEK_END) != 0 || (size = ftell(image)) <= 0 ||
        fseek(image, 0, SEEK_SET) != 0) {
        fclose(image);
        return fail("size image", 0);
    }
    storage_bytes = (size_t)size;
    storage = malloc(storage_bytes);
    if (storage == NULL) {
        fclose(image);
        return fail("malloc image", 0);
    }
    if (fread(storage, 1u, storage_bytes, image) != storage_bytes) {
        fclose(image);
        return fail("fread image", 0);
    }
    fclose(image);
    return 0;
}

/*
 * Writes back only the volume, not the container. e2fsck is handed the same
 * bare filesystem image it was given, so the gate does not depend on e2fsck
 * understanding Astra's partition layout.
 */
static int
store_bytes(const char *path, const uint8_t *from, size_t bytes)
{
    FILE *image = fopen(path, "wb");

    if (image == NULL) {
        return fail("fopen image for write", 0);
    }
    if (fwrite(from, 1u, bytes, image) != bytes) {
        fclose(image);
        return fail("fwrite image", 0);
    }
    if (fclose(image) != 0) {
        return fail("fclose image", 0);
    }
    return 0;
}

static int
store_image(const char *path)
{
    const uint8_t *from = storage;
    size_t bytes = storage_bytes;

    if (partitioned) {
        from = storage + (size_t)volume_first_sector * SECTOR_SIZE;
        bytes = storage_bytes - (size_t)volume_first_sector * SECTOR_SIZE;
    }
    return store_bytes(path, from, bytes);
}

/*
 * Rebuilds the loaded image as a partitioned card: the ext4 volume moves to
 * sit after a boot partition, and a partition table is written in front of
 * both.
 */
static int
build_partitioned_layout(void)
{
    size_t volume_bytes = storage_bytes;
    size_t volume_sectors = volume_bytes / SECTOR_SIZE;
    size_t total_sectors;
    uint8_t *grown;
    uint8_t *entry;
    size_t index;

    volume_first_sector = BOOT_FIRST_SECTOR + BOOT_SECTOR_COUNT;
    total_sectors = (size_t)volume_first_sector + volume_sectors;

    grown = malloc(total_sectors * SECTOR_SIZE);
    if (grown == NULL) {
        return fail("malloc partitioned image", 0);
    }
    memset(grown, 0, (size_t)volume_first_sector * SECTOR_SIZE);
    memcpy(grown + (size_t)volume_first_sector * SECTOR_SIZE, storage,
           volume_bytes);
    free(storage);
    storage = grown;
    storage_bytes = total_sectors * SECTOR_SIZE;

    /* The boot partition's contents: a pattern that must survive untouched. */
    for (index = 0u; index < (size_t)BOOT_SECTOR_COUNT * SECTOR_SIZE;
         ++index) {
        storage[(size_t)BOOT_FIRST_SECTOR * SECTOR_SIZE + index] =
            boot_pattern(index);
    }

    /* A partition table stage0 would accept: FAT32 LBA first, then ours. */
    storage[510] = 0x55u;
    storage[511] = 0xaau;
    entry = &storage[446u];
    entry[0] = 0x80u;
    entry[4] = 0x0cu;
    entry[8] = (uint8_t)(BOOT_FIRST_SECTOR & 0xffu);
    entry[9] = (uint8_t)((BOOT_FIRST_SECTOR >> 8) & 0xffu);
    entry[12] = (uint8_t)(BOOT_SECTOR_COUNT & 0xffu);
    entry[13] = (uint8_t)((BOOT_SECTOR_COUNT >> 8) & 0xffu);

    entry = &storage[446u + 16u];
    entry[4] = 0x83u;
    entry[8] = (uint8_t)(volume_first_sector & 0xffu);
    entry[9] = (uint8_t)((volume_first_sector >> 8) & 0xffu);
    entry[10] = (uint8_t)((volume_first_sector >> 16) & 0xffu);
    entry[12] = (uint8_t)(volume_sectors & 0xffu);
    entry[13] = (uint8_t)((volume_sectors >> 8) & 0xffu);
    entry[14] = (uint8_t)((volume_sectors >> 16) & 0xffu);

    partition_window.first_sector = volume_first_sector;
    partition_window.sector_count = volume_sectors;
    partition = &partition_window;
    return 0;
}

/*
 * The partition table is re-read through the same reader a service would use,
 * so the window the volume is mounted with is the one on the disk rather than
 * the one this test happens to remember.
 */
static int
check_layout_via_reader(void)
{
    AstraMbrTable table;
    const AstraMbrEntry *boot;
    const AstraMbrEntry *volume;

    if (astra_mbr_read(&device, sector_buffer, sizeof(sector_buffer), &table,
                       0u) != ASTRA_BLOCK_OK) {
        return fail("astra_mbr_read", 0);
    }
    boot = astra_mbr_find(&table, ASTRA_MBR_FAT);
    volume = astra_mbr_find(&table, ASTRA_MBR_LINUX);
    if (boot == NULL || volume == NULL) {
        return fail("partition table missing an entry", 0);
    }
    if (boot->first_sector != BOOT_FIRST_SECTOR ||
        boot->sector_count != BOOT_SECTOR_COUNT) {
        return fail("boot partition geometry", 0);
    }
    if (volume->first_sector != volume_first_sector) {
        return fail("volume partition geometry", 0);
    }
    if (!astra_mbr_range_conflicts(&table, ASTRA_MBR_ENTRY_COUNT,
                                   BOOT_FIRST_SECTOR, 1u)) {
        return fail("boot partition reported as free space", 0);
    }
    return 0;
}

/* Not one byte of the boot partition or the partition table may have moved. */
static int
check_boot_region_intact(void)
{
    size_t index;

    for (index = 0u; index < (size_t)BOOT_SECTOR_COUNT * SECTOR_SIZE;
         ++index) {
        size_t at = (size_t)BOOT_FIRST_SECTOR * SECTOR_SIZE + index;

        if (storage[at] != boot_pattern(index)) {
            printf("FAIL boot partition modified at byte %lu\n",
                   (unsigned long)index);
            ++failures;
            return 1;
        }
    }
    if (storage[510] != 0x55u || storage[511] != 0xaau) {
        return fail("partition table signature destroyed", 0);
    }
    for (index = 0u; index < 16u; ++index) {
        if (storage[446u + index] == 0u && index == 4u) {
            return fail("boot partition entry cleared", 0);
        }
    }
    return 0;
}

static int
bring_up(void)
{
    AstraBlockGeometry geometry;
    AstraAllocStatus alloc_status;
    AstraExt4Status port_status;

    if (sizeof(void *) == 4u) {
        classes = astra_ext4_alloc_classes;
        class_count = ASTRA_EXT4_ALLOC_CLASS_COUNT;
    } else {
        classes = host_classes;
        class_count = HOST_CLASS_COUNT;
    }

    alloc_status = astra_alloc_init(&allocator, classes, class_count, arena,
                                    sizeof(arena));
    if (alloc_status != ASTRA_ALLOC_OK) {
        printf("FAIL astra_alloc_init status=%d need=%lu have=%lu\n",
               (int)alloc_status,
               (unsigned long)astra_alloc_arena_bytes(classes, class_count),
               (unsigned long)sizeof(arena));
        ++failures;
        return 1;
    }
    astra_ext4_alloc_bind(&allocator);
    /*
     * The host's clock, so the timestamps this test writes are real ones and
     * the checks below can say what a plausible date looks like.
     */
    astra_ext4_clock_bind(host_clock_seconds);

    if (on_file) {
        astra_block_device_init(&device, &astra_file_block_backend,
                                &file_backing, nanoseconds, NULL);
    } else {
        astra_memory_block_init(&memory, storage, storage_bytes, SECTOR_SIZE,
                                MAX_TRANSFER_SECTORS,
                                ASTRA_BLOCK_FLAG_PRESENT);
        astra_block_device_init(&device, &controlled_backend, &controlled,
                                nanoseconds, NULL);
    }
    if (astra_block_query(&device, &geometry) != ASTRA_BLOCK_OK) {
        return fail("astra_block_query", 0);
    }
    if (on_file) {
        printf("volume: %llu sectors of %u bytes (%llu MiB)\n",
               (unsigned long long)geometry.sector_count,
               (unsigned)geometry.sector_size,
               (unsigned long long)(geometry.sector_count /
                                    (1048576u / geometry.sector_size)));
    }

    port_status = astra_ext4_port_init(&port, &device, partition, sector_buffer,
                                       sizeof(sector_buffer), 0u);
    if (port_status != ASTRA_EXT4_OK) {
        return fail("astra_ext4_port_init", (int)port_status);
    }
    return 0;
}

static int
do_mount(void)
{
    int rc = ext4_device_register(astra_ext4_port_blockdev(&port), "astra");

    if (rc != EOK) {
        return fail("ext4_device_register", rc);
    }
    rc = ext4_mount("astra", MOUNT_POINT, false);
    if (rc != EOK) {
        return fail("ext4_mount", rc);
    }
    rc = ext4_mount_setup_locks(MOUNT_POINT, &host_mount_locks);
    if (rc != EOK) {
        return fail("ext4_mount_setup_locks", rc);
    }
    rc = ext4_recover(MOUNT_POINT);
    if (rc != EOK && rc != ENOTSUP) {
        return fail("ext4_recover", rc);
    }
    rc = ext4_journal_start(MOUNT_POINT);
    if (rc != EOK) {
        return fail("ext4_journal_start", rc);
    }
    ext4_cache_write_back(MOUNT_POINT, 1);
    return 0;
}

static int
do_umount(void)
{
    int rc;

    ext4_cache_write_back(MOUNT_POINT, 0);
    rc = ext4_journal_stop(MOUNT_POINT);
    if (rc != EOK) {
        return fail("ext4_journal_stop", rc);
    }
    rc = ext4_umount(MOUNT_POINT);
    if (rc != EOK) {
        return fail("ext4_umount", rc);
    }
    rc = ext4_device_unregister("astra");
    if (rc != EOK) {
        return fail("ext4_device_unregister", rc);
    }
    return 0;
}

static int
write_file(const char *path, unsigned index, unsigned bytes)
{
    ext4_file file;
    uint8_t chunk[4096];
    size_t written = 0u;
    unsigned offset = 0u;
    int rc = ext4_fopen(&file, path, "wb");

    if (rc != EOK) {
        return fail("ext4_fopen(w)", rc);
    }
    while (offset < bytes) {
        unsigned span = bytes - offset;
        unsigned index_in_chunk;

        if (span > sizeof(chunk)) {
            span = sizeof(chunk);
        }
        for (index_in_chunk = 0u; index_in_chunk < span; ++index_in_chunk) {
            chunk[index_in_chunk] = pattern_byte(index, offset + index_in_chunk);
        }
        rc = ext4_fwrite(&file, chunk, span, &written);
        if (rc != EOK || written != span) {
            ext4_fclose(&file);
            return fail("ext4_fwrite", rc);
        }
        offset += span;
    }
    rc = ext4_fclose(&file);
    if (rc != EOK) {
        return fail("ext4_fclose(w)", rc);
    }
    return 0;
}

/*
 * A full volume must say so.
 *
 * This is the only condition that proves the write path reports failure rather
 * than absorbing it, and it is not reachable from the other modes: every one of
 * them writes far less than the volume holds.
 *
 * lwext4 as imported failed it. ext4_fwrite ended with
 * `r = ext4_fs_put_inode_ref(&ref)`, which overwrote whatever error the write
 * had produced, so an ENOSPC out of ext4_fs_append_inode_dblk came back as EOK
 * with zero bytes moved -- and took the commit branch rather than the abort
 * one, journalling a transaction whose write had failed. A device EIO
 * mid-write returned EOK by the same path. See astra patch 0004.
 *
 * So the assertion that matters is not "a write eventually fails" but "no write
 * ever claims EOK while moving less than it was given". The first is what a
 * correct implementation does; the second is what the broken one cannot do.
 */
#define FILL_CHUNK 4096u
/* Larger than any volume this test is run against, so a pass cannot be a hang. */
#define FILL_CHUNK_LIMIT 262144u

static int
check_full_volume_reports_enospc(void)
{
    static uint8_t chunk[FILL_CHUNK];
    ext4_file file;
    unsigned written_chunks;
    int rc;

    (void)memset(chunk, 0x5a, sizeof(chunk));
    rc = ext4_fopen(&file, MOUNT_POINT "fill.bin", "wb");
    if (rc != EOK) {
        return fail("ext4_fopen(fill)", rc);
    }
    for (written_chunks = 0u; written_chunks < FILL_CHUNK_LIMIT;
         ++written_chunks) {
        size_t moved = 0u;

        rc = ext4_fwrite(&file, chunk, sizeof(chunk), &moved);
        if (rc != EOK) {
            break; /* The volume filled and the write path said so. */
        }
        if (moved != sizeof(chunk)) {
            (void)ext4_fclose(&file);
            printf("FAIL ext4_fwrite returned EOK having moved %lu of %u "
                   "after %u chunks\n",
                   (unsigned long)moved, (unsigned)sizeof(chunk),
                   written_chunks);
            ++failures;
            return 1;
        }
    }
    (void)ext4_fclose(&file);
    if (written_chunks == FILL_CHUNK_LIMIT) {
        return fail("volume never filled; raise FILL_CHUNK_LIMIT", 0);
    }
    printf("full volume: %u chunks written, then rc=%d\n", written_chunks, rc);
    if (ext4_fremove(MOUNT_POINT "fill.bin") != EOK) {
        return fail("ext4_fremove(fill)", 0);
    }
    return 0;
}

static int
read_verify(const char *path, unsigned index, unsigned bytes)
{
    ext4_file file;
    static uint8_t chunk[4096];
    size_t got = 0u;
    unsigned offset = 0u;
    int rc = ext4_fopen(&file, path, "rb");

    if (rc != EOK) {
        return fail("ext4_fopen(r)", rc);
    }
    if (ext4_fsize(&file) != (uint64_t)bytes) {
        printf("FAIL size %s got=%llu want=%u\n", path,
               (unsigned long long)ext4_fsize(&file), bytes);
        ext4_fclose(&file);
        ++failures;
        return 1;
    }
    while (offset < bytes) {
        unsigned span = bytes - offset;
        unsigned index_in_chunk;

        if (span > sizeof(chunk)) {
            span = sizeof(chunk);
        }
        rc = ext4_fread(&file, chunk, span, &got);
        if (rc != EOK || got != span) {
            ext4_fclose(&file);
            return fail("ext4_fread", rc);
        }
        for (index_in_chunk = 0u; index_in_chunk < span; ++index_in_chunk) {
            if (chunk[index_in_chunk] !=
                pattern_byte(index, offset + index_in_chunk)) {
                printf("FAIL content %s at %u\n", path,
                       offset + index_in_chunk);
                ext4_fclose(&file);
                ++failures;
                return 1;
            }
        }
        offset += span;
    }
    rc = ext4_fclose(&file);
    if (rc != EOK) {
        return fail("ext4_fclose(r)", rc);
    }
    return 0;
}

static uint64_t
controlled_read_count(void)
{
    uint64_t reads;

    if (pthread_mutex_lock(&controlled.mutex) != 0)
        abort();
    reads = controlled.reads;
    if (pthread_mutex_unlock(&controlled.mutex) != 0)
        abort();
    return reads;
}

static int
check_coalesced_read_cache(void)
{
    enum { RUN_BYTES = 3u * 4096u };
    static uint8_t bytes[RUN_BYTES];
    ext4_file file;
    uint64_t before;
    uint64_t after_first;
    size_t moved = 0u;
    int rc;

    if (do_umount() || do_mount())
        return 1;
    rc = ext4_fopen(&file, MOUNT_POINT "dir/nested/big.bin", "rb");
    if (rc != EOK)
        return fail("coalesced cache open", rc);
    before = controlled_read_count();
    rc = ext4_fread(&file, bytes, sizeof(bytes), &moved);
    after_first = controlled_read_count();
    if (rc != EOK || moved != sizeof(bytes) || after_first == before)
        return fail("coalesced cache cold read", rc);
    for (uint32_t at = 0u; at < sizeof(bytes); ++at) {
        if (bytes[at] != pattern_byte(2u, at))
            return fail("coalesced cache cold bytes", 0);
    }
    if (ext4_fseek(&file, 0, SEEK_SET) != EOK)
        return fail("coalesced cache seek", 0);
    before = controlled_read_count();
    moved = 0u;
    rc = ext4_fread(&file, bytes, sizeof(bytes), &moved);
    if (rc != EOK || moved != sizeof(bytes) ||
        controlled_read_count() != before)
        return fail("coalesced cache warm read issued I/O", rc);
    (void)ext4_fclose(&file);
    puts("coalesced cache: warm contiguous read issued no I/O");
    return 0;
}

static void
many_path(char *out, size_t capacity, unsigned index)
{
    snprintf(out, capacity, MOUNT_POINT "many/entry_%03u.dat", index);
}

static int
populate(void)
{
    char path[64];
    ext4_file exclusive;
    unsigned index;
    int rc;

    rc = ext4_dir_mk(MOUNT_POINT "dir");
    if (rc != EOK) {
        return fail("ext4_dir_mk dir", rc);
    }
    rc = ext4_dir_mk(MOUNT_POINT "dir/nested");
    if (rc != EOK) {
        return fail("ext4_dir_mk nested", rc);
    }
    rc = ext4_dir_mk(MOUNT_POINT "many");
    if (rc != EOK) {
        return fail("ext4_dir_mk many", rc);
    }
    rc = ext4_dir_mk(MOUNT_POINT "remove-me");
    if (rc != EOK) {
        return fail("ext4_dir_mk remove-me", rc);
    }
    rc = ext4_fremove(MOUNT_POINT "remove-me");
    if (rc != EISDIR) {
        return fail("ext4_fremove directory", rc);
    }
    rc = ext4_dir_rm(MOUNT_POINT "remove-me");
    if (rc != EOK) {
        return fail("ext4_dir_rm remove-me", rc);
    }
    rc = ext4_dir_mk_mode(MOUNT_POINT "mode-dir", 0710u);
    if (rc != EOK)
        return fail("ext4_dir_mk_mode", rc);
    rc = ext4_fopen2_mode(&exclusive, MOUNT_POINT "mode-file",
                          O_WRONLY | O_CREAT | O_EXCL, 0600u);
    if (rc != EOK)
        return fail("ext4_fopen2_mode", rc);
    rc = ext4_fclose(&exclusive);
    if (rc != EOK)
        return fail("ext4_fclose(mode-file)", rc);

    if (write_file(MOUNT_POINT "hello.txt", 1u, 37u)) {
        return 1;
    }
    rc = ext4_fopen2(&exclusive, MOUNT_POINT "exclusive.tmp",
                     O_WRONLY | O_CREAT | O_EXCL);
    if (rc != EOK)
        return fail("ext4_fopen2(exclusive create)", rc);
    rc = ext4_fclose(&exclusive);
    if (rc != EOK)
        return fail("ext4_fclose(exclusive)", rc);
    rc = ext4_fopen2(&exclusive, MOUNT_POINT "exclusive.tmp",
                     O_WRONLY | O_CREAT | O_EXCL);
    if (rc != EEXIST)
        return fail("ext4_fopen2(exclusive existing)", rc);
    rc = ext4_fremove(MOUNT_POINT "exclusive.tmp");
    if (rc != EOK)
        return fail("ext4_fremove(exclusive)", rc);
    if (write_file(MOUNT_POINT "dir/nested/big.bin", 2u, BIG_FILE_BYTES)) {
        return 1;
    }
    /* Enough entries in one directory to force an htree index. */
    for (index = 0u; index < MANY_FILES; ++index) {
        many_path(path, sizeof(path), index);
        if (write_file(path, 100u + index, 64u + (index % 97u))) {
            return 1;
        }
    }

    rc = ext4_frename(MOUNT_POINT "hello.txt", MOUNT_POINT "dir/renamed.txt");
    if (rc != EOK) {
        return fail("ext4_frename", rc);
    }
    rc = ext4_fremove(MOUNT_POINT "many/entry_007.dat");
    if (rc != EOK) {
        return fail("ext4_fremove", rc);
    }

    /*
     * Names differing only in case are distinct objects. The frozen profile
     * states ^casefold, and the VFS above must honour the same byte-exact
     * rule; proving it here keeps that from being an assumption.
     */
    if (write_file(MOUNT_POINT "dir/Case.dat", 3u, 64u)) {
        return 1;
    }
    if (write_file(MOUNT_POINT "dir/case.dat", 4u, 128u)) {
        return 1;
    }
    if (write_file(MOUNT_POINT "dir/CASE.DAT", 5u, 192u)) {
        return 1;
    }
    return 0;
}

static int
verify(void)
{
    char path[64];
    ext4_dir dir;
    const ext4_direntry *entry;
    unsigned counted = 0u;
    unsigned metadata_checked = 0u;
    unsigned index;
    ext4_file probe;
    int rc;

    {
        uint32_t mode = 0u;

        rc = ext4_mode_get(MOUNT_POINT "mode-dir", &mode);
        if (rc != EOK || (mode & 07777u) != 0710u)
            return fail("creation mode directory", rc);
        rc = ext4_mode_get(MOUNT_POINT "mode-file", &mode);
        if (rc != EOK || (mode & 07777u) != 0600u)
            return fail("creation mode file", rc);
    }

    if (read_verify(MOUNT_POINT "dir/renamed.txt", 1u, 37u)) {
        return 1;
    }
    if (read_verify(MOUNT_POINT "dir/nested/big.bin", 2u, BIG_FILE_BYTES)) {
        return 1;
    }
    for (index = 0u; index < MANY_FILES; ++index) {
        if (index == 7u) {
            continue;
        }
        many_path(path, sizeof(path), index);
        if (read_verify(path, 100u + index, 64u + (index % 97u))) {
            return 1;
        }
    }
    if (read_verify(MOUNT_POINT "dir/Case.dat", 3u, 64u)) {
        return 1;
    }
    if (read_verify(MOUNT_POINT "dir/case.dat", 4u, 128u)) {
        return 1;
    }
    if (read_verify(MOUNT_POINT "dir/CASE.DAT", 5u, 192u)) {
        return 1;
    }

    rc = ext4_fopen(&probe, MOUNT_POINT "dir/cAsE.dat", "rb");
    if (rc == EOK) {
        ext4_fclose(&probe);
        printf("FAIL case folding: cAsE.dat resolved\n");
        ++failures;
        return 1;
    }
    if (rc != ENOENT) {
        return fail("ext4_fopen(case probe)", rc);
    }

    /*
     * `.` and `..` resolve by name, in a directory this library indexed.
     *
     * `many` holds enough names to carry an htree, and every directory lwext4
     * creates is indexed from the start. The dot entries are not in the hash
     * tree -- they are the first two entries of block 0, which is the index
     * root -- so a lookup that only asked the tree answered ENOENT for both,
     * and a caller resolving one path component at a time could not name the
     * directory it was standing in. `ls -l` showed `d?????????` for `.` and
     * `..` on every directory the machine itself had made, and only on those.
     */
    for (index = 0u; index < 2u; ++index) {
        uint32_t mode = 0u;
        const char *dot = index == 0u ? MOUNT_POINT "many/." :
                                        MOUNT_POINT "many/..";

        rc = ext4_mode_get(dot, &mode);
        if (rc != EOK) {
            return fail(dot, rc);
        }
        if ((mode & EXT4_INODE_MODE_TYPE_MASK) !=
            EXT4_INODE_MODE_DIRECTORY) {
            printf("FAIL %s is mode 0%o, not a directory\n", dot,
                   (unsigned)mode);
            ++failures;
            return 1;
        }
    }

    /*
     * The timestamps are real ones.
     *
     * lwext4 stamped nothing until the port gave it a clock: every inode it
     * created carried zero, so `ls -l` on a machine-written file showed no
     * date at all and there was no way to tell a file written this minute
     * from one written last year. The window is wide because this test can be
     * slow under a sanitizer, and narrow enough that a zero, an epoch, or a
     * clock running in seconds-since-boot all fail it.
     */
    {
        uint32_t mtime = 0u;
        uint32_t now = host_clock_seconds();

        rc = ext4_mtime_get(MOUNT_POINT "dir/renamed.txt", &mtime);
        if (rc != EOK) {
            return fail("ext4_mtime_get(renamed.txt)", rc);
        }
        if (mtime + 3600u < now || mtime > now + 60u) {
            printf("FAIL mtime %lu is not near now %lu\n",
                   (unsigned long)mtime, (unsigned long)now);
            ++failures;
            return 1;
        }
        rc = ext4_mtime_get(MOUNT_POINT "dir", &mtime);
        if (rc != EOK) {
            return fail("ext4_mtime_get(dir)", rc);
        }
        if (mtime + 3600u < now || mtime > now + 60u) {
            printf("FAIL directory mtime %lu is not near now %lu\n",
                   (unsigned long)mtime, (unsigned long)now);
            ++failures;
            return 1;
        }
    }

    rc = ext4_dir_open(&dir, MOUNT_POINT "many");
    if (rc != EOK) {
        return fail("ext4_dir_open", rc);
    }
    while ((entry = ext4_dir_entry_next(&dir)) != NULL) {
        if (entry->name_length == 1u && entry->name[0] == '.') {
            continue;
        }
        if (entry->name_length == 2u && entry->name[0] == '.' &&
            entry->name[1] == '.') {
            continue;
        }
        if (entry->name_length == 13u &&
            memcmp(entry->name, "entry_008.dat", 13u) == 0) {
            uint32_t direct[5] = {0u};
            uint32_t path_meta[5] = {0u};
            uint64_t direct_size = 0u;
            uint64_t path_size = 0u;

            rc = ext4_dir_entry_meta(&dir, entry, &direct[0], &direct[1],
                                     &direct[2], &direct[3], &direct[4],
                                     &direct_size);
            if (rc != EOK)
                return fail("ext4_dir_entry_meta", rc);
            many_path(path, sizeof(path), 8u);
            rc = ext4_meta_get(path, &path_meta[0], &path_meta[1],
                               &path_meta[2], &path_meta[3], &path_meta[4],
                               &path_size);
            if (rc != EOK)
                return fail("ext4_meta_get(entry_008.dat)", rc);
            if (memcmp(direct, path_meta, sizeof(direct)) != 0 ||
                direct_size != path_size || direct_size != 72u) {
                printf("FAIL direct directory metadata\n");
                ++failures;
                return 1;
            }
            metadata_checked = 1u;
        }
        ++counted;
    }
    ext4_dir_close(&dir);

    if (counted != MANY_FILES - 1u || metadata_checked == 0u) {
        printf("FAIL dir count got=%u want=%u\n", counted, MANY_FILES - 1u);
        ++failures;
        return 1;
    }
    return 0;
}

typedef struct ReadJob {
    ext4_file *file;
    uint8_t byte;
    size_t moved;
    int rc;
    int done;
} ReadJob;

typedef struct WriteJob {
    const char *path;
    unsigned index;
    unsigned bytes;
    int failed;
} WriteJob;

static void *
write_disjoint(void *argument)
{
    WriteJob *job = argument;

    job->failed = write_file(job->path, job->index, job->bytes);
    return NULL;
}

static int
check_concurrent_disjoint_writes(void)
{
    WriteJob left = {MOUNT_POINT "concurrent-left.bin", 210u, 8192u, 0};
    WriteJob right = {MOUNT_POINT "concurrent-right.bin", 211u, 8192u, 0};
    pthread_t left_thread;
    pthread_t right_thread;

    if (pthread_create(&left_thread, NULL, write_disjoint, &left) != 0 ||
        pthread_create(&right_thread, NULL, write_disjoint, &right) != 0)
        return fail("pthread_create disjoint writers", 0);
    (void)pthread_join(left_thread, NULL);
    (void)pthread_join(right_thread, NULL);
    if (left.failed || right.failed ||
        read_verify(left.path, left.index, left.bytes) ||
        read_verify(right.path, right.index, right.bytes))
        return fail("concurrent disjoint writes", 0);
    puts("concurrency oracle: disjoint writes survived readback");
    return 0;
}

static void *
read_one(void *argument)
{
    ReadJob *job = argument;

    job->rc = ext4_fread(job->file, &job->byte, 1u, &job->moved);
    if (pthread_mutex_lock(&controlled.mutex) != 0)
        abort();
    job->done = 1;
    (void)pthread_cond_broadcast(&controlled.changed);
    if (pthread_mutex_unlock(&controlled.mutex) != 0)
        abort();
    return NULL;
}

static struct timespec
realtime_after_ms(long milliseconds)
{
    struct timespec deadline;

    if (clock_gettime(CLOCK_REALTIME, &deadline) != 0)
        abort();
    deadline.tv_nsec += milliseconds * 1000000L;
    deadline.tv_sec += deadline.tv_nsec / 1000000000L;
    deadline.tv_nsec %= 1000000000L;
    return deadline;
}

/*
 * A is held inside a physical cache fill. B's unrelated byte is warm, so it
 * must finish without waiting for A and without issuing another device read.
 */
static int
check_concurrent_read_oracle(void)
{
    ext4_file a;
    ext4_file a_second;
    ext4_file b;
    ReadJob a_job = {.file = &a};
    ReadJob a_second_job = {.file = &a_second};
    ReadJob b_job = {.file = &b};
    pthread_t a_thread;
    pthread_t a_second_thread;
    pthread_t b_thread;
    struct timespec deadline;
    uint8_t warm = 0u;
    size_t moved = 0u;
    uint64_t reads_at_stall;
    int rc;

    if (do_umount() || do_mount())
        return 1;
    rc = ext4_fopen(&a, MOUNT_POINT "dir/renamed.txt", "rb");
    if (rc != EOK)
        return fail("concurrency open A", rc);
    rc = ext4_fopen(&b, MOUNT_POINT "dir/Case.dat", "rb");
    if (rc != EOK)
        return fail("concurrency open B", rc);
    rc = ext4_fopen(&a_second, MOUNT_POINT "dir/renamed.txt", "rb");
    if (rc != EOK)
        return fail("concurrency open second A", rc);
    rc = ext4_fread(&b, &warm, 1u, &moved);
    if (rc != EOK || moved != 1u ||
        warm != pattern_byte(3u, 0u) || ext4_fseek(&b, 0, SEEK_SET) != EOK)
        return fail("concurrency warm B", rc);

    if (pthread_mutex_lock(&controlled.mutex) != 0)
        abort();
    controlled.hold_next_read = 1;
    controlled.read_entered = 0;
    controlled.release_read = 0;
    if (pthread_mutex_unlock(&controlled.mutex) != 0)
        abort();

    if (pthread_create(&a_thread, NULL, read_one, &a_job) != 0)
        return fail("pthread_create A", 0);
    if (pthread_mutex_lock(&controlled.mutex) != 0)
        abort();
    deadline = realtime_after_ms(5000L);
    while (!controlled.read_entered &&
           pthread_cond_timedwait(&controlled.changed, &controlled.mutex,
                                  &deadline) == 0) {
    }
    if (!controlled.read_entered) {
        controlled.release_read = 1;
        (void)pthread_cond_broadcast(&controlled.changed);
        (void)pthread_mutex_unlock(&controlled.mutex);
        (void)pthread_join(a_thread, NULL);
        return fail("A did not reach physical read", 0);
    }
    reads_at_stall = controlled.reads;
    if (pthread_mutex_unlock(&controlled.mutex) != 0)
        abort();

    if (pthread_create(&b_thread, NULL, read_one, &b_job) != 0)
        return fail("pthread_create B", 0);
    if (pthread_mutex_lock(&controlled.mutex) != 0)
        abort();
    deadline = realtime_after_ms(5000L);
    while (!b_job.done &&
           pthread_cond_timedwait(&controlled.changed, &controlled.mutex,
                                  &deadline) == 0) {
    }
    if (!b_job.done || controlled.reads != reads_at_stall) {
        controlled.release_read = 1;
        (void)pthread_cond_broadcast(&controlled.changed);
        (void)pthread_mutex_unlock(&controlled.mutex);
        (void)pthread_join(a_thread, NULL);
        (void)pthread_join(b_thread, NULL);
        return fail("cached B did not overtake stalled A", 0);
    }
    if (pthread_mutex_unlock(&controlled.mutex) != 0)
        abort();

    if (pthread_create(&a_second_thread, NULL, read_one, &a_second_job) != 0)
        return fail("pthread_create second A", 0);
    if (pthread_mutex_lock(&controlled.mutex) != 0)
        abort();
    deadline = realtime_after_ms(100L);
    while (!a_second_job.done &&
           pthread_cond_timedwait(&controlled.changed, &controlled.mutex,
                                  &deadline) == 0) {
    }
    if (a_second_job.done || controlled.reads != reads_at_stall) {
        controlled.release_read = 1;
        (void)pthread_cond_broadcast(&controlled.changed);
        (void)pthread_mutex_unlock(&controlled.mutex);
        (void)pthread_join(a_thread, NULL);
        (void)pthread_join(a_second_thread, NULL);
        (void)pthread_join(b_thread, NULL);
        return fail("same-block miss was not coalesced", 0);
    }
    controlled.release_read = 1;
    (void)pthread_cond_broadcast(&controlled.changed);
    if (pthread_mutex_unlock(&controlled.mutex) != 0)
        abort();
    (void)pthread_join(a_thread, NULL);
    (void)pthread_join(a_second_thread, NULL);
    (void)pthread_join(b_thread, NULL);

    if (a_job.rc != EOK || a_job.moved != 1u ||
        a_job.byte != pattern_byte(1u, 0u) || a_second_job.rc != EOK ||
        a_second_job.moved != 1u ||
        a_second_job.byte != pattern_byte(1u, 0u) || b_job.rc != EOK ||
        b_job.moved != 1u || b_job.byte != pattern_byte(3u, 0u))
        return fail("concurrency oracle bytes", a_job.rc);
    if (controlled.reads != reads_at_stall + 1u)
        return fail("same-block miss issued duplicate I/O", 0);
    (void)ext4_fclose(&a);
    (void)ext4_fclose(&a_second);
    (void)ext4_fclose(&b);
    puts("concurrency oracle: cached B overtook stalled A without I/O");
    return 0;
}

static void
report(void)
{
    const AstraAllocMetrics *metrics = astra_alloc_metrics(&allocator);
    const AstraExt4AllocStats *stats = astra_ext4_alloc_stats();
    const AstraBlockMetrics *block = astra_block_metrics(&device);
    uint32_t index;

    printf("alloc: model=LP%u arena=%lu allocations=%lu frees=%lu failures=%lu "
           "rejections=%lu live=%lu peak_live=%lu peak_bytes=%lu valid=%d\n",
           (unsigned)(sizeof(void *) * 8u),
           (unsigned long)astra_alloc_arena_bytes(classes, class_count),
           (unsigned long)metrics->allocations, (unsigned long)metrics->frees,
           (unsigned long)metrics->failures,
           (unsigned long)metrics->rejections,
           (unsigned long)metrics->live_blocks,
           (unsigned long)metrics->peak_live_blocks,
           (unsigned long)metrics->peak_charged_bytes,
           astra_alloc_valid(&allocator));
    for (index = 0u; index < class_count; ++index) {
        printf("  class %-5lu count=%-5lu peak_live=%-5lu failures=%lu\n",
               (unsigned long)classes[index].size,
               (unsigned long)classes[index].count,
               (unsigned long)metrics->per_class[index].peak_live,
               (unsigned long)metrics->per_class[index].failures);
    }
    printf("port: split_transfers=%lu reentry_refusals=%lu "
           "unbound_requests=%lu rejected_frees=%lu\n",
           (unsigned long)port.split_transfers,
           (unsigned long)port.reentry_refusals,
           (unsigned long)stats->unbound_requests,
           (unsigned long)stats->rejected_frees);
    printf("block: reads=%llu sectors=%llu writes=%llu sectors=%llu "
           "failures=%llu\n",
           (unsigned long long)block->operation[ASTRA_BLOCK_OPERATION_READ]
               .calls,
           (unsigned long long)block->operation[ASTRA_BLOCK_OPERATION_READ]
               .units,
           (unsigned long long)block->operation[ASTRA_BLOCK_OPERATION_WRITE]
               .calls,
           (unsigned long long)block->operation[ASTRA_BLOCK_OPERATION_WRITE]
               .units,
           (unsigned long long)(
               block->operation[ASTRA_BLOCK_OPERATION_READ].failures +
               block->operation[ASTRA_BLOCK_OPERATION_WRITE].failures));
}

int
main(int argc, char **argv)
{
    /*
     * "verify-only" mounts a volume a previous run wrote and re-reads it
     * without touching anything. Run as a second process against the same
     * image, it is the check that the bytes on the volume mean the same thing
     * to a fresh mount as they did to the one that wrote them — which no
     * amount of read-back inside the writing process can establish.
     */
    int verify_only;
    int full_volume;
    int powercut_write;
    int powercut_recover;

    setvbuf(stdout, NULL, _IONBF, 0);

    if (argc < 2) {
        printf("usage: %s <image> [verify-only|partitioned|on-file|full|"
               "powercut-write|powercut-recover]\n", argv[0]);
        return 2;
    }
    verify_only = argc > 2 && strcmp(argv[2], "verify-only") == 0;
    partitioned = argc > 2 && strcmp(argv[2], "partitioned") == 0;
    on_file = argc > 2 && strcmp(argv[2], "on-file") == 0;
    full_volume = argc > 2 && strcmp(argv[2], "full") == 0;
    powercut_write = argc > 2 && strcmp(argv[2], "powercut-write") == 0;
    powercut_recover = argc > 2 && strcmp(argv[2], "powercut-recover") == 0;

    if (getenv("ASTRA_EXT4_DEBUG") != NULL) {
        ext4_dmask_set(DEBUG_ALL);
    }

    if (on_file) {
        if (astra_file_block_open(&file_backing, argv[1], SECTOR_SIZE,
                                  MAX_TRANSFER_SECTORS) != 0) {
            return fail("astra_file_block_open", 0);
        }
    } else if (load_image(argv[1])) {
        return 1;
    }
    if (powercut_write) {
        durable_storage = malloc(storage_bytes);
        if (durable_storage == NULL) {
            return fail("malloc durable image", 0);
        }
        memcpy(durable_storage, storage, storage_bytes);
    }
    if (partitioned && build_partitioned_layout()) {
        return 1;
    }
    if (bring_up()) {
        return 1;
    }
    if (partitioned && check_layout_via_reader()) {
        return 1;
    }
    if (do_mount()) {
        return 1;
    }
    if (powercut_write) {
        if (write_file(MOUNT_POINT "powercut.bin", 220u, 16384u) == 0 &&
            store_bytes(argv[1], durable_storage, storage_bytes) == 0) {
            puts("power-cut oracle: persisted only device-flushed bytes");
        }
        free(durable_storage);
        free(storage);
        return failures != 0;
    }
    if (powercut_recover) {
        (void)read_verify(MOUNT_POINT "powercut.bin", 220u, 16384u);
    } else if (verify_only) {
        if (verify() == 0) {
            (void)read_verify(MOUNT_POINT "concurrent-left.bin", 210u,
                              8192u);
            (void)read_verify(MOUNT_POINT "concurrent-right.bin", 211u,
                              8192u);
        }
    } else if (full_volume) {
        (void)check_full_volume_reports_enospc();
    } else if (populate() == 0) {
        if (verify() == 0 && !on_file &&
            check_coalesced_read_cache() == 0 &&
            check_concurrent_read_oracle() == 0)
            (void)check_concurrent_disjoint_writes();
    }
    do_umount();
    report();

    /*
     * An exhausted arena is a failed run even when lwext4 absorbs it and the
     * volume comes out clean. It did exactly that against a 200 GB volume
     * before this check existed: 241 refused allocations, e2fsck clean, and a
     * reported PASS. A budget that is silently over-run is not a budget.
     */
    if (astra_alloc_metrics(&allocator)->failures != 0u) {
        printf("FAIL allocator refused %lu allocations; the arena is too "
               "small for this volume\n",
               (unsigned long)astra_alloc_metrics(&allocator)->failures);
        ++failures;
    }
    /* Nothing may be left allocated once the volume is unmounted. */
    if (astra_alloc_metrics(&allocator)->live_blocks != 0u) {
        printf("FAIL allocator live blocks after umount: %lu\n",
               (unsigned long)astra_alloc_metrics(&allocator)->live_blocks);
        ++failures;
    }
    if (astra_alloc_valid(&allocator) == 0) {
        printf("FAIL allocator invariants broken\n");
        ++failures;
    }
    if (port.split_transfers == 0u) {
        printf("FAIL no transfer was split; the cap never bound\n");
        ++failures;
    }
    /*
     * The window is a boundary, not a convention. Any refusal here means
     * something asked for an address outside the volume, which on a real card
     * is the boot partition.
     */
    if (port.out_of_partition_refusals != 0u) {
        printf("FAIL %lu transfers addressed outside the volume\n",
               (unsigned long)port.out_of_partition_refusals);
        ++failures;
    }
    if (partitioned) {
        check_boot_region_intact();
    }
    /* On-file mode wrote through to the image already. */
    if (on_file) {
        astra_file_block_close(&file_backing);
    } else {
        if (store_image(argv[1])) {
            return 1;
        }
        free(storage);
    }
    if (failures != 0) {
        printf("astra ext4 mount: FAIL (%d)\n", failures);
        return 1;
    }
    puts("astra ext4 mount: PASS");
    return 0;
}

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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <astra/alloc.h>
#include <astra/block_device.h>
#include <astra/ext4_alloc.h>
#include <astra/ext4_port.h>
#include <astra/memory_block.h>

#include <ext4.h>
#include <ext4_debug.h>

#define MOUNT_POINT "/volume/"
#define SECTOR_SIZE 512u
#define MAX_TRANSFER_SECTORS 4u
#define BIG_FILE_BYTES (192u * 1024u)
#define MANY_FILES 200u

static uint8_t *storage;
static size_t storage_bytes;
static uint8_t sector_buffer[SECTOR_SIZE];

static AstraMemoryBlock memory;
static AstraBlockDevice device;
static AstraExt4Port port;

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
#define HOST_CLASS_COUNT 5u

static const AstraAllocClass host_classes[HOST_CLASS_COUNT] = {
    {64u, 16u},    /* nothing lands here on LP64; kept so the shape matches */
    {128u, 900u},  /* measured peak_live=838 */
    {2048u, 24u},  /* measured peak_live=16 */
    {4096u, 24u},  /* measured peak_live=17 */
    {8192u, 2u},   /* the htree sort array, 5,456 bytes on LP64 */
};

static const AstraAllocClass *classes;
static uint32_t class_count;

/* Sized for whichever table is larger; the unused tail costs nothing to run. */
static AstraAllocScalar arena[40000];

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

static int
store_image(const char *path)
{
    FILE *image = fopen(path, "wb");

    if (image == NULL) {
        return fail("fopen image for write", 0);
    }
    if (fwrite(storage, 1u, storage_bytes, image) != storage_bytes) {
        fclose(image);
        return fail("fwrite image", 0);
    }
    if (fclose(image) != 0) {
        return fail("fclose image", 0);
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

    astra_memory_block_init(&memory, storage, storage_bytes, SECTOR_SIZE,
                            MAX_TRANSFER_SECTORS, ASTRA_BLOCK_FLAG_PRESENT);
    astra_block_device_init(&device, &astra_memory_block_backend, &memory,
                            nanoseconds, NULL);
    if (astra_block_query(&device, &geometry) != ASTRA_BLOCK_OK) {
        return fail("astra_block_query", 0);
    }

    port_status = astra_ext4_port_init(&port, &device, sector_buffer,
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
    static uint8_t chunk[4096];
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

static void
many_path(char *out, size_t capacity, unsigned index)
{
    snprintf(out, capacity, MOUNT_POINT "many/entry_%03u.dat", index);
}

static int
populate(void)
{
    char path[64];
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

    if (write_file(MOUNT_POINT "hello.txt", 1u, 37u)) {
        return 1;
    }
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
    unsigned index;
    ext4_file probe;
    int rc;

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
        ++counted;
    }
    ext4_dir_close(&dir);

    if (counted != MANY_FILES - 1u) {
        printf("FAIL dir count got=%u want=%u\n", counted, MANY_FILES - 1u);
        ++failures;
        return 1;
    }
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

    setvbuf(stdout, NULL, _IONBF, 0);

    if (argc < 2) {
        printf("usage: %s <image> [verify-only]\n", argv[0]);
        return 2;
    }
    verify_only = argc > 2 && strcmp(argv[2], "verify-only") == 0;

    if (getenv("ASTRA_EXT4_DEBUG") != NULL) {
        ext4_dmask_set(DEBUG_ALL);
    }

    if (load_image(argv[1]) || bring_up() || do_mount()) {
        return 1;
    }
    if (verify_only) {
        verify();
    } else if (populate() == 0) {
        verify();
    }
    do_umount();
    report();

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
    if (store_image(argv[1])) {
        return 1;
    }

    free(storage);
    if (failures != 0) {
        printf("astra ext4 mount: FAIL (%d)\n", failures);
        return 1;
    }
    puts("astra ext4 mount: PASS");
    return 0;
}

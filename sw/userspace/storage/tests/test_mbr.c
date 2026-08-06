#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <astra/block_device.h>
#include <astra/mbr.h>
#include <astra/memory_block.h>

#define SECTOR_SIZE 512u
#define SECTORS 4096u
#define BYTES (SECTORS * SECTOR_SIZE)

static uint8_t storage[BYTES];
static uint8_t sector_buffer[SECTOR_SIZE];

static uint64_t
ticks(void *context)
{
    static uint64_t now;

    (void)context;
    return ++now;
}

static void
write_le32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)(value & 0xffu);
    bytes[1] = (uint8_t)((value >> 8) & 0xffu);
    bytes[2] = (uint8_t)((value >> 16) & 0xffu);
    bytes[3] = (uint8_t)((value >> 24) & 0xffu);
}

static void
set_entry(uint32_t index, uint8_t type, uint32_t first, uint32_t count,
          int bootable)
{
    uint8_t *entry = &storage[446u + (index * 16u)];

    memset(entry, 0, 16u);
    entry[0] = bootable ? 0x80u : 0x00u;
    entry[4] = type;
    write_le32(&entry[8], first);
    write_le32(&entry[12], count);
}

static void
begin_table(void)
{
    memset(storage, 0, SECTOR_SIZE);
    storage[510] = 0x55u;
    storage[511] = 0xaau;
}

static void
bring_up(AstraMemoryBlock *memory, AstraBlockDevice *device)
{
    AstraBlockGeometry geometry;

    astra_memory_block_init(memory, storage, sizeof(storage), SECTOR_SIZE, 16u,
                            ASTRA_BLOCK_FLAG_PRESENT);
    astra_block_device_init(device, &astra_memory_block_backend, memory, ticks,
                            NULL);
    assert(astra_block_query(device, &geometry) == ASTRA_BLOCK_OK);
}

static void
test_classify_matches_stage0(void)
{
    /* Exactly sw/stage0's fat_partition_type(). */
    assert(astra_mbr_classify(0x04u) == ASTRA_MBR_FAT);
    assert(astra_mbr_classify(0x06u) == ASTRA_MBR_FAT);
    assert(astra_mbr_classify(0x0bu) == ASTRA_MBR_FAT);
    assert(astra_mbr_classify(0x0cu) == ASTRA_MBR_FAT);
    assert(astra_mbr_classify(0x0eu) == ASTRA_MBR_FAT);
    /* And nothing else is FAT, including neighbours of that set. */
    assert(astra_mbr_classify(0x05u) == ASTRA_MBR_EXTENDED);
    assert(astra_mbr_classify(0x0fu) == ASTRA_MBR_EXTENDED);
    assert(astra_mbr_classify(0x07u) == ASTRA_MBR_OTHER);
    assert(astra_mbr_classify(0x0du) == ASTRA_MBR_OTHER);

    assert(astra_mbr_classify(0x83u) == ASTRA_MBR_LINUX);
    assert(astra_mbr_classify(0xeeu) == ASTRA_MBR_GPT_PROTECTIVE);
    assert(astra_mbr_classify(0x00u) == ASTRA_MBR_EMPTY);
}

static void
test_reads_a_boot_layout(void)
{
    AstraMemoryBlock memory;
    AstraBlockDevice device;
    AstraMbrTable table;
    const AstraMbrEntry *entry;

    bring_up(&memory, &device);
    begin_table();
    set_entry(0u, 0x0cu, 2048u, 1024u, 1);  /* FAT32 LBA boot partition */
    set_entry(1u, 0x83u, 3072u, 1024u, 0);  /* an Astra volume */

    assert(astra_mbr_read(&device, sector_buffer, sizeof(sector_buffer),
                          &table, 0u) == ASTRA_BLOCK_OK);
    assert(table.used == 2u);

    assert(table.entry[0].kind == ASTRA_MBR_FAT);
    assert(table.entry[0].first_sector == 2048u);
    assert(table.entry[0].sector_count == 1024u);
    assert(table.entry[0].bootable == 1u);
    assert(table.entry[1].kind == ASTRA_MBR_LINUX);
    assert(table.entry[1].bootable == 0u);
    assert(table.entry[2].kind == ASTRA_MBR_EMPTY);
    assert(table.entry[3].kind == ASTRA_MBR_EMPTY);

    entry = astra_mbr_find(&table, ASTRA_MBR_FAT);
    assert(entry != NULL && entry->first_sector == 2048u);
    entry = astra_mbr_find(&table, ASTRA_MBR_LINUX);
    assert(entry != NULL && entry->first_sector == 3072u);
    assert(astra_mbr_find(&table, ASTRA_MBR_EXTENDED) == NULL);
}

static void
test_rejects_bad_tables(void)
{
    AstraMemoryBlock memory;
    AstraBlockDevice device;
    AstraMbrTable table;

    bring_up(&memory, &device);

    /* No signature. */
    memset(storage, 0, SECTOR_SIZE);
    assert(astra_mbr_read(&device, sector_buffer, sizeof(sector_buffer),
                          &table, 0u) == ASTRA_BLOCK_CORRUPT);

    /*
     * An entry running past the device. Truncating it would hand out a window
     * whose end nobody chose, so the whole table is refused.
     */
    begin_table();
    set_entry(0u, 0x83u, SECTORS - 8u, 16u, 0);
    assert(astra_mbr_read(&device, sector_buffer, sizeof(sector_buffer),
                          &table, 0u) == ASTRA_BLOCK_CORRUPT);

    /* Exactly reaching the last sector is fine. */
    begin_table();
    set_entry(0u, 0x83u, SECTORS - 8u, 8u, 0);
    assert(astra_mbr_read(&device, sector_buffer, sizeof(sector_buffer),
                          &table, 0u) == ASTRA_BLOCK_OK);
    assert(table.used == 1u);

    /* A zero-length entry is empty whatever its type byte claims. */
    begin_table();
    set_entry(0u, 0x0cu, 2048u, 0u, 0);
    assert(astra_mbr_read(&device, sector_buffer, sizeof(sector_buffer),
                          &table, 0u) == ASTRA_BLOCK_OK);
    assert(table.used == 0u);
    assert(table.entry[0].kind == ASTRA_MBR_EMPTY);
    assert(astra_mbr_find(&table, ASTRA_MBR_FAT) == NULL);

    /*
     * A GPT card. stage0 lives in FPGA BRAM and only looks for FAT in the four
     * primary slots, so this layout does not boot; naming the type makes that
     * diagnosable rather than looking like an empty disk.
     */
    begin_table();
    set_entry(0u, 0xeeu, 1u, SECTORS - 1u, 0);
    assert(astra_mbr_read(&device, sector_buffer, sizeof(sector_buffer),
                          &table, 0u) == ASTRA_BLOCK_OK);
    assert(table.entry[0].kind == ASTRA_MBR_GPT_PROTECTIVE);
    assert(astra_mbr_find(&table, ASTRA_MBR_FAT) == NULL);

    /*
     * Overlapping entries, which is the dangerous shape rather than merely the
     * malformed one. The port confines a mount to the window this table hands
     * out so a filesystem defect cannot reach the boot partition; if the table
     * itself overlaps, that window is legitimately inside the boot partition
     * and every write to it is correctly permitted. The table has to be refused
     * here or the enforcement downstream guarantees nothing.
     */
    begin_table();
    set_entry(0u, 0x0cu, 2048u, 1024u, 1);
    set_entry(1u, 0x83u, 2560u, 1024u, 0);
    assert(astra_mbr_read(&device, sector_buffer, sizeof(sector_buffer),
                          &table, 0u) == ASTRA_BLOCK_CORRUPT);

    /* One partition wholly inside another is the same refusal. */
    begin_table();
    set_entry(0u, 0x0cu, 2048u, 1024u, 1);
    set_entry(1u, 0x83u, 2100u, 16u, 0);
    assert(astra_mbr_read(&device, sector_buffer, sizeof(sector_buffer),
                          &table, 0u) == ASTRA_BLOCK_CORRUPT);

    /* A partition over the table itself is refused however small. */
    begin_table();
    set_entry(0u, 0x83u, 0u, 64u, 0);
    assert(astra_mbr_read(&device, sector_buffer, sizeof(sector_buffer),
                          &table, 0u) == ASTRA_BLOCK_CORRUPT);

    /* Butting up exactly, which is the layout every real card uses, is fine. */
    begin_table();
    set_entry(0u, 0x0cu, 2048u, 512u, 1);
    set_entry(1u, 0x83u, 2560u, 512u, 0);
    assert(astra_mbr_read(&device, sector_buffer, sizeof(sector_buffer),
                          &table, 0u) == ASTRA_BLOCK_OK);
    assert(table.used == 2u);

    assert(astra_mbr_read(NULL, sector_buffer, sizeof(sector_buffer), &table,
                          0u) == ASTRA_BLOCK_INVALID_ARGUMENT);
    assert(astra_mbr_read(&device, NULL, sizeof(sector_buffer), &table, 0u) ==
           ASTRA_BLOCK_INVALID_ARGUMENT);
    assert(astra_mbr_read(&device, sector_buffer, SECTOR_SIZE - 1u, &table,
                          0u) == ASTRA_BLOCK_INVALID_ARGUMENT);
}

/*
 * The check any volume tool must consult before writing. Getting this wrong
 * means an unbootable machine with the user's files gone, so every adjacency
 * case is here rather than the obvious overlap alone.
 */
static void
test_range_conflicts(void)
{
    AstraMemoryBlock memory;
    AstraBlockDevice device;
    AstraMbrTable table;

    bring_up(&memory, &device);
    begin_table();
    set_entry(0u, 0x0cu, 2048u, 1024u, 1); /* FAT: [2048, 3072) */
    set_entry(1u, 0x83u, 3072u, 512u, 0);  /* volume: [3072, 3584) */
    assert(astra_mbr_read(&device, sector_buffer, sizeof(sector_buffer),
                          &table, 0u) == ASTRA_BLOCK_OK);

    /* Free space after both partitions, owned by nobody yet. */
    assert(!astra_mbr_range_conflicts(&table, ASTRA_MBR_ENTRY_COUNT, 3584u,
                                      512u));

    /* Butting up against the end of the last partition is not an overlap. */
    assert(!astra_mbr_range_conflicts(&table, ASTRA_MBR_ENTRY_COUNT, 3584u,
                                      1u));

    /* One sector into the FAT partition, from either side. */
    assert(astra_mbr_range_conflicts(&table, ASTRA_MBR_ENTRY_COUNT, 3071u,
                                     512u));
    assert(astra_mbr_range_conflicts(&table, ASTRA_MBR_ENTRY_COUNT, 1024u,
                                     1025u));

    /* Entirely inside the FAT partition. */
    assert(astra_mbr_range_conflicts(&table, ASTRA_MBR_ENTRY_COUNT, 2100u,
                                     4u));

    /* Entirely containing it. */
    assert(astra_mbr_range_conflicts(&table, ASTRA_MBR_ENTRY_COUNT, 1024u,
                                     4096u - 1024u));

    /* An entry may be grown into its own space without conflicting. */
    assert(!astra_mbr_range_conflicts(&table, 1u, 3072u, 512u));
    /* But not into its neighbour's. */
    assert(astra_mbr_range_conflicts(&table, 1u, 3000u, 512u));

    /* Sector 0 holds the table; nothing may be allocated over it. */
    assert(astra_mbr_range_conflicts(&table, ASTRA_MBR_ENTRY_COUNT, 0u, 1u));
    assert(astra_mbr_range_conflicts(&table, ASTRA_MBR_ENTRY_COUNT, 0u, 2048u));

    /* Degenerate requests are conflicts, not permissions. */
    assert(astra_mbr_range_conflicts(&table, ASTRA_MBR_ENTRY_COUNT, 3584u, 0u));
    assert(astra_mbr_range_conflicts(NULL, ASTRA_MBR_ENTRY_COUNT, 3584u, 1u));
}

int
main(void)
{
    test_classify_matches_stage0();
    test_reads_a_boot_layout();
    test_rejects_bad_tables();
    test_range_conflicts();
    puts("astra mbr: PASS");
    return 0;
}

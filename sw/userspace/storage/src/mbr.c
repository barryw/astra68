#include <astra/mbr.h>

#include <stddef.h>

#define MBR_TABLE_OFFSET 446u
#define MBR_ENTRY_BYTES 16u
#define MBR_SIGNATURE_OFFSET 510u

/* On-disk partition entry field offsets. */
#define ENTRY_STATUS 0u
#define ENTRY_TYPE 4u
#define ENTRY_FIRST_LBA 8u
#define ENTRY_SECTORS 12u

/* The table is little-endian on disk regardless of the CPU reading it. */
static uint32_t
read_le32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

AstraMbrKind
astra_mbr_classify(uint8_t type)
{
    switch (type) {
    case 0x00u:
        return ASTRA_MBR_EMPTY;
    /* Conventional FAT partition type bytes. */
    case 0x04u:
    case 0x06u:
    case 0x0bu:
    case 0x0cu:
    case 0x0eu:
        return ASTRA_MBR_FAT;
    case 0x83u:
        return ASTRA_MBR_LINUX;
    case 0x05u:
    case 0x0fu:
        return ASTRA_MBR_EXTENDED;
    /* A GPT disk carries one protective entry of this type. */
    case 0xeeu:
        return ASTRA_MBR_GPT_PROTECTIVE;
    default:
        return ASTRA_MBR_OTHER;
    }
}

AstraBlockStatus
astra_mbr_read(AstraBlockDevice *device, void *sector_buffer,
               uint32_t sector_buffer_bytes, AstraMbrTable *table,
               uint64_t deadline)
{
    const uint8_t *sector = sector_buffer;
    AstraBlockStatus status;
    uint32_t index;

    if (device == NULL || sector_buffer == NULL || table == NULL) {
        return ASTRA_BLOCK_INVALID_ARGUMENT;
    }
    if (sector_buffer_bytes < device->geometry.sector_size ||
        device->geometry.sector_size < ASTRA_MBR_SECTOR_BYTES) {
        return ASTRA_BLOCK_INVALID_ARGUMENT;
    }

    for (index = 0u; index < ASTRA_MBR_ENTRY_COUNT; ++index) {
        table->entry[index].first_sector = 0u;
        table->entry[index].sector_count = 0u;
        table->entry[index].type = 0u;
        table->entry[index].bootable = 0u;
        table->entry[index].kind = ASTRA_MBR_EMPTY;
    }
    table->used = 0u;

    status = astra_block_read(device, 0u, 1u, sector_buffer, deadline);
    if (status != ASTRA_BLOCK_OK) {
        return status;
    }

    if (sector[MBR_SIGNATURE_OFFSET] != 0x55u ||
        sector[MBR_SIGNATURE_OFFSET + 1u] != 0xaau) {
        return ASTRA_BLOCK_CORRUPT;
    }

    for (index = 0u; index < ASTRA_MBR_ENTRY_COUNT; ++index) {
        const uint8_t *raw =
            &sector[MBR_TABLE_OFFSET + (index * MBR_ENTRY_BYTES)];
        AstraMbrEntry *entry = &table->entry[index];
        uint32_t first = read_le32(&raw[ENTRY_FIRST_LBA]);
        uint32_t count = read_le32(&raw[ENTRY_SECTORS]);

        entry->type = raw[ENTRY_TYPE];
        entry->bootable = raw[ENTRY_STATUS] == 0x80u ? 1u : 0u;
        entry->kind = astra_mbr_classify(entry->type);

        if (count == 0u) {
            entry->kind = ASTRA_MBR_EMPTY;
            continue;
        }
        /*
         * An entry that runs past the device is a table this code refuses to
         * act on at all, rather than one it silently truncates. Truncating
         * would hand out a window whose end nobody chose.
         */
        if ((uint64_t)first + (uint64_t)count >
            device->geometry.sector_count) {
            return ASTRA_BLOCK_CORRUPT;
        }
        entry->first_sector = first;
        entry->sector_count = count;
        ++table->used;
    }

    /*
     * Overlapping entries are refused for the same reason an entry running past
     * the device is, and it is the more dangerous of the two.
     *
     * The port confines a mount to the window this table hands out, so that a
     * filesystem defect cannot reach the partition holding the code that boots
     * the machine. That guarantee is only as good as the table: if two entries
     * overlap, the window is legitimately inside the boot partition and every
     * write to it is correctly permitted. Checking here is what makes the
     * enforcement downstream mean what it claims.
     *
     * A second pass rather than a check inside the loop above, because an entry
     * can only be compared against entries that have been parsed.
     */
    for (index = 0u; index < ASTRA_MBR_ENTRY_COUNT; ++index) {
        if (table->entry[index].sector_count == 0u) {
            continue;
        }
        if (astra_mbr_range_conflicts(table, index,
                                      table->entry[index].first_sector,
                                      table->entry[index].sector_count)) {
            return ASTRA_BLOCK_CORRUPT;
        }
    }
    return ASTRA_BLOCK_OK;
}

const AstraMbrEntry *
astra_mbr_find(const AstraMbrTable *table, AstraMbrKind kind)
{
    uint32_t index;

    if (table == NULL) {
        return NULL;
    }
    for (index = 0u; index < ASTRA_MBR_ENTRY_COUNT; ++index) {
        if (table->entry[index].sector_count != 0u &&
            table->entry[index].kind == kind) {
            return &table->entry[index];
        }
    }
    return NULL;
}

int
astra_mbr_range_conflicts(const AstraMbrTable *table, uint32_t index,
                          uint32_t first_sector, uint32_t sector_count)
{
    uint64_t start = first_sector;
    uint64_t end = (uint64_t)first_sector + (uint64_t)sector_count;
    uint32_t other;

    if (table == NULL || sector_count == 0u) {
        return 1;
    }
    /* Sector 0 holds the table itself; nothing may be allocated over it. */
    if (start == 0u) {
        return 1;
    }
    for (other = 0u; other < ASTRA_MBR_ENTRY_COUNT; ++other) {
        uint64_t other_start;
        uint64_t other_end;

        if (other == index || table->entry[other].sector_count == 0u) {
            continue;
        }
        other_start = table->entry[other].first_sector;
        other_end = other_start + table->entry[other].sector_count;
        if (start < other_end && other_start < end) {
            return 1;
        }
    }
    return 0;
}

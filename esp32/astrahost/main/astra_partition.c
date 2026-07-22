#include "astra_partition.h"

#include <string.h>

// GPT stores the first three GUID fields little-endian and the remaining
// eight bytes in display order.
static const uint8_t astra_type_guid[16] = {
    0x04, 0x11, 0x99, 0x1a, 0x17, 0x93, 0xfd, 0x4c,
    0xb5, 0xeb, 0x04, 0x02, 0x47, 0x15, 0x70, 0xac,
};

static uint32_t read_le32(const uint8_t *data)
{
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

static uint64_t read_le64(const uint8_t *data)
{
    return (uint64_t)read_le32(data) |
           ((uint64_t)read_le32(data + 4) << 32);
}

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t size)
{
    for (size_t index = 0; index < size; ++index) {
        crc ^= data[index];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return crc;
}

static bool protective_mbr_valid(const uint8_t sector[ASTRA_SECTOR_BYTES])
{
    if (sector[510] != 0x55 || sector[511] != 0xaa)
        return false;
    for (unsigned entry = 0; entry < 4; ++entry) {
        const uint8_t *record = sector + 446 + entry * 16;
        if (record[4] == 0xee && read_le32(record + 8) == 1)
            return true;
    }
    return false;
}

typedef astra_partition_result_t (*entry_visitor_fn)(
    const uint8_t *entry, void *context);

static bool guid_is_zero(const uint8_t *guid)
{
    uint8_t combined = 0;
    for (unsigned index = 0; index < 16; ++index)
        combined |= guid[index];
    return combined == 0;
}

static astra_partition_result_t scan_entries(
    astra_partition_read_fn read_sector, void *read_context,
    uint64_t entries_lba, uint32_t entry_count, uint32_t entry_size,
    bool verify_crc, uint32_t expected_crc,
    entry_visitor_fn visitor, void *visitor_context)
{
    uint8_t sector[ASTRA_SECTOR_BYTES];
    uint8_t entry[ASTRA_SECTOR_BYTES];
    uint64_t table_bytes = (uint64_t)entry_count * entry_size;
    uint64_t table_sectors =
        (table_bytes + ASTRA_SECTOR_BYTES - 1) / ASTRA_SECTOR_BYTES;
    uint64_t bytes_left = table_bytes;
    uint32_t entry_fill = 0;
    uint32_t table_crc = 0xffffffffu;

    for (uint64_t table_sector = 0; table_sector < table_sectors;
         ++table_sector) {
        if (!read_sector(read_context, entries_lba + table_sector, sector))
            return ASTRA_PARTITION_IO_ERROR;
        size_t valid_bytes = bytes_left < ASTRA_SECTOR_BYTES ?
                             (size_t)bytes_left : ASTRA_SECTOR_BYTES;
        if (verify_crc)
            table_crc = crc32_update(table_crc, sector, valid_bytes);
        bytes_left -= valid_bytes;

        size_t source = 0;
        while (source < valid_bytes) {
            size_t available = valid_bytes - source;
            size_t needed = entry_size - entry_fill;
            size_t copy = available < needed ? available : needed;
            memcpy(entry + entry_fill, sector + source, copy);
            entry_fill += (uint32_t)copy;
            source += copy;
            if (entry_fill != entry_size)
                continue;

            astra_partition_result_t result =
                visitor(entry, visitor_context);
            if (result != ASTRA_PARTITION_OK)
                return result;
            entry_fill = 0;
        }
    }

    if (entry_fill != 0 || bytes_left != 0 ||
        (verify_crc && ~table_crc != expected_crc))
        return ASTRA_PARTITION_CORRUPT;
    return ASTRA_PARTITION_OK;
}

typedef struct {
    uint64_t first_usable;
    uint64_t last_usable;
    uint64_t media_sectors;
    astra_partition_t *partition;
    unsigned matches;
} find_entry_context_t;

static astra_partition_result_t find_astra_entry(
    const uint8_t *entry, void *raw_context)
{
    find_entry_context_t *context = raw_context;
    if (guid_is_zero(entry))
        return ASTRA_PARTITION_OK;

    uint64_t first_lba = read_le64(entry + 32);
    uint64_t last_lba = read_le64(entry + 40);
    if (first_lba < context->first_usable || first_lba > last_lba ||
        last_lba > context->last_usable ||
        last_lba >= context->media_sectors)
        return ASTRA_PARTITION_CORRUPT;
    if (memcmp(entry, astra_type_guid, sizeof(astra_type_guid)) != 0)
        return ASTRA_PARTITION_OK;
    if (++context->matches > 1)
        return ASTRA_PARTITION_DUPLICATE;

    context->partition->first_lba = first_lba;
    context->partition->sector_count = last_lba - first_lba + 1;
    return ASTRA_PARTITION_OK;
}

typedef struct {
    uint64_t astra_first;
    uint64_t astra_last;
} overlap_context_t;

static astra_partition_result_t reject_astra_overlap(
    const uint8_t *entry, void *raw_context)
{
    overlap_context_t *context = raw_context;
    if (guid_is_zero(entry) ||
        memcmp(entry, astra_type_guid, sizeof(astra_type_guid)) == 0)
        return ASTRA_PARTITION_OK;

    uint64_t first_lba = read_le64(entry + 32);
    uint64_t last_lba = read_le64(entry + 40);
    if (first_lba <= context->astra_last &&
        context->astra_first <= last_lba)
        return ASTRA_PARTITION_CORRUPT;
    return ASTRA_PARTITION_OK;
}

astra_partition_result_t astra_partition_find(
    astra_partition_read_fn read_sector, void *context,
    uint64_t media_sectors, astra_partition_t *partition)
{
    uint8_t sector[ASTRA_SECTOR_BYTES];
    if (read_sector == NULL || partition == NULL || media_sectors < 3)
        return ASTRA_PARTITION_CORRUPT;
    partition->first_lba = 0;
    partition->sector_count = 0;

    if (!read_sector(context, 0, sector))
        return ASTRA_PARTITION_IO_ERROR;
    if (!protective_mbr_valid(sector))
        return ASTRA_PARTITION_NOT_GPT;
    if (!read_sector(context, 1, sector))
        return ASTRA_PARTITION_IO_ERROR;
    if (memcmp(sector, "EFI PART", 8) != 0)
        return ASTRA_PARTITION_NOT_GPT;

    uint32_t header_size = read_le32(sector + 12);
    uint32_t expected_header_crc = read_le32(sector + 16);
    uint32_t revision = read_le32(sector + 8);
    uint32_t reserved = read_le32(sector + 20);
    uint64_t current_lba = read_le64(sector + 24);
    uint64_t backup_lba = read_le64(sector + 32);
    uint64_t first_usable = read_le64(sector + 40);
    uint64_t last_usable = read_le64(sector + 48);
    uint64_t entries_lba = read_le64(sector + 72);
    uint32_t entry_count = read_le32(sector + 80);
    uint32_t entry_size = read_le32(sector + 84);
    uint32_t expected_entries_crc = read_le32(sector + 88);

    if (revision != 0x00010000u || reserved != 0 ||
        header_size < 92 || header_size > ASTRA_SECTOR_BYTES ||
        current_lba != 1 || backup_lba != media_sectors - 1 ||
        first_usable > last_usable || last_usable >= media_sectors ||
        entries_lba < 2 || entries_lba >= media_sectors ||
        entry_count == 0 || entry_size < 128 ||
        entry_size > ASTRA_SECTOR_BYTES ||
        (entry_size & 7u) != 0)
        return ASTRA_PARTITION_CORRUPT;
    if (entry_count > ASTRA_GPT_MAX_ENTRIES)
        return ASTRA_PARTITION_UNSUPPORTED;

    uint8_t saved_crc[4];
    memcpy(saved_crc, sector + 16, sizeof(saved_crc));
    memset(sector + 16, 0, sizeof(saved_crc));
    uint32_t actual_header_crc =
        ~crc32_update(0xffffffffu, sector, header_size);
    memcpy(sector + 16, saved_crc, sizeof(saved_crc));
    if (actual_header_crc != expected_header_crc)
        return ASTRA_PARTITION_CORRUPT;

    uint64_t table_bytes = (uint64_t)entry_count * entry_size;
    uint64_t table_sectors =
        (table_bytes + ASTRA_SECTOR_BYTES - 1) / ASTRA_SECTOR_BYTES;
    if (table_bytes == 0 || entries_lba + table_sectors < entries_lba ||
        entries_lba + table_sectors > media_sectors ||
        entries_lba + table_sectors > first_usable)
        return ASTRA_PARTITION_CORRUPT;

    find_entry_context_t find_context = {
        .first_usable = first_usable,
        .last_usable = last_usable,
        .media_sectors = media_sectors,
        .partition = partition,
        .matches = 0,
    };
    astra_partition_result_t result = scan_entries(
        read_sector, context, entries_lba, entry_count, entry_size,
        true, expected_entries_crc, find_astra_entry, &find_context);
    if (result != ASTRA_PARTITION_OK)
        return result;
    if (find_context.matches == 0)
        return ASTRA_PARTITION_NOT_FOUND;

    overlap_context_t overlap_context = {
        .astra_first = partition->first_lba,
        .astra_last = partition->first_lba + partition->sector_count - 1,
    };
    return scan_entries(
        read_sector, context, entries_lba, entry_count, entry_size,
        false, 0, reject_astra_overlap, &overlap_context);
}

bool astra_partition_u32_addressable(const astra_partition_t *partition)
{
    if (partition == NULL || partition->sector_count == 0 ||
        partition->first_lba > UINT32_MAX)
        return false;
    return partition->sector_count - 1 <=
           (uint64_t)UINT32_MAX - partition->first_lba;
}

bool astra_partition_translate_u32(const astra_partition_t *partition,
                                   uint64_t relative_lba,
                                   uint32_t sector_count,
                                   uint32_t *absolute_lba)
{
    if (absolute_lba == NULL || sector_count == 0 ||
        !astra_partition_u32_addressable(partition) ||
        relative_lba >= partition->sector_count ||
        sector_count > partition->sector_count - relative_lba)
        return false;

    uint64_t absolute = partition->first_lba + relative_lba;
    if ((uint64_t)sector_count - 1 > (uint64_t)UINT32_MAX - absolute)
        return false;
    *absolute_lba = (uint32_t)absolute;
    return true;
}

const char *astra_partition_result_string(astra_partition_result_t result)
{
    switch (result) {
    case ASTRA_PARTITION_OK: return "ok";
    case ASTRA_PARTITION_NOT_GPT: return "not a GPT disk";
    case ASTRA_PARTITION_NOT_FOUND: return "Astra partition not found";
    case ASTRA_PARTITION_IO_ERROR: return "partition table read failed";
    case ASTRA_PARTITION_CORRUPT: return "corrupt partition table";
    case ASTRA_PARTITION_UNSUPPORTED: return "unsupported partition table";
    case ASTRA_PARTITION_DUPLICATE: return "multiple Astra partitions";
    default: return "unknown partition result";
    }
}

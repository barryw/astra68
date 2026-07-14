#include "fat_loader.h"

#define ROM_HEADER_SIZE 32u
#define ROM_BASE 0xffe00000u
#define ROM_LIMIT 0xffe40000u
#define ROM_LOAD_ADDRESS 0x03e00000u
#define STACK_MIN 0x01ff8000u
#define STACK_MAX 0x02000000u

typedef struct {
    uint32_t volume_lba;
    uint32_t fat_lba;
    uint32_t data_lba;
    uint32_t root_lba;
    uint32_t root_cluster;
    uint32_t cluster_count;
    uint32_t root_sectors;
    uint8_t sectors_per_cluster;
    uint8_t sectors_per_cluster_shift;
    uint8_t fat32;
} FatVolume;

typedef struct {
    uint32_t first_cluster;
    uint32_t size;
} FatFile;

static uint16_t read_le16(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t read_le32(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static uint16_t read_be16(const uint8_t *data)
{
    return ((uint16_t)data[0] << 8) | (uint16_t)data[1];
}

static uint32_t read_be32(const uint8_t *data)
{
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) | (uint32_t)data[3];
}

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, uint32_t size)
{
    while (size-- != 0u) {
        crc ^= *data++;
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return crc;
}

static int read_sector(const FatBootIo *io, uint32_t lba, uint8_t *data)
{
    return io->read_sector(io->context, lba, data) == 0;
}

static int power_of_two_shift(uint8_t value, uint8_t *shift)
{
    if (value == 0u || (value & (uint8_t)(value - 1u)) != 0u) return 0;
    *shift = 0u;
    while (value > 1u) {
        value >>= 1;
        ++*shift;
    }
    return 1;
}

static int looks_like_fat_bpb(const uint8_t *sector)
{
    uint8_t shift;
    return sector[510] == 0x55u && sector[511] == 0xaau &&
           read_le16(&sector[11]) == FAT_BOOT_SECTOR_SIZE &&
           power_of_two_shift(sector[13], &shift) &&
           read_le16(&sector[14]) != 0u && sector[16] != 0u;
}

static int fat_partition_type(uint8_t type)
{
    return type == 0x04u || type == 0x06u || type == 0x0bu ||
           type == 0x0cu || type == 0x0eu;
}

static int mount_volume(const FatBootIo *io, uint8_t *sector, FatVolume *volume)
{
    if (!read_sector(io, 0u, sector)) return FAT_BOOT_ERR_IO;

    uint32_t volume_lba = 0u;
    if (!looks_like_fat_bpb(sector)) {
        if (sector[510] != 0x55u || sector[511] != 0xaau)
            return FAT_BOOT_ERR_PARTITION;
        int found = 0;
        for (unsigned index = 0; index < 4; ++index) {
            const uint8_t *entry = &sector[446u + index * 16u];
            if (fat_partition_type(entry[4]) && read_le32(&entry[12]) != 0u) {
                volume_lba = read_le32(&entry[8]);
                found = 1;
                break;
            }
        }
        if (!found) return FAT_BOOT_ERR_PARTITION;
        if (!read_sector(io, volume_lba, sector)) return FAT_BOOT_ERR_IO;
        if (!looks_like_fat_bpb(sector)) return FAT_BOOT_ERR_FILESYSTEM;
    }

    uint8_t cluster_shift;
    uint8_t sectors_per_cluster = sector[13];
    if (!power_of_two_shift(sectors_per_cluster, &cluster_shift))
        return FAT_BOOT_ERR_FILESYSTEM;

    uint32_t reserved = read_le16(&sector[14]);
    uint32_t fat_count = sector[16];
    uint32_t root_entries = read_le16(&sector[17]);
    uint32_t total_sectors = read_le16(&sector[19]);
    if (total_sectors == 0u) total_sectors = read_le32(&sector[32]);
    uint32_t fat_sectors = read_le16(&sector[22]);
    if (fat_sectors == 0u) fat_sectors = read_le32(&sector[36]);
    if (total_sectors == 0u || fat_sectors == 0u)
        return FAT_BOOT_ERR_FILESYSTEM;

    uint32_t root_sectors = ((root_entries * 32u) + 511u) >> 9;
    uint32_t metadata_sectors = reserved + fat_count * fat_sectors + root_sectors;
    if (metadata_sectors >= total_sectors) return FAT_BOOT_ERR_FILESYSTEM;
    uint32_t cluster_count = (total_sectors - metadata_sectors) >> cluster_shift;
    if (cluster_count < 4085u) return FAT_BOOT_ERR_FAT12;

    volume->volume_lba = volume_lba;
    volume->fat_lba = volume_lba + reserved;
    volume->root_lba = volume_lba + reserved + fat_count * fat_sectors;
    volume->data_lba = volume->root_lba + root_sectors;
    volume->root_sectors = root_sectors;
    volume->cluster_count = cluster_count;
    volume->sectors_per_cluster = sectors_per_cluster;
    volume->sectors_per_cluster_shift = cluster_shift;
    volume->fat32 = cluster_count >= 65525u;
    volume->root_cluster = volume->fat32 ? read_le32(&sector[44]) & 0x0fffffffu : 0u;
    if (volume->fat32 && volume->root_cluster < 2u)
        return FAT_BOOT_ERR_FILESYSTEM;
    return FAT_BOOT_OK;
}

static int cluster_valid(const FatVolume *volume, uint32_t cluster)
{
    return cluster >= 2u && cluster < volume->cluster_count + 2u;
}

static uint32_t cluster_lba(const FatVolume *volume, uint32_t cluster)
{
    return volume->data_lba +
           ((cluster - 2u) << volume->sectors_per_cluster_shift);
}

static int next_cluster(const FatBootIo *io, uint8_t *sector,
                        const FatVolume *volume, uint32_t cluster,
                        uint32_t *next, int *end)
{
    uint32_t entry_size = volume->fat32 ? 4u : 2u;
    uint32_t offset = cluster * entry_size;
    if (!read_sector(io, volume->fat_lba + (offset >> 9), sector))
        return FAT_BOOT_ERR_IO;

    uint32_t value = volume->fat32 ?
        read_le32(&sector[offset & 511u]) & 0x0fffffffu :
        read_le16(&sector[offset & 511u]);
    uint32_t end_marker = volume->fat32 ? 0x0ffffff8u : 0xfff8u;
    *end = value >= end_marker;
    *next = value;
    if (!*end && !cluster_valid(volume, value)) return FAT_BOOT_ERR_CHAIN;
    return FAT_BOOT_OK;
}

static int name_matches(const uint8_t *entry)
{
    static const uint8_t expected[11] = {
        'A', 'S', 'T', 'R', 'A', '6', '8', ' ', 'R', 'O', 'M'
    };
    for (unsigned index = 0; index < 11; ++index)
        if (entry[index] != expected[index]) return 0;
    return 1;
}

static int scan_directory_sector(const uint8_t *sector, int fat32,
                                 FatFile *file, int *finished)
{
    for (unsigned offset = 0; offset < FAT_BOOT_SECTOR_SIZE; offset += 32u) {
        const uint8_t *entry = &sector[offset];
        if (entry[0] == 0x00u) {
            *finished = 1;
            return FAT_BOOT_ERR_NOT_FOUND;
        }
        if (entry[0] == 0xe5u || entry[11] == 0x0fu ||
            (entry[11] & 0x18u) != 0u)
            continue;
        if (!name_matches(entry)) continue;

        file->first_cluster = read_le16(&entry[26]);
        if (fat32)
            file->first_cluster |= (uint32_t)read_le16(&entry[20]) << 16;
        file->size = read_le32(&entry[28]);
        *finished = 1;
        return FAT_BOOT_OK;
    }
    return FAT_BOOT_ERR_NOT_FOUND;
}

static int find_file(const FatBootIo *io, uint8_t *sector,
                     const FatVolume *volume, FatFile *file)
{
    if (!volume->fat32) {
        for (uint32_t index = 0; index < volume->root_sectors; ++index) {
            if (!read_sector(io, volume->root_lba + index, sector))
                return FAT_BOOT_ERR_IO;
            int finished = 0;
            int status = scan_directory_sector(sector, 0, file, &finished);
            if (status == FAT_BOOT_OK || finished) return status;
        }
        return FAT_BOOT_ERR_NOT_FOUND;
    }

    uint32_t cluster = volume->root_cluster;
    for (uint32_t visited = 0; visited < volume->cluster_count; ++visited) {
        if (!cluster_valid(volume, cluster)) return FAT_BOOT_ERR_CHAIN;
        uint32_t lba = cluster_lba(volume, cluster);
        for (uint32_t index = 0; index < volume->sectors_per_cluster; ++index) {
            if (!read_sector(io, lba + index, sector)) return FAT_BOOT_ERR_IO;
            int finished = 0;
            int status = scan_directory_sector(sector, 1, file, &finished);
            if (status == FAT_BOOT_OK || finished) return status;
        }
        uint32_t next;
        int end;
        int status = next_cluster(io, sector, volume, cluster, &next, &end);
        if (status != FAT_BOOT_OK) return status;
        if (end) return FAT_BOOT_ERR_NOT_FOUND;
        cluster = next;
    }
    return FAT_BOOT_ERR_CHAIN;
}

static int validate_header(const uint8_t *header, uint32_t file_size,
                           uint32_t *payload_size, uint32_t *payload_crc)
{
    if (header[0] != 'A' || header[1] != '6' ||
        header[2] != '8' || header[3] != 'R' ||
        read_be16(&header[4]) != 1u ||
        read_be16(&header[6]) != ROM_HEADER_SIZE)
        return FAT_BOOT_ERR_HEADER;

    *payload_size = read_be32(&header[8]);
    *payload_crc = read_be32(&header[12]);
    if (*payload_size < 8u || *payload_size > FAT_BOOT_MAX_PAYLOAD ||
        file_size != ROM_HEADER_SIZE + *payload_size ||
        read_be32(&header[16]) != ROM_BASE ||
        read_be32(&header[20]) != ROM_LOAD_ADDRESS ||
        read_be32(&header[24]) != 0u)
        return FAT_BOOT_ERR_HEADER;

    uint32_t header_crc = ~crc32_update(0xffffffffu, header, 28u);
    if (header_crc != read_be32(&header[28])) return FAT_BOOT_ERR_HEADER;
    return FAT_BOOT_OK;
}

static int load_file(const FatBootIo *io, uint8_t *sector,
                     const FatVolume *volume, const FatFile *file,
                     uint8_t *destination, FatBootResult *result)
{
    if (!cluster_valid(volume, file->first_cluster) || file->size < ROM_HEADER_SIZE)
        return FAT_BOOT_ERR_HEADER;

    uint32_t cluster = file->first_cluster;
    uint32_t file_remaining = file->size;
    uint32_t payload_remaining = 0u;
    uint32_t payload_size = 0u;
    uint32_t payload_expected_crc = 0u;
    uint32_t payload_crc = 0xffffffffu;
    uint32_t destination_offset = 0u;
    int header_seen = 0;

    for (uint32_t visited = 0; visited < volume->cluster_count; ++visited) {
        if (!cluster_valid(volume, cluster)) return FAT_BOOT_ERR_CHAIN;
        uint32_t lba = cluster_lba(volume, cluster);
        for (uint32_t index = 0;
             index < volume->sectors_per_cluster && file_remaining != 0u;
             ++index) {
            if (!read_sector(io, lba + index, sector)) return FAT_BOOT_ERR_IO;
            uint32_t available = file_remaining < FAT_BOOT_SECTOR_SIZE ?
                                 file_remaining : FAT_BOOT_SECTOR_SIZE;
            uint32_t source_offset = 0u;
            if (!header_seen) {
                int status = validate_header(sector, file->size, &payload_size,
                                             &payload_expected_crc);
                if (status != FAT_BOOT_OK) return status;
                payload_remaining = payload_size;
                source_offset = ROM_HEADER_SIZE;
                header_seen = 1;
            }

            uint32_t copy_size = available - source_offset;
            if (copy_size > payload_remaining) copy_size = payload_remaining;
            for (uint32_t byte = 0; byte < copy_size; ++byte)
                destination[destination_offset + byte] = sector[source_offset + byte];
            payload_crc = crc32_update(payload_crc, &sector[source_offset], copy_size);
            destination_offset += copy_size;
            payload_remaining -= copy_size;
            file_remaining -= available;
        }
        if (file_remaining == 0u) break;

        uint32_t next;
        int end;
        int status = next_cluster(io, sector, volume, cluster, &next, &end);
        if (status != FAT_BOOT_OK) return status;
        if (end) return FAT_BOOT_ERR_CHAIN;
        cluster = next;
    }

    if (!header_seen || file_remaining != 0u || payload_remaining != 0u ||
        destination_offset != payload_size)
        return FAT_BOOT_ERR_CHAIN;
    payload_crc = ~payload_crc;
    if (payload_crc != payload_expected_crc) return FAT_BOOT_ERR_CRC;

    uint32_t initial_sp = read_be32(destination);
    uint32_t initial_pc = read_be32(&destination[4]);
    if (initial_sp <= STACK_MIN || initial_sp > STACK_MAX ||
        initial_pc < ROM_BASE || initial_pc >= ROM_LIMIT)
        return FAT_BOOT_ERR_VECTORS;

    result->payload_size = payload_size;
    result->payload_crc32 = payload_crc;
    result->initial_sp = initial_sp;
    result->initial_pc = initial_pc;
    return FAT_BOOT_OK;
}

int fat_boot_load(const FatBootIo *io, uint8_t sector[FAT_BOOT_SECTOR_SIZE],
                  uint8_t *destination, FatBootResult *result)
{
    FatVolume volume;
    FatFile file;
    int status = mount_volume(io, sector, &volume);
    if (status != FAT_BOOT_OK) return status;
    status = find_file(io, sector, &volume, &file);
    if (status != FAT_BOOT_OK) return status;
    return load_file(io, sector, &volume, &file, destination, result);
}

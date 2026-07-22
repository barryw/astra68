#include "astra_partition.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define TEST_SECTORS 128u

static uint8_t image[TEST_SECTORS][ASTRA_SECTOR_BYTES];

static void write_le32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
    data[2] = (uint8_t)(value >> 16);
    data[3] = (uint8_t)(value >> 24);
}

static void write_le64(uint8_t *data, uint64_t value)
{
    write_le32(data, (uint32_t)value);
    write_le32(data + 4, (uint32_t)(value >> 32));
}

static uint32_t crc32_bytes(const uint8_t *data, size_t size)
{
    uint32_t crc = 0xffffffffu;
    for (size_t index = 0; index < size; ++index) {
        crc ^= data[index];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

static bool read_sector(void *context, uint64_t lba,
                        uint8_t sector[ASTRA_SECTOR_BYTES])
{
    (void)context;
    if (lba >= TEST_SECTORS)
        return false;
    memcpy(sector, image[lba], ASTRA_SECTOR_BYTES);
    return true;
}

static void update_crcs(void)
{
    write_le32(image[1] + 88, crc32_bytes(image[2], 4 * 128));
    write_le32(image[1] + 16, 0);
    write_le32(image[1] + 16, crc32_bytes(image[1], 92));
}

static void build_gpt(unsigned astra_entries)
{
    static const uint8_t type_guid[16] = {
        0x04, 0x11, 0x99, 0x1a, 0x17, 0x93, 0xfd, 0x4c,
        0xb5, 0xeb, 0x04, 0x02, 0x47, 0x15, 0x70, 0xac,
    };
    memset(image, 0, sizeof(image));
    image[0][446 + 4] = 0xee;
    write_le32(image[0] + 446 + 8, 1);
    write_le32(image[0] + 446 + 12, TEST_SECTORS - 1);
    image[0][510] = 0x55;
    image[0][511] = 0xaa;

    memcpy(image[1], "EFI PART", 8);
    write_le32(image[1] + 8, 0x00010000);
    write_le32(image[1] + 12, 92);
    write_le64(image[1] + 24, 1);
    write_le64(image[1] + 32, TEST_SECTORS - 1);
    write_le64(image[1] + 40, 34);
    write_le64(image[1] + 48, TEST_SECTORS - 34);
    write_le64(image[1] + 72, 2);
    write_le32(image[1] + 80, 4);
    write_le32(image[1] + 84, 128);

    for (unsigned index = 0; index < astra_entries; ++index) {
        uint8_t *entry = image[2] + index * 128;
        memcpy(entry, type_guid, sizeof(type_guid));
        write_le64(entry + 32, 40 + index * 20);
        write_le64(entry + 40, 55 + index * 20);
    }
    update_crcs();
}

int main(void)
{
    astra_partition_t partition;

    build_gpt(1);
    assert(astra_partition_find(read_sector, NULL, TEST_SECTORS,
                                &partition) == ASTRA_PARTITION_OK);
    assert(partition.first_lba == 40 && partition.sector_count == 16);

    build_gpt(0);
    assert(astra_partition_find(read_sector, NULL, TEST_SECTORS,
                                &partition) == ASTRA_PARTITION_NOT_FOUND);

    build_gpt(2);
    assert(astra_partition_find(read_sector, NULL, TEST_SECTORS,
                                &partition) == ASTRA_PARTITION_DUPLICATE);

    build_gpt(1);
    image[2][128] = 1;
    write_le64(image[2] + 128 + 32, 60);
    write_le64(image[2] + 128 + 40, 70);
    update_crcs();
    assert(astra_partition_find(read_sector, NULL, TEST_SECTORS,
                                &partition) == ASTRA_PARTITION_OK);

    build_gpt(1);
    image[2][128] = 1;
    write_le64(image[2] + 128 + 32, 50);
    write_le64(image[2] + 128 + 40, 70);
    update_crcs();
    assert(astra_partition_find(read_sector, NULL, TEST_SECTORS,
                                &partition) == ASTRA_PARTITION_CORRUPT);

    build_gpt(1);
    image[2][128] = 1;
    write_le64(image[2] + 128 + 32, 20);
    write_le64(image[2] + 128 + 40, 30);
    update_crcs();
    assert(astra_partition_find(read_sector, NULL, TEST_SECTORS,
                                &partition) == ASTRA_PARTITION_CORRUPT);

    build_gpt(1);
    image[1][40] ^= 1;
    assert(astra_partition_find(read_sector, NULL, TEST_SECTORS,
                                &partition) == ASTRA_PARTITION_CORRUPT);

    build_gpt(1);
    image[2][64] ^= 1;
    assert(astra_partition_find(read_sector, NULL, TEST_SECTORS,
                                &partition) == ASTRA_PARTITION_CORRUPT);

    build_gpt(1);
    write_le32(image[1] + 80, ASTRA_GPT_MAX_ENTRIES + 1u);
    write_le32(image[1] + 16, 0);
    write_le32(image[1] + 16, crc32_bytes(image[1], 92));
    assert(astra_partition_find(read_sector, NULL, TEST_SECTORS,
                                &partition) == ASTRA_PARTITION_UNSUPPORTED);

    partition.first_lba = 40;
    partition.sector_count = 16;
    uint32_t absolute_lba = 0;
    assert(astra_partition_u32_addressable(&partition));
    assert(astra_partition_translate_u32(&partition, 15, 1,
                                         &absolute_lba));
    assert(absolute_lba == 55);
    assert(!astra_partition_translate_u32(&partition, 15, 2,
                                          &absolute_lba));
    partition.first_lba = UINT32_MAX - 1u;
    partition.sector_count = 2;
    assert(astra_partition_u32_addressable(&partition));
    assert(astra_partition_translate_u32(&partition, 0, 2,
                                         &absolute_lba));
    partition.sector_count = 3;
    assert(!astra_partition_u32_addressable(&partition));

    memset(image, 0, sizeof(image));
    assert(astra_partition_find(read_sector, NULL, TEST_SECTORS,
                                &partition) == ASTRA_PARTITION_NOT_GPT);

    puts("Astra GPT partition parser tests passed");
    return 0;
}

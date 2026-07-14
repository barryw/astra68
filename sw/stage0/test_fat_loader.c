#include "fat_loader.h"

#include <stdio.h>
#include <string.h>

#define PARTITION_LBA 2048u
#define RESERVED_SECTORS 32u
#define FAT_SECTORS 600u
#define FAT_LBA (PARTITION_LBA + RESERVED_SECTORS)
#define DATA_LBA (FAT_LBA + FAT_SECTORS)
#define PAYLOAD_SIZE 1024u
#define IMAGE_SIZE (32u + PAYLOAD_SIZE)

typedef struct {
    uint8_t image[IMAGE_SIZE];
    int corrupt_payload;
} TestDisk;

static void write_le16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
}

static void write_le32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
    data[2] = (uint8_t)(value >> 16);
    data[3] = (uint8_t)(value >> 24);
}

static void write_be16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)(value >> 8);
    data[1] = (uint8_t)value;
}

static void write_be32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)(value >> 24);
    data[1] = (uint8_t)(value >> 16);
    data[2] = (uint8_t)(value >> 8);
    data[3] = (uint8_t)value;
}

static uint32_t crc32_bytes(const uint8_t *data, uint32_t size)
{
    uint32_t crc = 0xffffffffu;
    while (size-- != 0u) {
        crc ^= *data++;
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

static void build_image(TestDisk *disk)
{
    memset(disk, 0, sizeof(*disk));
    uint8_t *payload = &disk->image[32];
    write_be32(&payload[0], 0x02000000u);
    write_be32(&payload[4], 0xffe00400u);
    for (uint32_t index = 8; index < PAYLOAD_SIZE; ++index)
        payload[index] = (uint8_t)(index * 29u);

    uint8_t *header = disk->image;
    memcpy(header, "A68R", 4);
    write_be16(&header[4], 1u);
    write_be16(&header[6], 32u);
    write_be32(&header[8], PAYLOAD_SIZE);
    write_be32(&header[12], crc32_bytes(payload, PAYLOAD_SIZE));
    write_be32(&header[16], 0xffe00000u);
    write_be32(&header[20], 0x03e00000u);
    write_be32(&header[24], 0u);
    write_be32(&header[28], crc32_bytes(header, 28u));
}

static int read_test_sector(void *context, uint32_t lba, uint8_t *data)
{
    TestDisk *disk = context;
    memset(data, 0, FAT_BOOT_SECTOR_SIZE);

    if (lba == 0u) {
        data[446 + 4] = 0x0cu;
        write_le32(&data[446 + 8], PARTITION_LBA);
        write_le32(&data[446 + 12], 70000u);
        data[510] = 0x55u;
        data[511] = 0xaau;
        return 0;
    }
    if (lba == PARTITION_LBA) {
        data[0] = 0xebu;
        data[2] = 0x90u;
        memcpy(&data[3], "ASTRA68 ", 8);
        write_le16(&data[11], 512u);
        data[13] = 1u;
        write_le16(&data[14], RESERVED_SECTORS);
        data[16] = 1u;
        write_le16(&data[17], 0u);
        write_le32(&data[32], 70000u);
        write_le32(&data[36], FAT_SECTORS);
        write_le32(&data[44], 2u);
        data[510] = 0x55u;
        data[511] = 0xaau;
        return 0;
    }
    if (lba == FAT_LBA) {
        write_le32(&data[0], 0x0ffffff8u);
        write_le32(&data[4], 0xffffffffu);
        write_le32(&data[8], 0x0fffffffu); // root cluster 2
        write_le32(&data[12], 4u);         // file clusters 3 -> 4 -> 5
        write_le32(&data[16], 5u);
        write_le32(&data[20], 0x0fffffffu);
        return 0;
    }
    if (lba == DATA_LBA) {
        memcpy(data, "ASTRA68 ROM", 11);
        data[11] = 0x20u;
        write_le16(&data[20], 0u);
        write_le16(&data[26], 3u);
        write_le32(&data[28], IMAGE_SIZE);
        return 0;
    }
    if (lba >= DATA_LBA + 1u && lba <= DATA_LBA + 3u) {
        uint32_t offset = (lba - (DATA_LBA + 1u)) * FAT_BOOT_SECTOR_SIZE;
        uint32_t count = IMAGE_SIZE - offset;
        if (count > FAT_BOOT_SECTOR_SIZE) count = FAT_BOOT_SECTOR_SIZE;
        memcpy(data, &disk->image[offset], count);
        if (disk->corrupt_payload && lba == DATA_LBA + 2u) data[17] ^= 0x80u;
        return 0;
    }
    return -1;
}

static int test_valid_image(void)
{
    TestDisk disk;
    uint8_t sector[FAT_BOOT_SECTOR_SIZE];
    uint8_t destination[FAT_BOOT_MAX_PAYLOAD];
    FatBootResult result;
    build_image(&disk);
    memset(destination, 0xa5, sizeof(destination));
    FatBootIo io = {read_test_sector, &disk};

    int status = fat_boot_load(&io, sector, destination, &result);
    if (status != FAT_BOOT_OK) return printf("valid image status=%d\n", status), 1;
    if (result.payload_size != PAYLOAD_SIZE || result.initial_sp != 0x02000000u ||
        result.initial_pc != 0xffe00400u)
        return printf("result metadata mismatch\n"), 1;
    if (memcmp(destination, &disk.image[32], PAYLOAD_SIZE) != 0)
        return printf("payload mismatch\n"), 1;
    return 0;
}

static int test_crc_failure(void)
{
    TestDisk disk;
    uint8_t sector[FAT_BOOT_SECTOR_SIZE];
    uint8_t destination[FAT_BOOT_MAX_PAYLOAD];
    FatBootResult result;
    build_image(&disk);
    disk.corrupt_payload = 1;
    FatBootIo io = {read_test_sector, &disk};

    int status = fat_boot_load(&io, sector, destination, &result);
    if (status != FAT_BOOT_ERR_CRC)
        return printf("corrupt image status=%d\n", status), 1;
    return 0;
}

int main(void)
{
    if (test_valid_image() || test_crc_failure()) return 1;
    puts("PASS stage0 FAT32 loader and ROM CRC");
    return 0;
}

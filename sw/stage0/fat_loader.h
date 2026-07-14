#ifndef ASTRA_STAGE0_FAT_LOADER_H
#define ASTRA_STAGE0_FAT_LOADER_H

#include <stdint.h>

#define FAT_BOOT_SECTOR_SIZE 512u
#define FAT_BOOT_MAX_PAYLOAD 0x00040000u

typedef int (*fat_boot_read_sector_fn)(void *context, uint32_t lba,
                                       uint8_t *data);

typedef struct {
    fat_boot_read_sector_fn read_sector;
    void *context;
} FatBootIo;

typedef struct {
    uint32_t payload_size;
    uint32_t payload_crc32;
    uint32_t initial_sp;
    uint32_t initial_pc;
} FatBootResult;

enum {
    FAT_BOOT_OK = 0,
    FAT_BOOT_ERR_IO = 1,
    FAT_BOOT_ERR_PARTITION = 2,
    FAT_BOOT_ERR_FILESYSTEM = 3,
    FAT_BOOT_ERR_FAT12 = 4,
    FAT_BOOT_ERR_NOT_FOUND = 5,
    FAT_BOOT_ERR_CHAIN = 6,
    FAT_BOOT_ERR_HEADER = 7,
    FAT_BOOT_ERR_CRC = 8,
    FAT_BOOT_ERR_VECTORS = 9,
};

int fat_boot_load(const FatBootIo *io, uint8_t sector[FAT_BOOT_SECTOR_SIZE],
                  uint8_t *destination, FatBootResult *result);

#endif

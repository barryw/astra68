#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ASTRA_SECTOR_BYTES 512u
#define ASTRA_GPT_MAX_ENTRIES 4096u
#define ASTRA_PARTITION_TYPE_GUID "1A991104-9317-4CFD-B5EB-0402471570AC"

typedef bool (*astra_partition_read_fn)(void *context, uint64_t lba,
                                        uint8_t sector[ASTRA_SECTOR_BYTES]);

typedef struct {
    uint64_t first_lba;
    uint64_t sector_count;
} astra_partition_t;

typedef enum {
    ASTRA_PARTITION_OK = 0,
    ASTRA_PARTITION_NOT_GPT,
    ASTRA_PARTITION_NOT_FOUND,
    ASTRA_PARTITION_IO_ERROR,
    ASTRA_PARTITION_CORRUPT,
    ASTRA_PARTITION_UNSUPPORTED,
    ASTRA_PARTITION_DUPLICATE,
} astra_partition_result_t;

astra_partition_result_t astra_partition_find(
    astra_partition_read_fn read_sector, void *context,
    uint64_t media_sectors, astra_partition_t *partition);

// ESP-IDF's SDMMC/SDSPI sector APIs take a 32-bit absolute sector number.
// Keep that backend limit explicit instead of truncating a valid GPT LBA.
bool astra_partition_u32_addressable(const astra_partition_t *partition);
bool astra_partition_translate_u32(const astra_partition_t *partition,
                                   uint64_t relative_lba,
                                   uint32_t sector_count,
                                   uint32_t *absolute_lba);

const char *astra_partition_result_string(astra_partition_result_t result);

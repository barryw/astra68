#ifndef ASTRA_BOOT_LZ4_LEGACY_H
#define ASTRA_BOOT_LZ4_LEGACY_H

#include <stdint.h>

typedef enum {
    ASTRA_LZ4_OK = 0,
    ASTRA_LZ4_BAD_ARGUMENT,
    ASTRA_LZ4_BAD_MAGIC,
    ASTRA_LZ4_BAD_FRAME,
    ASTRA_LZ4_SOURCE_TRUNCATED,
    ASTRA_LZ4_OUTPUT_OVERFLOW,
    ASTRA_LZ4_BAD_OFFSET,
    ASTRA_LZ4_SIZE_MISMATCH
} AstraLz4Result;

AstraLz4Result astra_lz4_legacy_decode(
    const uint8_t *source, uint32_t source_size,
    uint8_t *destination, uint32_t destination_capacity,
    uint32_t expected_size);

#endif

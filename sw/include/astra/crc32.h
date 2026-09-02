#ifndef ASTRA_CRC32_H
#define ASTRA_CRC32_H

#include <stdint.h>

/*
 * Standard CRC-32 (reflected, polynomial 0xEDB88320), the same value
 * `zlib.crc32` and the ROM packaging tools produce.
 */
uint32_t astra_crc32(const void *data, uint32_t size);
uint32_t astra_crc32_update(uint32_t crc, const void *data, uint32_t size);

#endif

#ifndef ASTRA_CRC32_H
#define ASTRA_CRC32_H

#include <stdint.h>

/*
 * Standard CRC-32 (reflected, polynomial 0xEDB88320), the same value
 * `zlib.crc32` and the ROM packaging tools produce.
 *
 * `sw/stage0` carries its own bitwise copy. Sharing this one would change the
 * stage-0 BRAM image, which means a new bitstream and a full timing-closure
 * re-qualification; that is not a trade a duplicate 8-line loop justifies.
 */
uint32_t astra_crc32(const void *data, uint32_t size);
uint32_t astra_crc32_update(uint32_t crc, const void *data, uint32_t size);

#endif

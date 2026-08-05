#include <astra/crc32.h>

/*
 * Nibble-table CRC-32. The bitwise form costs about eight shift/xor rounds per
 * byte, and firmware now checksums the whole decompressed kernel on every boot;
 * a 64-byte table turns that into two lookups per byte for a table small enough
 * that the ROM does not notice it.
 */
static const uint32_t crc32_nibble[16] = {
    0x00000000u, 0x1db71064u, 0x3b6e20c8u, 0x26d930acu,
    0x76dc4190u, 0x6b6b51f4u, 0x4db26158u, 0x5005713cu,
    0xedb88320u, 0xf00f9344u, 0xd6d6a3e8u, 0xcb61b38cu,
    0x9b64c2b0u, 0x86d3d2d4u, 0xa00ae278u, 0xbdbdf21cu
};

uint32_t astra_crc32_update(uint32_t crc, const void *data, uint32_t size)
{
    const uint8_t *bytes = data;

    while (size-- != 0u) {
        crc ^= *bytes++;
        crc = (crc >> 4) ^ crc32_nibble[crc & 0x0fu];
        crc = (crc >> 4) ^ crc32_nibble[crc & 0x0fu];
    }
    return crc;
}

uint32_t astra_crc32(const void *data, uint32_t size)
{
    return ~astra_crc32_update(0xffffffffu, data, size);
}

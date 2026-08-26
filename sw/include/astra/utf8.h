#ifndef ASTRA_UTF8_H
#define ASTRA_UTF8_H

#include <stdint.h>

/* Decode one scalar. Invalid or incomplete input consumes one byte. */
static inline uint32_t astra_utf8_decode(const void *text, uint32_t length,
                                         uint32_t *consumed)
{
    const uint8_t *bytes = text;
    uint8_t first;

    if (bytes == 0 || length == 0u || consumed == 0)
        return 0xfffdu;
    first = bytes[0];
    *consumed = 1u;
    if (first < 0x80u)
        return first;
    if (first >= 0xc2u && first <= 0xdfu && length >= 2u &&
        (bytes[1] & 0xc0u) == 0x80u) {
        *consumed = 2u;
        return ((uint32_t)(first & 0x1fu) << 6u) |
               (bytes[1] & 0x3fu);
    }
    if (first >= 0xe0u && first <= 0xefu && length >= 3u &&
        (bytes[1] & 0xc0u) == 0x80u && (bytes[2] & 0xc0u) == 0x80u &&
        !(first == 0xe0u && bytes[1] < 0xa0u) &&
        !(first == 0xedu && bytes[1] >= 0xa0u)) {
        *consumed = 3u;
        return ((uint32_t)(first & 0x0fu) << 12u) |
               ((uint32_t)(bytes[1] & 0x3fu) << 6u) |
               (bytes[2] & 0x3fu);
    }
    if (first >= 0xf0u && first <= 0xf4u && length >= 4u &&
        (bytes[1] & 0xc0u) == 0x80u && (bytes[2] & 0xc0u) == 0x80u &&
        (bytes[3] & 0xc0u) == 0x80u &&
        !(first == 0xf0u && bytes[1] < 0x90u) &&
        !(first == 0xf4u && bytes[1] >= 0x90u)) {
        *consumed = 4u;
        return ((uint32_t)(first & 0x07u) << 18u) |
               ((uint32_t)(bytes[1] & 0x3fu) << 12u) |
               ((uint32_t)(bytes[2] & 0x3fu) << 6u) |
               (bytes[3] & 0x3fu);
    }
    return 0xfffdu;
}

/* Encode one Unicode scalar. Invalid scalar values become U+FFFD. */
static inline uint32_t astra_utf8_encode(uint32_t scalar, void *output)
{
    uint8_t *bytes = output;

    if (bytes == 0)
        return 0u;
    if (scalar > 0x10ffffu ||
        (scalar >= 0xd800u && scalar <= 0xdfffu))
        scalar = 0xfffdu;
    if (scalar <= 0x7fu) {
        bytes[0] = (uint8_t)scalar;
        return 1u;
    }
    if (scalar <= 0x7ffu) {
        bytes[0] = (uint8_t)(0xc0u | scalar >> 6u);
        bytes[1] = (uint8_t)(0x80u | (scalar & 0x3fu));
        return 2u;
    }
    if (scalar <= 0xffffu) {
        bytes[0] = (uint8_t)(0xe0u | scalar >> 12u);
        bytes[1] = (uint8_t)(0x80u | ((scalar >> 6u) & 0x3fu));
        bytes[2] = (uint8_t)(0x80u | (scalar & 0x3fu));
        return 3u;
    }
    bytes[0] = (uint8_t)(0xf0u | scalar >> 18u);
    bytes[1] = (uint8_t)(0x80u | ((scalar >> 12u) & 0x3fu));
    bytes[2] = (uint8_t)(0x80u | ((scalar >> 6u) & 0x3fu));
    bytes[3] = (uint8_t)(0x80u | (scalar & 0x3fu));
    return 4u;
}

#endif

#include "lz4_legacy.h"

#include <limits.h>
#include <stddef.h>

#define LZ4_LEGACY_MAGIC 0x184c2102u
#define LZ4_MIN_MATCH 4u

static uint32_t read_le32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

static AstraLz4Result extend_length(const uint8_t *source,
                                    uint32_t source_size,
                                    uint32_t *source_offset,
                                    uint32_t *length)
{
    uint32_t extension;

    do {
        if (*source_offset >= source_size)
            return ASTRA_LZ4_SOURCE_TRUNCATED;
        extension = source[(*source_offset)++];
        if (*length > UINT32_MAX - extension)
            return ASTRA_LZ4_BAD_FRAME;
        *length += extension;
    } while (extension == 255u);
    return ASTRA_LZ4_OK;
}

AstraLz4Result astra_lz4_legacy_decode(
    const uint8_t *source, uint32_t source_size,
    uint8_t *destination, uint32_t destination_capacity,
    uint32_t expected_size)
{
    uint32_t source_offset = 8u;
    uint32_t destination_offset = 0u;
    uint32_t block_size;

    if (source == NULL || destination == NULL || expected_size == 0u ||
        expected_size > destination_capacity)
        return ASTRA_LZ4_BAD_ARGUMENT;
    if (source_size < 8u)
        return ASTRA_LZ4_SOURCE_TRUNCATED;
    if (read_le32(source) != LZ4_LEGACY_MAGIC)
        return ASTRA_LZ4_BAD_MAGIC;

    block_size = read_le32(source + 4u);
    if (block_size == 0u || (block_size & 0x80000000u) != 0u ||
        block_size != source_size - 8u)
        return ASTRA_LZ4_BAD_FRAME;

    while (source_offset < source_size) {
        uint8_t token = source[source_offset++];
        uint32_t literal_length = token >> 4;
        uint32_t match_length;
        uint32_t match_offset;
        AstraLz4Result result;

        if (literal_length == 15u) {
            result = extend_length(source, source_size, &source_offset,
                                   &literal_length);
            if (result != ASTRA_LZ4_OK)
                return result;
        }
        if (literal_length > source_size - source_offset)
            return ASTRA_LZ4_SOURCE_TRUNCATED;
        if (literal_length > destination_capacity - destination_offset)
            return ASTRA_LZ4_OUTPUT_OVERFLOW;
        for (uint32_t index = 0u; index < literal_length; ++index)
            destination[destination_offset + index] =
                source[source_offset + index];
        source_offset += literal_length;
        destination_offset += literal_length;

        // The final sequence consists only of literals and has no offset.
        if (source_offset == source_size)
            break;
        if (source_size - source_offset < 2u)
            return ASTRA_LZ4_SOURCE_TRUNCATED;
        match_offset = (uint32_t)source[source_offset] |
                       ((uint32_t)source[source_offset + 1u] << 8);
        source_offset += 2u;
        if (match_offset == 0u || match_offset > destination_offset)
            return ASTRA_LZ4_BAD_OFFSET;

        match_length = (uint32_t)(token & 0x0fu) + LZ4_MIN_MATCH;
        if ((token & 0x0fu) == 15u) {
            result = extend_length(source, source_size, &source_offset,
                                   &match_length);
            if (result != ASTRA_LZ4_OK)
                return result;
        }
        if (match_length > destination_capacity - destination_offset)
            return ASTRA_LZ4_OUTPUT_OVERFLOW;
        for (uint32_t index = 0u; index < match_length; ++index) {
            destination[destination_offset] =
                destination[destination_offset - match_offset];
            ++destination_offset;
        }
    }

    return destination_offset == expected_size ? ASTRA_LZ4_OK :
           ASTRA_LZ4_SIZE_MISMATCH;
}

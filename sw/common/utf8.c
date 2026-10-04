#include <astra/utf8.h>

#include <stddef.h>

static int scalar_at(const uint8_t *bytes, uint32_t available,
                     uint32_t *width)
{
    uint32_t consumed = 0u;
    uint32_t scalar;

    scalar = astra_utf8_decode(bytes, available, &consumed);
    if (consumed == 0u ||
        (scalar == ASTRA_UTF8_REPLACEMENT &&
         !(consumed == 3u && bytes[0] == 0xefu && bytes[1] == 0xbfu &&
           bytes[2] == 0xbdu)))
        return 0;
    *width = consumed;
    return 1;
}

int astra_utf8_validate(const void *text, uint32_t length, uint32_t flags)
{
    const uint8_t *bytes = text;
    uint32_t offset = 0u;

    if ((flags & ~ASTRA_UTF8_ALLOW_NUL) != 0u ||
        (bytes == NULL && length != 0u))
        return 0;
    while (offset < length) {
        uint32_t width;

        if (((flags & ASTRA_UTF8_ALLOW_NUL) == 0u && bytes[offset] == 0u) ||
            !scalar_at(bytes + offset, length - offset, &width))
            return 0;
        offset += width;
    }
    return 1;
}

int astra_utf8_scalar_advance(const void *text, uint32_t length,
                              uint32_t *offset)
{
    const uint8_t *bytes = text;
    uint32_t width;

    if (bytes == NULL || offset == NULL || *offset >= length ||
        !scalar_at(bytes + *offset, length - *offset, &width))
        return 0;
    *offset += width;
    return 1;
}

int astra_utf8_scalar_retreat(const void *text, uint32_t length,
                              uint32_t *offset)
{
    const uint8_t *bytes = text;
    uint32_t start;
    uint32_t width;

    if (bytes == NULL || offset == NULL || *offset == 0u || *offset > length)
        return 0;
    start = *offset - 1u;
    while (start != 0u && (bytes[start] & 0xc0u) == 0x80u)
        --start;
    if (!scalar_at(bytes + start, *offset - start, &width) ||
        start + width != *offset)
        return 0;
    *offset = start;
    return 1;
}

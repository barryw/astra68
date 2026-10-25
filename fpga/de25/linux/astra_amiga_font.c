#include "astra_amiga_font.h"

#include <astra/endian.h>
#include <astra/font_worker.h>

#include <stddef.h>
#include <string.h>

enum {
    HUNK_HEADER = 0x3f3u,
    HUNK_CODE = 0x3e9u,
    HUNK_DATA = 0x3eau,
    HUNK_RELOC32 = 0x3ecu,
    HUNK_END = 0x3f2u,
    DISK_FONT_ID = 0x0f80u,
    HUNK_DATA_OFFSET = 32u,
    DISK_HEADER_OFFSET = 28u,
    DISK_HEADER_BYTES = 114u,
    FONT_HEADER_PAYLOAD_BYTES = DISK_HEADER_BYTES - 4u,
    AMIGA_FLAG_PROPORTIONAL = 0x20u,
    AMIGA_FLAG_REVERSE_PATH = 0x04u,
    AMIGA_STYLE_COLOR = 0x40u,
};

static int span(uint32_t offset, uint32_t size, uint32_t limit)
{
    return offset <= limit && size <= limit - offset;
}

static int hunk_tail_valid(const uint8_t *source, uint32_t length,
                           uint32_t data_bytes)
{
    uint32_t at = HUNK_DATA_OFFSET + data_bytes;

    while (span(at, 4u, length)) {
        uint32_t kind = astra_load_be32(source + at);
        at += 4u;
        if (kind == HUNK_END) return at == length;
        if (kind != HUNK_RELOC32) return 0;
        for (;;) {
            uint32_t count, target;
            if (!span(at, 4u, length)) return 0;
            count = astra_load_be32(source + at);
            at += 4u;
            if (count == 0u) break;
            if (!span(at, 4u, length) || count > (length - at - 4u) / 4u)
                return 0;
            target = astra_load_be32(source + at);
            at += 4u;
            if (target != 0u) return 0;
            for (uint32_t index = 0u; index < count; ++index) {
                uint32_t offset = astra_load_be32(source + at);
                if (!span(offset, 4u, data_bytes)) return 0;
                at += 4u;
            }
        }
    }
    return 0;
}

int astra_amiga_font_parse(const void *source, uint32_t length,
                           AstraAmigaFont *font)
{
    const uint8_t *bytes = source;
    const uint8_t *header, *payload;
    uint32_t data_words, data_bytes, bitmap_at, loc_at, space_at, kern_at;
    uint32_t glyphs, bitmap_bytes, loc_bytes;
    const uint8_t *name_end;
    AstraAmigaFont parsed = {0};

    if (source == NULL || font == NULL || length <
            DISK_HEADER_OFFSET + DISK_HEADER_BYTES + 4u ||
        length > ASTRA_FONT_WORKER_SOURCE_MAX ||
        astra_load_be32(bytes) != HUNK_HEADER ||
        astra_load_be32(bytes + 4u) != 0u ||
        astra_load_be32(bytes + 8u) != 1u ||
        astra_load_be32(bytes + 12u) != 0u ||
        astra_load_be32(bytes + 16u) != 0u ||
        (astra_load_be32(bytes + 24u) != HUNK_CODE &&
         astra_load_be32(bytes + 24u) != HUNK_DATA))
        return 0;
    data_words = astra_load_be32(bytes + 28u);
    if (data_words < (FONT_HEADER_PAYLOAD_BYTES + 3u) / 4u ||
        data_words > (length - HUNK_DATA_OFFSET) / 4u ||
        (astra_load_be32(bytes + 20u) & UINT32_C(0x3fffffff)) < data_words)
        return 0;
    data_bytes = data_words * 4u;
    if (!hunk_tail_valid(bytes, length, data_bytes)) return 0;
    header = bytes + DISK_HEADER_OFFSET;
    payload = bytes + HUNK_DATA_OFFSET;
    if (astra_load_be16(header + 22u) != DISK_FONT_ID) return 0;
    name_end = memchr(header + 30u, '\0', 32u);
    if (name_end == NULL || name_end == header + 30u) return 0;
    memcpy(parsed.name, header + 30u,
           (size_t)(name_end - (header + 30u)));
    parsed.revision = astra_load_be16(header + 24u);
    parsed.height = astra_load_be16(header + 82u);
    parsed.style = header[84u];
    parsed.flags = header[85u];
    parsed.nominal_width = astra_load_be16(header + 86u);
    parsed.baseline = astra_load_be16(header + 88u);
    parsed.first_code = header[94u];
    parsed.last_code = header[95u];
    parsed.modulo = astra_load_be16(header + 100u);
    if (parsed.height == 0u || parsed.height > 256u ||
        parsed.baseline >= parsed.height || parsed.nominal_width == 0u ||
        parsed.modulo == 0u || parsed.first_code > parsed.last_code ||
        (parsed.flags & AMIGA_FLAG_REVERSE_PATH) != 0u ||
        (parsed.style & AMIGA_STYLE_COLOR) != 0u)
        return 0;
    glyphs = (uint32_t)parsed.last_code - parsed.first_code + 2u;
    bitmap_bytes = (uint32_t)parsed.modulo * parsed.height;
    loc_bytes = glyphs * 4u;
    bitmap_at = astra_load_be32(header + 96u);
    loc_at = astra_load_be32(header + 102u);
    space_at = astra_load_be32(header + 106u);
    kern_at = astra_load_be32(header + 110u);
    if (bitmap_at < FONT_HEADER_PAYLOAD_BYTES ||
        loc_at < FONT_HEADER_PAYLOAD_BYTES ||
        !span(bitmap_at, bitmap_bytes, data_bytes) ||
        !span(loc_at, loc_bytes, data_bytes) ||
        (space_at != 0u &&
         (space_at < FONT_HEADER_PAYLOAD_BYTES ||
          !span(space_at, glyphs * 2u, data_bytes))) ||
        (kern_at != 0u &&
         (kern_at < FONT_HEADER_PAYLOAD_BYTES ||
          !span(kern_at, glyphs * 2u, data_bytes))))
        return 0;
    parsed.bitmap = payload + bitmap_at;
    parsed.bitmap_bytes = bitmap_bytes;
    parsed.glyph_count = (uint16_t)glyphs;
    for (uint32_t index = 0u; index < glyphs; ++index) {
        uint32_t position = loc_at + index * 4u;
        uint32_t offset = astra_load_be16(payload + position);
        uint32_t width = astra_load_be16(payload + position + 2u);
        if (offset > (uint32_t)parsed.modulo * 8u ||
            width > (uint32_t)parsed.modulo * 8u - offset)
            return 0;
        parsed.location[index] = (uint16_t)offset;
        parsed.width[index] = (uint16_t)width;
        parsed.spacing[index] =
            (parsed.flags & AMIGA_FLAG_PROPORTIONAL) != 0u && space_at != 0u ?
                (int16_t)astra_load_be16(payload + space_at + index * 2u) :
                (int16_t)parsed.nominal_width;
        parsed.kerning[index] = kern_at == 0u ? 0 :
            (int16_t)astra_load_be16(payload + kern_at + index * 2u);
    }
    *font = parsed;
    return 1;
}

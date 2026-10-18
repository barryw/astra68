#include <astra/afnt.h>
#include <astra/crc32.h>
#include <astra/json.h>
#include <astra/utf8.h>

#include <stddef.h>
#include <string.h>

enum { HEADER_SIZE = 28, ENTRY_SIZE = 20, STRIKE_SIZE = 68,
       GLYPH_SIZE = 32, CHUNK_COUNT = 5 };

static uint16_t be16(const uint8_t *at)
{
    return (uint16_t)((uint16_t)at[0] << 8 | at[1]);
}

static uint32_t be32(const uint8_t *at)
{
    return (uint32_t)at[0] << 24 | (uint32_t)at[1] << 16 |
           (uint32_t)at[2] << 8 | at[3];
}

static void put16(uint8_t *at, uint16_t value)
{
    at[0] = (uint8_t)(value >> 8);
    at[1] = (uint8_t)value;
}

static void put32(uint8_t *at, uint32_t value)
{
    at[0] = (uint8_t)(value >> 24);
    at[1] = (uint8_t)(value >> 16);
    at[2] = (uint8_t)(value >> 8);
    at[3] = (uint8_t)value;
}

int astra_afnt_pack(const AstraAfntChunk chunks[ASTRA_AFNT_CHUNK_COUNT],
                    void *output, uint32_t capacity, uint32_t *length)
{
    static const char names[CHUNK_COUNT][4] = {
        {'N','A','M','E'}, {'C','M','A','P'}, {'S','T','R','K'},
        {'G','L','Y','P'}, {'B','I','T','M'}
    };
    uint8_t *bytes = output;
    uint32_t offset = HEADER_SIZE + CHUNK_COUNT * ENTRY_SIZE;
    if (chunks == NULL || length == NULL) return 0;
    for (uint32_t at = 0; at < CHUNK_COUNT; ++at) {
        if (chunks[at].bytes == NULL || offset > UINT32_MAX - 3u) return 0;
        offset = (offset + 3u) & ~3u;
        if (chunks[at].length > UINT32_MAX - offset) return 0;
        offset += chunks[at].length;
    }
    *length = offset;
    if (bytes == NULL) return 1;
    if (capacity < offset) return 0;
    memset(bytes, 0, offset);
    memcpy(bytes, "AFNT", 4u);
    put16(bytes + 6u, 2u);
    put16(bytes + 8u, HEADER_SIZE);
    put16(bytes + 10u, ENTRY_SIZE);
    put32(bytes + 12u, CHUNK_COUNT);
    put32(bytes + 16u, offset);
    offset = HEADER_SIZE + CHUNK_COUNT * ENTRY_SIZE;
    for (uint32_t at = 0; at < CHUNK_COUNT; ++at) {
        uint8_t *entry = bytes + HEADER_SIZE + at * ENTRY_SIZE;
        offset = (offset + 3u) & ~3u;
        memcpy(entry, names[at], 4u);
        put32(entry + 8u, offset);
        put32(entry + 12u, chunks[at].length);
        put32(entry + 16u, astra_crc32(chunks[at].bytes, chunks[at].length));
        memcpy(bytes + offset, chunks[at].bytes, chunks[at].length);
        offset += chunks[at].length;
    }
    put32(bytes + 20u, astra_crc32(bytes + HEADER_SIZE,
                                   CHUNK_COUNT * ENTRY_SIZE));
    return 1;
}

static int32_t signed32(const uint8_t *at)
{
    uint32_t value = be32(at);
    if (value <= INT32_MAX) return (int32_t)value;
    return -1 - (int32_t)(UINT32_MAX - value);
}

static int span(uint32_t offset, uint32_t size, uint32_t total)
{
    return offset <= total && size <= total - offset;
}

static int chunk_index(const uint8_t *name)
{
    static const char names[CHUNK_COUNT][4] = {
        {'N','A','M','E'}, {'C','M','A','P'}, {'S','T','R','K'},
        {'G','L','Y','P'}, {'B','I','T','M'}
    };
    for (int at = 0; at < CHUNK_COUNT; ++at)
        if (memcmp(name, names[at], 4) == 0) return at;
    return -1;
}

static int bitmap_pitch(uint16_t format, uint16_t width, uint32_t *pitch)
{
    switch (format) {
    case 1: *pitch = ((uint32_t)width + 7u) / 8u; return 1;
    case 2: case 3: *pitch = ((uint32_t)width + 1u) / 2u; return 1;
    case 4: case 5: *pitch = width; return 1;
    default: return 0;
    }
}

int astra_afnt_validate(const void *source, uint32_t length, AstraAfntView *view)
{
    const uint8_t *data = source;
    AstraAfntChunk chunks[CHUNK_COUNT] = {{0}};
    uint32_t ranges[CHUNK_COUNT][2] = {{0}};
    uint32_t cmap_count, strike_count, glyph_count, expected_first = 0;
    uint32_t face_glyphs = 0;
    uint32_t last_scalar = 0, replacement = UINT32_MAX;
    uint8_t present = 0;

    if (data == NULL || view == NULL || length < HEADER_SIZE +
            CHUNK_COUNT * ENTRY_SIZE || memcmp(data, "AFNT", 4) != 0 ||
        be16(data + 4) != 0 || be16(data + 6) != 2 ||
        be16(data + 8) != HEADER_SIZE || be16(data + 10) != ENTRY_SIZE ||
        be32(data + 12) != CHUNK_COUNT || be32(data + 16) != length ||
        be32(data + 24) != 0 ||
        astra_crc32(data + HEADER_SIZE, CHUNK_COUNT * ENTRY_SIZE) != be32(data + 20))
        return 0;

    for (uint32_t at = 0; at < CHUNK_COUNT; ++at) {
        const uint8_t *entry = data + HEADER_SIZE + at * ENTRY_SIZE;
        int index = chunk_index(entry);
        uint32_t offset = be32(entry + 8), size = be32(entry + 12);
        if (index < 0 || (present & (1u << index)) != 0 || be32(entry + 4) != 0 ||
            (offset & 3u) != 0 || offset < HEADER_SIZE + CHUNK_COUNT * ENTRY_SIZE ||
            !span(offset, size, length) ||
            astra_crc32(data + offset, size) != be32(entry + 16))
            return 0;
        for (uint32_t previous = 0; previous < at; ++previous)
            if (offset < ranges[previous][1] && ranges[previous][0] < offset + size)
                return 0;
        chunks[index].bytes = data + offset;
        chunks[index].length = size;
        ranges[at][0] = offset;
        ranges[at][1] = offset + size;
        present |= (uint8_t)(1u << index);
    }
    if (present != (1u << CHUNK_COUNT) - 1u || chunks[0].length == 0u ||
        chunks[0].length > ASTRA_AFNT_NAME_MAX ||
        memchr(chunks[0].bytes, 0, chunks[0].length) != NULL ||
        !astra_json_validate((AstraJsonValue){chunks[0].bytes, chunks[0].length}) ||
        chunks[1].length < 4u || chunks[2].length < 4u ||
        chunks[3].length < 4u)
        return 0;
    uint32_t name_start = 0u;
    while (name_start < chunks[0].length &&
           (chunks[0].bytes[name_start] == ' ' || chunks[0].bytes[name_start] == '\t' ||
            chunks[0].bytes[name_start] == '\r' || chunks[0].bytes[name_start] == '\n'))
        ++name_start;
    if (name_start == chunks[0].length || chunks[0].bytes[name_start] != '{') return 0;

    cmap_count = be32(chunks[1].bytes);
    strike_count = be32(chunks[2].bytes);
    glyph_count = be32(chunks[3].bytes);
    if (cmap_count == 0u || strike_count == 0u || glyph_count == 0u ||
        cmap_count > (chunks[1].length - 4u) / 8u ||
        strike_count > (chunks[2].length - 4u) / STRIKE_SIZE ||
        glyph_count > (chunks[3].length - 4u) / GLYPH_SIZE ||
        chunks[1].length != 4u + cmap_count * 8u ||
        chunks[2].length != 4u + strike_count * STRIKE_SIZE ||
        chunks[3].length != 4u + glyph_count * GLYPH_SIZE)
        return 0;

    for (uint32_t at = 0; at < cmap_count; ++at) {
        const uint8_t *record = chunks[1].bytes + 4u + at * 8u;
        uint32_t scalar = be32(record);
        if ((at != 0u && scalar <= last_scalar) || scalar > 0x10ffffu ||
            (scalar >= 0xd800u && scalar <= 0xdfffu)) return 0;
        if (scalar == 0xfffdu) replacement = be32(record + 4);
        last_scalar = scalar;
    }
    if (replacement == UINT32_MAX) return 0;

    for (uint32_t at = 0; at < strike_count; ++at) {
        const uint8_t *strike = chunks[2].bytes + 4u + at * STRIKE_SIZE;
        uint16_t format = be16(strike + 2), pixel_width = be16(strike + 4);
        uint32_t first = be32(strike + 48), count = be32(strike + 52);
        int32_t max_advance = signed32(strike + 28);
        uint32_t pitch;
        if (be16(strike) != at || !bitmap_pitch(format, 1u, &pitch) ||
            be16(strike + 6) == 0u || signed32(strike + 8) < 0 ||
            signed32(strike + 12) < 0 || signed32(strike + 16) < 0 ||
            signed32(strike + 20) < 0 || signed32(strike + 24) < 0 ||
            max_advance <= 0 || signed32(strike + 36) <= 0 ||
            signed32(strike + 44) <= 0 ||
            (pixel_width != 0u && (uint32_t)max_advance != (uint32_t)pixel_width * 64u) ||
            first != expected_first || count == 0u || count > glyph_count - first ||
            (at != 0u && count != face_glyphs) ||
            be32(strike + 56) != GLYPH_SIZE || be32(strike + 60) != 0u ||
            be32(strike + 64) != 0u || replacement >= count)
            return 0;
        for (uint32_t map = 0; map < cmap_count; ++map)
            if (be32(chunks[1].bytes + 4u + map * 8u + 4u) >= count)
                return 0;
        for (uint32_t glyph = 0; glyph < count; ++glyph) {
            const uint8_t *record = chunks[3].bytes + 4u + (first + glyph) * GLYPH_SIZE;
            uint32_t offset = be32(record + 4), size = be32(record + 8);
            uint16_t width = be16(record + 12), height = be16(record + 14);
            if (be32(record) != glyph || width == 0u || height == 0u ||
                !bitmap_pitch(format, width, &pitch) || be16(record + 16) != pitch ||
                be16(record + 18) != 0u || size != pitch * height ||
                !span(offset, size, chunks[4].length) || signed32(record + 28) < 0)
                return 0;
        }
        if (at == 0u) face_glyphs = count;
        expected_first += count;
    }
    if (expected_first != glyph_count) return 0;
    *view = (AstraAfntView){chunks[0], chunks[1], chunks[2], chunks[3],
                            chunks[4], face_glyphs, strike_count};
    return 1;
}

int astra_afnt_name_string(const AstraAfntView *view, const char *key,
                           char *output, uint32_t capacity)
{
    AstraJsonValue field;
    uint32_t bytes;
    if (view == NULL || key == NULL || key[0] == '\0' ||
        output == NULL || capacity == 0u) return 0;
    output[0] = '\0';
    if (astra_json_member((AstraJsonValue){view->name.bytes, view->name.length},
            key, &field) != ASTRA_JSON_FOUND ||
        !astra_json_string(field, NULL, 0u, &bytes) || bytes == 0u ||
        bytes >= capacity || !astra_json_string(field, output, capacity, &bytes))
        return 0;
    for (uint32_t at = 0u; at < bytes; ++at)
        if ((uint8_t)output[at] < 0x20u || (uint8_t)output[at] == 0x7fu) {
            output[0] = '\0';
            return 0;
        }
    return 1;
}

int astra_afnt_strike_info(const AstraAfntView *view, uint32_t index,
                           AstraAfntStrikeInfo *info)
{
    const uint8_t *strike;
    if (view == NULL || info == NULL || view->strikes.bytes == NULL ||
        index >= view->strike_count || view->strikes.length < 4u ||
        index >= (view->strikes.length - 4u) / STRIKE_SIZE) return 0;
    strike = view->strikes.bytes + 4u + index * STRIKE_SIZE;
    *info = (AstraAfntStrikeInfo){
        .pixel_width = be16(strike + 4u),
        .pixel_height = be16(strike + 6u),
        .bitmap_format = be16(strike + 2u),
        .ascent = signed32(strike + 8u),
        .descent = signed32(strike + 12u),
        .line_gap = signed32(strike + 16u),
        .cap_height = signed32(strike + 20u),
        .x_height = signed32(strike + 24u),
        .max_advance = signed32(strike + 28u),
        .underline_position = signed32(strike + 32u),
        .underline_thickness = signed32(strike + 36u),
        .strikeout_position = signed32(strike + 40u),
        .strikeout_thickness = signed32(strike + 44u)
    };
    return 1;
}

static int cmap_glyph(const AstraAfntView *view, uint32_t count,
                      uint32_t scalar, uint32_t *id)
{
    uint32_t low = 0u, high = count;

    while (low < high) {
        uint32_t middle = low + (high - low) / 2u;
        uint32_t candidate = be32(view->cmap.bytes + 4u + middle * 8u);
        if (candidate < scalar) low = middle + 1u;
        else high = middle;
    }
    if (low == count ||
        be32(view->cmap.bytes + 4u + low * 8u) != scalar)
        return 0;
    *id = be32(view->cmap.bytes + 4u + low * 8u + 4u);
    return 1;
}

int astra_afnt_has_scalar(const AstraAfntView *view, uint32_t scalar)
{
    uint32_t count, id;

    if (view == NULL || view->cmap.bytes == NULL || view->cmap.length < 4u ||
        !astra_unicode_scalar_valid(scalar))
        return 0;
    count = be32(view->cmap.bytes);
    return count != 0u && count <= (view->cmap.length - 4u) / 8u &&
           cmap_glyph(view, count, scalar, &id);
}

int astra_afnt_glyph_info(const AstraAfntView *view, uint32_t strike_index,
                          uint32_t scalar, AstraAfntGlyphInfo *info)
{
    const uint8_t *strike, *glyph;
    uint32_t cmap_count, glyph_count, id, first, count;
    uint32_t offset, length;

    if (view == NULL || info == NULL || view->cmap.bytes == NULL ||
        view->strikes.bytes == NULL || view->glyphs.bytes == NULL ||
        view->bitmap.bytes == NULL || view->cmap.length < 4u ||
        view->strikes.length < 4u || view->glyphs.length < 4u ||
        strike_index >= view->strike_count ||
        strike_index >= (view->strikes.length - 4u) / STRIKE_SIZE)
        return 0;
    cmap_count = be32(view->cmap.bytes);
    glyph_count = be32(view->glyphs.bytes);
    if (cmap_count == 0u || cmap_count > (view->cmap.length - 4u) / 8u ||
        glyph_count == 0u ||
        glyph_count > (view->glyphs.length - 4u) / GLYPH_SIZE)
        return 0;
    if (!cmap_glyph(view, cmap_count, scalar, &id) &&
        !cmap_glyph(view, cmap_count, UINT32_C(0xfffd), &id))
        return 0;
    strike = view->strikes.bytes + 4u + strike_index * STRIKE_SIZE;
    first = be32(strike + 48u);
    count = be32(strike + 52u);
    if (first > glyph_count || count > glyph_count - first || id >= count)
        return 0;
    glyph = view->glyphs.bytes + 4u + (first + id) * GLYPH_SIZE;
    offset = be32(glyph + 4u);
    length = be32(glyph + 8u);
    if (!span(offset, length, view->bitmap.length)) return 0;
    *info = (AstraAfntGlyphInfo){
        .bitmap = view->bitmap.bytes + offset,
        .bitmap_length = length,
        .width = be16(glyph + 12u),
        .height = be16(glyph + 14u),
        .pitch = be16(glyph + 16u),
        .bearing_x = signed32(glyph + 20u),
        .bearing_y = signed32(glyph + 24u),
        .advance_x = signed32(glyph + 28u)
    };
    return 1;
}

#include <astra/afnt.h>
#include <astra/crc32.h>
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
        chunks[0].length > 65536u ||
        memchr(chunks[0].bytes, 0, chunks[0].length) != NULL ||
        chunks[1].length < 4u || chunks[2].length < 4u ||
        chunks[3].length < 4u)
        return 0;

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

static void json_space(const uint8_t *bytes, uint32_t length, uint32_t *at)
{
    while (*at < length && (bytes[*at] == ' ' || bytes[*at] == '\n' ||
                            bytes[*at] == '\r' || bytes[*at] == '\t'))
        ++*at;
}

static int hex(uint8_t value)
{
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

static int json_string(const uint8_t *bytes, uint32_t length, uint32_t *at,
                       uint32_t *first, uint32_t *last)
{
    if (*at >= length || bytes[*at] != '"') return 0;
    *first = ++*at;
    while (*at < length) {
        uint8_t value = bytes[(*at)++];
        if (value == '"') {
            *last = *at - 1u;
            return 1;
        }
        if (value < 0x20u) return 0;
        if (value == '\\') {
            if (*at >= length) return 0;
            value = bytes[(*at)++];
            if (value == 'u') {
                if (length - *at < 4u) return 0;
                for (uint32_t digit = 0u; digit < 4u; ++digit)
                    if (hex(bytes[(*at)++]) < 0) return 0;
            } else if (value != '"' && value != '\\' && value != '/' &&
                       value != 'b' && value != 'f' && value != 'n' &&
                       value != 'r' && value != 't') return 0;
        }
    }
    return 0;
}

static int json_value(const uint8_t *bytes, uint32_t length, uint32_t *at)
{
    uint8_t stack[32];
    uint32_t depth = 0u, first, last;

    if (*at >= length) return 0;
    if (bytes[*at] == '"')
        return json_string(bytes, length, at, &first, &last);
    if (bytes[*at] != '{' && bytes[*at] != '[') {
        first = *at;
        while (*at < length && bytes[*at] != ',' && bytes[*at] != '}' &&
               bytes[*at] != ']') ++*at;
        return *at > first;
    }
    stack[depth++] = bytes[(*at)++];
    while (*at < length) {
        uint8_t value = bytes[(*at)++];
        if (value == '"') {
            --*at;
            if (!json_string(bytes, length, at, &first, &last)) return 0;
        } else if (value == '{' || value == '[') {
            if (depth == sizeof(stack)) return 0;
            stack[depth++] = value;
        } else if (value == '}' || value == ']') {
            if (depth == 0u || (stack[depth - 1u] == '{' ? '}' : ']') != value)
                return 0;
            if (--depth == 0u) return 1;
        }
    }
    return 0;
}

static int json_unicode(const uint8_t *bytes, uint32_t length,
                        uint32_t *at, uint32_t *scalar)
{
    uint32_t value = 0u;
    if (length - *at < 4u) return 0;
    for (uint32_t digit = 0u; digit < 4u; ++digit) {
        int nibble = hex(bytes[(*at)++]);
        if (nibble < 0) return 0;
        value = (value << 4u) | (uint32_t)nibble;
    }
    *scalar = value;
    return 1;
}

static int json_name_decode(const uint8_t *bytes, uint32_t first,
                            uint32_t last, char *output, uint32_t capacity)
{
    uint32_t used = 0u;
    if (output == NULL || capacity == 0u) return 0;
    for (uint32_t at = first; at < last;) {
        uint8_t value = bytes[at++];
        if (value == '\\') {
            uint32_t scalar;
            uint8_t encoded[4];
            uint32_t count;
            if (at >= last) return 0;
            value = bytes[at++];
            if (value == 'u') {
                if (!json_unicode(bytes, last, &at, &scalar)) return 0;
                if (scalar >= 0xd800u && scalar <= 0xdbffu) {
                    uint32_t low;
                    if (last - at < 6u || bytes[at] != '\\' ||
                        bytes[at + 1u] != 'u') return 0;
                    at += 2u;
                    if (!json_unicode(bytes, last, &at, &low) ||
                        low < 0xdc00u || low > 0xdfffu) return 0;
                    scalar = 0x10000u + ((scalar - 0xd800u) << 10u) +
                             low - 0xdc00u;
                }
                if (!astra_unicode_scalar_valid(scalar) || scalar < 0x20u ||
                    scalar == 0x7fu) return 0;
                count = astra_utf8_encode(scalar, encoded);
                if (count > capacity - used - 1u) return 0;
                memcpy(output + used, encoded, count);
                used += count;
                continue;
            }
            if (value != '"' && value != '\\' && value != '/') return 0;
        }
        if (value < 0x20u || value == 0x7fu || used >= capacity - 1u)
            return 0;
        output[used++] = (char)value;
    }
    if (used == 0u) return 0;
    output[used] = '\0';
    for (uint32_t at = 0u; at < used;) {
        uint32_t consumed = 0u;
        uint32_t scalar = astra_utf8_decode(output + at, used - at,
                                             &consumed);
        if (consumed == 0u || (scalar == ASTRA_UTF8_REPLACEMENT &&
            !(consumed == 3u && (uint8_t)output[at] == 0xefu &&
              (uint8_t)output[at + 1u] == 0xbfu &&
              (uint8_t)output[at + 2u] == 0xbdu))) return 0;
        at += consumed;
    }
    return 1;
}

int astra_afnt_name_string(const AstraAfntView *view, const char *key,
                           char *output, uint32_t capacity)
{
    const uint8_t *bytes;
    uint32_t length, at = 0u, first, last;
    int found = 0;

    if (view == NULL || view->name.bytes == NULL || key == NULL ||
        key[0] == '\0' || output == NULL || capacity == 0u) return 0;
    output[0] = '\0';
    bytes = view->name.bytes;
    length = view->name.length;
    json_space(bytes, length, &at);
    if (at >= length || bytes[at++] != '{') return 0;
    for (;;) {
        json_space(bytes, length, &at);
        if (at >= length) return 0;
        if (bytes[at] == '}') {
            ++at;
            json_space(bytes, length, &at);
            return found && at == length;
        }
        if (!json_string(bytes, length, &at, &first, &last)) return 0;
        if (memchr(bytes + first, '\\', last - first) != NULL) return 0;
        json_space(bytes, length, &at);
        if (at >= length || bytes[at++] != ':') return 0;
        json_space(bytes, length, &at);
        if (last - first == strlen(key) &&
            memcmp(bytes + first, key, last - first) == 0) {
            if (found || !json_string(bytes, length, &at, &first, &last) ||
                !json_name_decode(bytes, first, last, output, capacity))
                return 0;
            found = 1;
        } else if (!json_value(bytes, length, &at)) return 0;
        json_space(bytes, length, &at);
        if (at >= length) return 0;
        if (bytes[at] == ',') {
            ++at;
            json_space(bytes, length, &at);
            if (at >= length || bytes[at] == '}') return 0;
            continue;
        }
        if (bytes[at] != '}') return 0;
    }
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

#include "astra_font_convert.h"

#include <astra/afnt.h>
#include <astra/theme.h>
#include <astra/utf8.h>

#include "../../arty/linux/astra_sha256.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FONT_SOURCE_LIMIT (32u * 1024u * 1024u)
#define FONT_OUTPUT_LIMIT (128u * 1024u * 1024u)
#define FONT_GLYPH_LIMIT 131072u
#define FONT_CMAP_LIMIT 131072u

/* System roles request their own sizes; keep import strikes in that contract. */
static const uint16_t system_sizes[] = {
    ASTRA_THEME_SYSTEM_BODY_FONT_HEIGHT,
    ASTRA_THEME_SYSTEM_TITLE_FONT_HEIGHT,
    ASTRA_THEME_SYSTEM_MENU_FONT_HEIGHT,
    20u
};
#define SYSTEM_STRIKES ((uint32_t)(sizeof(system_sizes) / sizeof(system_sizes[0])))

typedef struct Bytes {
    uint8_t *data;
    uint32_t length;
    uint32_t capacity;
} Bytes;

typedef struct MapEntry {
    uint32_t scalar;
    uint32_t glyph;
} MapEntry;

static int grow(Bytes *bytes, uint32_t extra)
{
    uint32_t required, capacity;
    void *replacement;
    if (extra > FONT_OUTPUT_LIMIT - bytes->length) return 0;
    required = bytes->length + extra;
    if (required <= bytes->capacity) return 1;
    capacity = bytes->capacity == 0u ? 1024u : bytes->capacity;
    while (capacity < required) {
        if (capacity > FONT_OUTPUT_LIMIT / 2u) { capacity = FONT_OUTPUT_LIMIT; break; }
        capacity *= 2u;
    }
    if (capacity < required) return 0;
    replacement = realloc(bytes->data, capacity);
    if (replacement == NULL) return 0;
    bytes->data = replacement;
    bytes->capacity = capacity;
    return 1;
}

static int append(Bytes *bytes, const void *source, uint32_t length)
{
    if (!grow(bytes, length)) return 0;
    if (length != 0u) memcpy(bytes->data + bytes->length, source, length);
    bytes->length += length;
    return 1;
}

static int literal(Bytes *bytes, const char *text)
{
    size_t length = strlen(text);
    return length <= UINT32_MAX && append(bytes, text, (uint32_t)length);
}

static int u32(Bytes *bytes, uint32_t value)
{
    uint8_t encoded[4] = {
        (uint8_t)(value >> 24), (uint8_t)(value >> 16),
        (uint8_t)(value >> 8), (uint8_t)value
    };
    return append(bytes, encoded, 4u);
}

static int u16(Bytes *bytes, uint16_t value)
{
    uint8_t encoded[2] = {(uint8_t)(value >> 8), (uint8_t)value};
    return append(bytes, encoded, 2u);
}

static int json_string(Bytes *bytes, const char *text)
{
    static const char digits[] = "0123456789abcdef";
    size_t length;
    if (text == NULL) text = "";
    length = strlen(text);
    if (length > 255u || !astra_utf8_validate(text, (uint32_t)length, 0u) ||
        !literal(bytes, "\"")) return 0;
    for (size_t at = 0; at < length; ++at) {
        unsigned char character = (unsigned char)text[at];
        if (character == '"' || character == '\\') {
            char escaped[] = {'\\', (char)character};
            if (!append(bytes, escaped, 2u)) return 0;
        } else if (character < 0x20u) {
            char escaped[] = {'\\', 'u', '0', '0',
                              digits[character >> 4], digits[character & 15u]};
            if (!append(bytes, escaped, 6u)) return 0;
        } else if (!append(bytes, &character, 1u)) return 0;
    }
    return literal(bytes, "\"");
}

static int map_compare(const void *left, const void *right)
{
    const MapEntry *a = left, *b = right;
    return (a->scalar > b->scalar) - (a->scalar < b->scalar);
}

static int metadata(Bytes *name, FT_Face face, const char *source_name,
                    const char *license, const uint8_t digest[32],
                    uint32_t glyphs, uint32_t face_index)
{
    static const char digits[] = "0123456789abcdef";
    const char *family = face->family_name != NULL ? face->family_name : "Imported Font";
    const char *style = face->style_name != NULL ? face->style_name : "Regular";
    if (!literal(name, "{\"family\":") || !json_string(name, family) ||
        !literal(name, ",\"style\":") || !json_string(name, style) ||
        !literal(name, ",\"license\":") || !json_string(name, license) ||
        !literal(name, ",\"source_encoding\":\"unicode\",\"source_files\":[{\"name\":") ||
        !json_string(name, source_name) || !literal(name, ",\"sha256\":\""))
        return 0;
    for (uint32_t at = 0; at < 32u; ++at) {
        char hex[] = {digits[digest[at] >> 4], digits[digest[at] & 15u]};
        if (!append(name, hex, 2u)) return 0;
    }
    char suffix[192];
    int length = snprintf(suffix, sizeof(suffix),
        "\"}],\"source_face_index\":%u,\"converter\":\"astra-font-worker/0.1\","
        "\"outline_ppem\":[", face_index);
    if (length <= 0 || (size_t)length >= sizeof(suffix) ||
        !append(name, suffix, (uint32_t)length)) return 0;
    for (uint32_t at = 0u; at < SYSTEM_STRIKES; ++at) {
        length = snprintf(suffix, sizeof(suffix), "%s%u",
                          at == 0u ? "" : ",", (unsigned)system_sizes[at]);
        if (length <= 0 || (size_t)length >= sizeof(suffix) ||
            !append(name, suffix, (uint32_t)length)) return 0;
    }
    length = snprintf(suffix, sizeof(suffix),
                      "],\"glyphs_per_strike\":%u}", glyphs);
    return length > 0 && (size_t)length < sizeof(suffix) &&
           append(name, suffix, (uint32_t)length);
}

static int glyph_bitmap(Bytes *glyphs, Bytes *bitmap, FT_Face face,
                        FT_UInt source_glyph, uint32_t dense_glyph,
                        uint32_t *max_advance, int *has_ink)
{
    FT_GlyphSlot slot;
    uint32_t offset = bitmap->length, width, height, pitch, size;
    int32_t bearing_x, bearing_y, advance;
    if (FT_Load_Glyph(face, source_glyph, FT_LOAD_DEFAULT | FT_LOAD_NO_BITMAP) != 0 ||
        FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL) != 0)
        return ASTRA_FONT_CONVERT_INVALID;
    slot = face->glyph;
    if (((slot->bitmap.width != 0u && slot->bitmap.rows != 0u) &&
         (slot->bitmap.pixel_mode != FT_PIXEL_MODE_GRAY ||
          slot->bitmap.num_grays != 256 || slot->bitmap.buffer == NULL)) ||
        slot->bitmap.width > UINT16_MAX ||
        slot->bitmap.rows > UINT16_MAX || slot->bitmap.pitch == INT_MIN ||
        slot->advance.x < 0 || slot->advance.x > INT32_MAX ||
        slot->bitmap_left < INT32_MIN / 64 || slot->bitmap_left > INT32_MAX / 64 ||
        slot->bitmap_top < INT32_MIN / 64 || slot->bitmap_top > INT32_MAX / 64)
        return ASTRA_FONT_CONVERT_UNSUPPORTED;
    width = slot->bitmap.width;
    height = slot->bitmap.rows;
    advance = (int32_t)slot->advance.x;
    bearing_x = slot->bitmap_left * 64;
    bearing_y = slot->bitmap_top * 64;
    if (width == 0u || height == 0u) {
        uint8_t transparent = 0u;
        width = height = pitch = size = 1u;
        bearing_x = bearing_y = 0;
        if (!append(bitmap, &transparent, 1u)) return ASTRA_FONT_CONVERT_NOMEM;
    } else {
        pitch = width;
        size = pitch * height;
        if (size > FONT_OUTPUT_LIMIT - bitmap->length ||
            (uint32_t)abs(slot->bitmap.pitch) < pitch ||
            !grow(bitmap, size)) return ASTRA_FONT_CONVERT_LIMIT;
        for (uint32_t row = 0; row < height; ++row) {
            const uint8_t *source_row = slot->bitmap.buffer +
                (slot->bitmap.pitch >= 0 ? row : height - 1u - row) *
                (uint32_t)abs(slot->bitmap.pitch);
            memcpy(bitmap->data + bitmap->length, source_row, pitch);
            if (has_ink != NULL)
                for (uint32_t column = 0; column < pitch; ++column)
                    *has_ink |= source_row[column] != 0u;
            bitmap->length += pitch;
        }
    }
    if ((uint32_t)advance > *max_advance) *max_advance = (uint32_t)advance;
    if (!u32(glyphs, dense_glyph) || !u32(glyphs, offset) || !u32(glyphs, size) ||
        !u16(glyphs, (uint16_t)width) || !u16(glyphs, (uint16_t)height) ||
        !u16(glyphs, (uint16_t)pitch) || !u16(glyphs, 0u) ||
        !u32(glyphs, (uint32_t)bearing_x) || !u32(glyphs, (uint32_t)bearing_y) ||
        !u32(glyphs, (uint32_t)advance)) return ASTRA_FONT_CONVERT_NOMEM;
    return ASTRA_FONT_CONVERT_OK;
}

static int make_strike(Bytes *strikes, Bytes *glyphs, Bytes *bitmap,
                       FT_Face face, uint16_t ppem, uint16_t strike_id,
                       const uint32_t *source_glyphs, uint32_t count,
                       uint32_t fallback_dense)
{
    uint32_t max_advance = 0u, first = (glyphs->length - 4u) / 32u;
    int fallback_ink = 0;
    FT_Pos ascent, descent, height, line_gap, underline, thickness;
    if (FT_Set_Pixel_Sizes(face, 0u, ppem) != 0) return ASTRA_FONT_CONVERT_INVALID;
    for (uint32_t glyph = 0; glyph < count; ++glyph) {
        int result = glyph_bitmap(glyphs, bitmap, face, source_glyphs[glyph],
            glyph, &max_advance, glyph == fallback_dense ? &fallback_ink : NULL);
        if (result != ASTRA_FONT_CONVERT_OK) return result;
    }
    if (max_advance == 0u || !fallback_ink) return ASTRA_FONT_CONVERT_INVALID;
    ascent = face->size->metrics.ascender;
    descent = -face->size->metrics.descender;
    height = face->size->metrics.height;
    if (ascent < 0 || descent < 0 || ascent > INT32_MAX || descent > INT32_MAX)
        return ASTRA_FONT_CONVERT_INVALID;
    line_gap = height - ascent - descent;
    if (line_gap < 0) line_gap = 0;
    underline = -FT_MulFix(face->underline_position, face->size->metrics.y_scale);
    thickness = FT_MulFix(face->underline_thickness, face->size->metrics.y_scale);
    if (thickness < 64) thickness = 64;
    if (underline < INT32_MIN || underline > INT32_MAX ||
        thickness > INT32_MAX || line_gap > INT32_MAX)
        return ASTRA_FONT_CONVERT_LIMIT;
    if (!u16(strikes, strike_id) || !u16(strikes, 5u) ||
        !u16(strikes, 0u) || !u16(strikes, ppem) ||
        !u32(strikes, (uint32_t)ascent) || !u32(strikes, (uint32_t)descent) ||
        !u32(strikes, (uint32_t)line_gap) || !u32(strikes, 0u) ||
        !u32(strikes, 0u) || !u32(strikes, max_advance) ||
        !u32(strikes, (uint32_t)underline) || !u32(strikes, (uint32_t)thickness) ||
        !u32(strikes, (uint32_t)(ascent / 3)) || !u32(strikes, 64u) ||
        !u32(strikes, first) || !u32(strikes, count) ||
        !u32(strikes, 32u) || !u32(strikes, 0u) || !u32(strikes, 0u))
        return ASTRA_FONT_CONVERT_NOMEM;
    return ASTRA_FONT_CONVERT_OK;
}

int astra_font_convert_outline(const void *source, uint32_t source_length,
    uint32_t face_index, const char *source_name, const char *license,
    uint8_t **output, uint32_t *output_length, uint32_t *face_count)
{
    FT_Library library = NULL;
    FT_Face face = NULL;
    Bytes name = {0}, cmap = {0}, strikes = {0}, glyphs = {0}, bitmap = {0};
    uint32_t *dense_map = NULL, *source_glyphs = NULL;
    MapEntry *entries = NULL;
    uint32_t count = 0u, map_count = 0u, fallback_dense = UINT32_MAX;
    uint8_t digest[32], *packed = NULL;
    uint32_t packed_length = 0u;
    int result = ASTRA_FONT_CONVERT_INVALID;
    AstraAfntView validated;
    if (output == NULL || output_length == NULL || face_count == NULL) return result;
    *output = NULL;
    *output_length = *face_count = 0u;
    if (source == NULL || source_length == 0u || source_length > FONT_SOURCE_LIMIT ||
        source_name == NULL || license == NULL || strlen(source_name) > 255u ||
        strlen(license) > 255u ||
        !astra_utf8_validate(source_name, (uint32_t)strlen(source_name), 0u) ||
        !astra_utf8_validate(license, (uint32_t)strlen(license), 0u))
        return ASTRA_FONT_CONVERT_INVALID;
    if (FT_Init_FreeType(&library) != 0) return ASTRA_FONT_CONVERT_UNSUPPORTED;
    if (FT_New_Memory_Face(library, source, (FT_Long)source_length,
                           (FT_Long)face_index, &face) != 0) goto done;
    if (face->num_faces <= 0 || face->num_faces > 64 ||
        face->num_glyphs <= 0 || face->num_glyphs > FONT_GLYPH_LIMIT ||
        face->num_faces > UINT32_MAX) { result = ASTRA_FONT_CONVERT_LIMIT; goto done; }
    *face_count = (uint32_t)face->num_faces;
    /* shortcut: variable axes need provenance, add FT_Get_MM_Var before enabling. */
    if ((face->face_flags & FT_FACE_FLAG_MULTIPLE_MASTERS) != 0 ||
        (face->face_flags & FT_FACE_FLAG_COLOR) != 0 ||
        FT_Select_Charmap(face, FT_ENCODING_UNICODE) != 0) {
        result = ASTRA_FONT_CONVERT_UNSUPPORTED;
        goto done;
    }
    dense_map = calloc((size_t)face->num_glyphs, sizeof(*dense_map));
    source_glyphs = calloc((size_t)face->num_glyphs, sizeof(*source_glyphs));
    entries = calloc(FONT_CMAP_LIMIT, sizeof(*entries));
    if (dense_map == NULL || source_glyphs == NULL || entries == NULL) {
        result = ASTRA_FONT_CONVERT_NOMEM; goto done;
    }
    FT_UInt source_glyph;
    FT_ULong scalar = FT_Get_First_Char(face, &source_glyph);
    while (source_glyph != 0u) {
        if (scalar <= 0x10ffffu && !(scalar >= 0xd800u && scalar <= 0xdfffu)) {
            if (map_count == FONT_CMAP_LIMIT || source_glyph >= (FT_UInt)face->num_glyphs) {
                result = ASTRA_FONT_CONVERT_LIMIT; goto done;
            }
            if (dense_map[source_glyph] == 0u) {
                if (count == FONT_GLYPH_LIMIT) { result = ASTRA_FONT_CONVERT_LIMIT; goto done; }
                dense_map[source_glyph] = ++count;
                source_glyphs[count - 1u] = source_glyph;
            }
            entries[map_count++] = (MapEntry){(uint32_t)scalar,
                                              dense_map[source_glyph] - 1u};
            if (scalar == 0xfffdu) fallback_dense = dense_map[source_glyph] - 1u;
        }
        scalar = FT_Get_Next_Char(face, scalar, &source_glyph);
    }
    if (fallback_dense == UINT32_MAX) {
        FT_UInt fallback = FT_Get_Char_Index(face, '?');
        if (fallback == 0u || fallback >= (FT_UInt)face->num_glyphs ||
            map_count == FONT_CMAP_LIMIT || dense_map[fallback] == 0u) goto done;
        fallback_dense = dense_map[fallback] - 1u;
        entries[map_count++] = (MapEntry){0xfffdu, fallback_dense};
        qsort(entries, map_count, sizeof(*entries), map_compare);
    }
    if (count == 0u || !u32(&cmap, map_count)) {
        result = ASTRA_FONT_CONVERT_NOMEM; goto done;
    }
    for (uint32_t at = 0; at < map_count; ++at)
        if (!u32(&cmap, entries[at].scalar) || !u32(&cmap, entries[at].glyph)) {
            result = ASTRA_FONT_CONVERT_NOMEM; goto done;
        }
    if (!u32(&strikes, SYSTEM_STRIKES) ||
        !u32(&glyphs, count * SYSTEM_STRIKES)) {
        result = ASTRA_FONT_CONVERT_NOMEM; goto done;
    }
    for (uint16_t at = 0u; at < SYSTEM_STRIKES; ++at) {
        result = make_strike(&strikes, &glyphs, &bitmap, face, system_sizes[at],
                             at, source_glyphs, count, fallback_dense);
        if (result != ASTRA_FONT_CONVERT_OK) goto done;
    }
    AstraSha256 hash;
    astra_sha256_init(&hash);
    astra_sha256_update(&hash, source, source_length);
    astra_sha256_final(&hash, digest);
    if (!metadata(&name, face, source_name, license, digest, count, face_index)) {
        result = ASTRA_FONT_CONVERT_NOMEM; goto done;
    }
    AstraAfntChunk chunks[ASTRA_AFNT_CHUNK_COUNT] = {
        {name.data, name.length}, {cmap.data, cmap.length},
        {strikes.data, strikes.length}, {glyphs.data, glyphs.length},
        {bitmap.data, bitmap.length}
    };
    if (!astra_afnt_pack(chunks, NULL, 0u, &packed_length) ||
        packed_length > FONT_OUTPUT_LIMIT ||
        (packed = malloc(packed_length)) == NULL ||
        !astra_afnt_pack(chunks, packed, packed_length, &packed_length) ||
        !astra_afnt_validate(packed, packed_length, &validated)) {
        result = ASTRA_FONT_CONVERT_LIMIT; goto done;
    }
    *output = packed;
    *output_length = packed_length;
    packed = NULL;
    result = ASTRA_FONT_CONVERT_OK;
done:
    free(packed);
    free(name.data);
    free(cmap.data);
    free(strikes.data);
    free(glyphs.data);
    free(bitmap.data);
    free(dense_map);
    free(source_glyphs);
    free(entries);
    if (face != NULL) FT_Done_Face(face);
    FT_Done_FreeType(library);
    return result;
}

#include "astra_font_convert.h"
#include "astra_amiga_font.h"

#include <astra/afnt.h>
#include <astra/font_worker.h>
#include <astra/theme.h>
#include <astra/utf8.h>

#include <astra/sha256.h>
#include <astra/endian.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MULTIPLE_MASTERS_H
#include FT_SFNT_NAMES_H
#include FT_TRUETYPE_IDS_H
#include FT_TRUETYPE_TABLES_H

#include <limits.h>
#include <iconv.h>
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

static int json_text(Bytes *bytes, const char *text, uint32_t length)
{
    static const char digits[] = "0123456789abcdef";
    if (!astra_utf8_validate(text, length, 0u) ||
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

static int json_string(Bytes *bytes, const char *text)
{
    size_t length = text == NULL ? 0u : strlen(text);
    return length <= 255u && json_text(bytes, text, (uint32_t)length);
}

static int json_hex(Bytes *bytes, const uint8_t *data, uint32_t length)
{
    static const char digits[] = "0123456789abcdef";
    if (!literal(bytes, "\"")) return 0;
    for (uint32_t at = 0u; at < length; ++at) {
        char hex[] = {digits[data[at] >> 4], digits[data[at] & 15u]};
        if (!append(bytes, hex, 2u)) return 0;
    }
    return literal(bytes, "\"");
}

/* Keep original bytes even when a source encoding cannot be decoded. */
static int notice_text(Bytes *bytes, const uint8_t *data, uint32_t length,
                       const char *encoding, const char *key)
{
    iconv_t converter;
    char *output, *write_at, *read_at = (char *)data;
    size_t available, remaining = length;
    int result = 1;
    if (encoding == NULL) return 1;
    if (length > (FONT_OUTPUT_LIMIT - 1u) / 4u) return 0;
    available = (size_t)length * 4u + 1u;
    output = malloc(available);
    if (output == NULL) return 0;
    write_at = output;
    converter = iconv_open("UTF-8", encoding);
    if (converter == (iconv_t)-1) { free(output); return 0; }
    if (iconv(converter, &read_at, &remaining, &write_at, &available) != (size_t)-1 &&
        remaining == 0u &&
        astra_utf8_validate(output, (uint32_t)(write_at - output), 0u))
        result = literal(bytes, key) &&
                 json_text(bytes, output, (uint32_t)(write_at - output));
    iconv_close(converter);
    free(output);
    return result;
}

static int notice_language(Bytes *bytes, FT_Face face, uint16_t language)
{
    uint8_t header[6], record[4];
    FT_ULong size = sizeof(header);
    FT_ULong tag = FT_MAKE_TAG('n', 'a', 'm', 'e');
    uint32_t offset, count, expected;
    uint8_t *text;
    int result;

    /* Public language-tag accessor rejects valid first ID 0x8000. */
    if (FT_Load_Sfnt_Table(face, tag, 0, header, &size) != 0 ||
        size != sizeof(header) || astra_load_be16(header) != 1u) return 1;
    offset = 6u + (uint32_t)astra_load_be16(header + 2u) * 12u;
    size = 2u;
    if (FT_Load_Sfnt_Table(face, tag, offset, record, &size) != 0 ||
        size != 2u) return 1;
    count = astra_load_be16(record);
    if ((uint32_t)language - 0x8000u >= count) return 1;
    offset += 2u + ((uint32_t)language - 0x8000u) * 4u;
    size = sizeof(record);
    if (FT_Load_Sfnt_Table(face, tag, offset, record, &size) != 0 ||
        size != sizeof(record)) return 1;
    size = astra_load_be16(record);
    expected = (uint32_t)size;
    offset = (uint32_t)astra_load_be16(header + 4u) +
             astra_load_be16(record + 2u);
    text = malloc(size == 0u ? 1u : size);
    if (text == NULL) return 0;
    result = 1;
    if ((size == 0u || FT_Load_Sfnt_Table(face, tag, offset, text, &size) == 0) &&
        size == expected)
        result = literal(bytes, ",\"language_tag_raw_hex\":") &&
            json_hex(bytes, text, (uint32_t)size) &&
            notice_text(bytes, text, (uint32_t)size, "UTF-16BE",
                        ",\"language_tag\":");
    free(text);
    return result;
}

static int notices(Bytes *bytes, FT_Face face)
{
    uint32_t count = 0u;
    if (!literal(bytes, ",\"notices\":[")) return 0;
    for (FT_UInt at = 0u; at < FT_Get_Sfnt_Name_Count(face); ++at) {
        FT_SfntName record;
        const char *encoding = NULL;
        char prefix[192];
        int length;
        if (FT_Get_Sfnt_Name(face, at, &record) != 0) return 0;
        if (record.name_id != TT_NAME_ID_COPYRIGHT &&
            record.name_id != TT_NAME_ID_LICENSE &&
            record.name_id != TT_NAME_ID_LICENSE_URL) continue;
        length = snprintf(prefix, sizeof(prefix),
            "%s{\"name_id\":%u,\"platform_id\":%u,\"encoding_id\":%u,"
            "\"language_id\":%u,\"raw_hex\":", count++ == 0u ? "" : ",",
            record.name_id, record.platform_id, record.encoding_id,
            record.language_id);
        if (length <= 0 || (size_t)length >= sizeof(prefix) ||
            !append(bytes, prefix, (uint32_t)length) ||
            !json_hex(bytes, record.string, record.string_len)) return 0;
        if (record.platform_id == TT_PLATFORM_APPLE_UNICODE ||
            (record.platform_id == TT_PLATFORM_MICROSOFT &&
             (record.encoding_id == TT_MS_ID_SYMBOL_CS ||
              record.encoding_id == TT_MS_ID_UNICODE_CS ||
              record.encoding_id == TT_MS_ID_UCS_4))) encoding = "UTF-16BE";
        else if (record.platform_id == TT_PLATFORM_MACINTOSH &&
                 record.encoding_id == TT_MAC_ID_ROMAN) encoding = "MACINTOSH";
        if (!notice_text(bytes, record.string, record.string_len, encoding,
                         ",\"text\":")) return 0;
        if (record.language_id >= 0x8000u &&
            !notice_language(bytes, face, record.language_id)) return 0;
        if (!literal(bytes, "}")) return 0;
    }
    return literal(bytes, "]");
}

static int map_compare(const void *left, const void *right)
{
    const MapEntry *a = left, *b = right;
    return (a->scalar > b->scalar) - (a->scalar < b->scalar);
}

static int metadata(Bytes *name, FT_Face face, const char *source_name,
                    const char *license, const uint8_t digest[32],
                    uint32_t glyphs, uint32_t face_index,
                    uint32_t instance_index, const FT_MM_Var *variation,
                    const FT_Fixed *coordinates)
{
    static const char digits[] = "0123456789abcdef";
    const char *family = face->family_name != NULL ? face->family_name : "Imported Font";
    const char *style = face->style_name != NULL ? face->style_name : "Regular";
    if (!literal(name, "{\"family\":") || !json_string(name, family) ||
        !literal(name, ",\"style\":") || !json_string(name, style) ||
        !literal(name, ",\"license\":") || !json_string(name, license) ||
        !notices(name, face) ||
        !literal(name, ",\"source_encoding\":\"unicode\",\"source_files\":[{\"name\":") ||
        !json_string(name, source_name) || !literal(name, ",\"sha256\":\""))
        return 0;
    for (uint32_t at = 0; at < 32u; ++at) {
        char hex[] = {digits[digest[at] >> 4], digits[digest[at] & 15u]};
        if (!append(name, hex, 2u)) return 0;
    }
    char suffix[192];
    int length = snprintf(suffix, sizeof(suffix),
        "\"}],\"source_face_index\":%u,\"source_instance_index\":%u,"
        "\"variation_axes\":[", face_index, instance_index);
    if (length <= 0 || (size_t)length >= sizeof(suffix) ||
        !append(name, suffix, (uint32_t)length)) return 0;
    if (variation != NULL) {
        for (FT_UInt at = 0u; at < variation->num_axis; ++at) {
            FT_ULong value = variation->axis[at].tag;
            char tag[5] = {(char)(value >> 24), (char)(value >> 16),
                           (char)(value >> 8), (char)value, '\0'};
            if (!literal(name, at == 0u ? "{\"tag\":" : ",{\"tag\":") ||
                !json_string(name, tag)) return 0;
            length = snprintf(suffix, sizeof(suffix),
                ",\"design_16_16\":%ld}", (long)coordinates[at]);
            if (length <= 0 || (size_t)length >= sizeof(suffix) ||
                !append(name, suffix, (uint32_t)length)) return 0;
        }
    }
    if (!literal(name, "],\"converter\":\"astra-font-worker/0.3\","
                       "\"outline_ppem\":[")) return 0;
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

static int open_face(FT_Library library, const void *source,
                     uint32_t source_length, uint32_t selected,
                     FT_Face *selected_face, uint32_t *face_count,
                     uint32_t *file_index, uint32_t *instance_index)
{
    FT_Face face = NULL;
    FT_Long files;
    uint32_t total = 0u, target_file = UINT32_MAX, target_instance = 0u;
    int result = ASTRA_FONT_CONVERT_INVALID;

    if (FT_New_Memory_Face(library, source, (FT_Long)source_length,
                           0, &face) != 0) return result;
    files = face->num_faces;
    FT_Done_Face(face);
    face = NULL;
    if (files <= 0 || files > ASTRA_FONT_WORKER_FACES_MAX)
        return ASTRA_FONT_CONVERT_LIMIT;
    for (FT_Long file = 0; file < files; ++file) {
        FT_MM_Var *variation = NULL;
        uint32_t instances = 1u;
        if (FT_New_Memory_Face(library, source, (FT_Long)source_length,
                               file, &face) != 0) goto done;
        if (selected == total) {
            target_file = (uint32_t)file;
            target_instance = 0u;
        }
        if ((face->face_flags & FT_FACE_FLAG_MULTIPLE_MASTERS) != 0) {
            if (FT_Get_MM_Var(face, &variation) != 0 ||
                variation == NULL || variation->num_axis == 0u ||
                variation->num_axis > 32u || variation->axis == NULL ||
                variation->num_namedstyles >= ASTRA_FONT_WORKER_FACES_MAX ||
                (variation->num_namedstyles != 0u &&
                 variation->namedstyle == NULL)) {
                result = ASTRA_FONT_CONVERT_UNSUPPORTED;
                if (variation != NULL) FT_Done_MM_Var(library, variation);
                goto done;
            }
            for (FT_UInt at = 0u; at < variation->num_namedstyles; ++at) {
                const FT_Fixed *coords = variation->namedstyle[at].coords;
                int duplicate = 1;
                if (coords == NULL) {
                    result = ASTRA_FONT_CONVERT_UNSUPPORTED;
                    FT_Done_MM_Var(library, variation);
                    goto done;
                }
                for (FT_UInt axis = 0u; axis < variation->num_axis; ++axis)
                    if (coords[axis] != variation->axis[axis].def)
                        duplicate = 0;
                for (FT_UInt prior = 0u; !duplicate && prior < at; ++prior) {
                    int same = 1;
                    const FT_Fixed *old = variation->namedstyle[prior].coords;
                    if (old == NULL) continue;
                    for (FT_UInt axis = 0u; axis < variation->num_axis; ++axis)
                        if (coords[axis] != old[axis]) same = 0;
                    if (same) duplicate = 1;
                }
                if (duplicate) continue;
                if (selected == total + instances) {
                    target_file = (uint32_t)file;
                    target_instance = at + 1u;
                }
                ++instances;
            }
        }
        if (instances > ASTRA_FONT_WORKER_FACES_MAX - total) {
            result = ASTRA_FONT_CONVERT_LIMIT;
            if (variation != NULL) FT_Done_MM_Var(library, variation);
            goto done;
        }
        total += instances;
        if (variation != NULL) FT_Done_MM_Var(library, variation);
        FT_Done_Face(face);
        face = NULL;
    }
    *face_count = total;
    if (target_file == UINT32_MAX) goto done;
    if (FT_New_Memory_Face(library, source, (FT_Long)source_length,
        (FT_Long)((target_instance << 16) | target_file), &face) != 0)
        goto done;
    *selected_face = face;
    *file_index = target_file;
    *instance_index = target_instance;
    face = NULL;
    result = ASTRA_FONT_CONVERT_OK;
done:
    if (face != NULL) FT_Done_Face(face);
    return result;
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
    FT_MM_Var *variation = NULL;
    FT_Fixed coordinates[32] = {0};
    Bytes name = {0}, cmap = {0}, strikes = {0}, glyphs = {0}, bitmap = {0};
    uint32_t *dense_map = NULL, *source_glyphs = NULL;
    MapEntry *entries = NULL;
    uint32_t count = 0u, map_count = 0u, fallback_dense = UINT32_MAX;
    uint32_t file_index = 0u, instance_index = 0u;
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
    result = open_face(library, source, source_length, face_index,
                       &face, face_count, &file_index, &instance_index);
    if (result != ASTRA_FONT_CONVERT_OK) goto done;
    if (face->num_glyphs <= 0 || face->num_glyphs > FONT_GLYPH_LIMIT) {
        result = ASTRA_FONT_CONVERT_LIMIT; goto done;
    }
    if ((face->face_flags & FT_FACE_FLAG_MULTIPLE_MASTERS) != 0) {
        if (FT_Get_MM_Var(face, &variation) != 0 || variation == NULL ||
            variation->num_axis == 0u || variation->num_axis > 32u ||
            FT_Get_Var_Design_Coordinates(face, variation->num_axis,
                                           coordinates) != 0) {
            result = ASTRA_FONT_CONVERT_UNSUPPORTED;
            goto done;
        }
    }
    if ((face->face_flags & FT_FACE_FLAG_COLOR) != 0 ||
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
    if (!metadata(&name, face, source_name, license, digest, count,
                  file_index, instance_index, variation, coordinates)) {
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
    if (variation != NULL) FT_Done_MM_Var(library, variation);
    if (face != NULL) FT_Done_Face(face);
    FT_Done_FreeType(library);
    return result;
}

static int amiga_scalar(iconv_t codec, uint8_t code, uint32_t *scalar)
{
    char input = (char)code, output[4];
    char *in = &input, *out = output;
    size_t in_left = 1u, out_left = sizeof(output);
    if (iconv(codec, &in, &in_left, &out, &out_left) == (size_t)-1 ||
        in_left != 0u || out_left != 0u) return 0;
    *scalar = (uint32_t)(uint8_t)output[0] << 24 |
              (uint32_t)(uint8_t)output[1] << 16 |
              (uint32_t)(uint8_t)output[2] << 8 | (uint8_t)output[3];
    return *scalar <= 0x10ffffu &&
           (*scalar < 0xd800u || *scalar > 0xdfffu);
}

static int amiga_metadata(Bytes *name, const AstraAmigaFont *font,
                          const char *encoding, const char *source_name,
                          const char *license, const uint8_t digest[32])
{
    static const char digits[] = "0123456789abcdef";
    char suffix[128];
    int length;
    /* Fall back to UTF-8 filename if legacy family bytes are not UTF-8. */
    const char *family = astra_utf8_validate(font->name,
        (uint32_t)strlen(font->name), 0u) ? font->name : source_name;
    const char *style = (font->style & 0x06u) == 0x06u ? "Bold Italic" :
                        (font->style & 0x02u) != 0u ? "Bold" :
                        (font->style & 0x04u) != 0u ? "Italic" : "Regular";
    if (!literal(name, "{\"family\":") || !json_string(name, family) ||
        !literal(name, ",\"style\":") || !json_string(name, style) ||
        !literal(name, ",\"license\":") ||
        !json_string(name, license) ||
        !literal(name, ",\"source_encoding\":") ||
        !json_string(name, encoding) ||
        !literal(name, ",\"source_files\":[{\"name\":") ||
        !json_string(name, source_name) ||
        !literal(name, ",\"sha256\":\"")) return 0;
    for (uint32_t at = 0u; at < 32u; ++at) {
        char hex[] = {digits[digest[at] >> 4], digits[digest[at] & 15u]};
        if (!append(name, hex, 2u)) return 0;
    }
    length = snprintf(suffix, sizeof(suffix),
        "\"}],\"source_face_index\":0,\"converter\":\"astra-font-worker/0.3\","
        "\"outline_ppem\":[%u],\"glyphs_per_strike\":%u}",
        (unsigned)font->height, (unsigned)font->glyph_count);
    return length > 0 && (size_t)length < sizeof(suffix) &&
           append(name, suffix, (uint32_t)length);
}

int astra_font_convert_amiga(const void *source, uint32_t source_length,
    const char *encoding, const char *source_name, const char *license,
    uint8_t **output, uint32_t *output_length, uint32_t *face_count)
{
    AstraAmigaFont font;
    AstraAfntView validated;
    AstraSha256 hash;
    Bytes name = {0}, cmap = {0}, strikes = {0}, glyphs = {0}, bitmap = {0};
    MapEntry entries[258];
    uint8_t digest[32], *packed = NULL;
    uint32_t packed_length = 0u, map_count = 0u, max_advance = 0u;
    iconv_t codec = (iconv_t)-1;
    int result = ASTRA_FONT_CONVERT_INVALID;

    if (output == NULL || output_length == NULL || face_count == NULL)
        return result;
    *output = NULL;
    *output_length = *face_count = 0u;
    if (source == NULL || encoding == NULL || source_name == NULL ||
        license == NULL || strlen(source_name) > 255u ||
        strlen(license) > 255u ||
        !astra_utf8_validate(source_name, (uint32_t)strlen(source_name), 0u) ||
        !astra_utf8_validate(license, (uint32_t)strlen(license), 0u) ||
        (strcmp(encoding, "ISO-8859-1") != 0 &&
         strcmp(encoding, "ISO-8859-2") != 0) ||
        !astra_amiga_font_parse(source, source_length, &font))
        return result;
    *face_count = 1u;
    codec = iconv_open("UTF-32BE", encoding);
    if (codec == (iconv_t)-1) return ASTRA_FONT_CONVERT_UNSUPPORTED;
    for (uint32_t at = 0u; at + 1u < font.glyph_count; ++at) {
        uint32_t scalar;
        if (!amiga_scalar(codec, (uint8_t)(font.first_code + at), &scalar)) {
            result = ASTRA_FONT_CONVERT_UNSUPPORTED;
            goto done;
        }
        entries[map_count++] = (MapEntry){scalar, at};
    }
    entries[map_count++] = (MapEntry){0xfffdu, font.glyph_count - 1u};
    qsort(entries, map_count, sizeof(entries[0]), map_compare);
    if (!u32(&cmap, map_count)) { result = ASTRA_FONT_CONVERT_NOMEM; goto done; }
    for (uint32_t at = 0u; at < map_count; ++at) {
        if ((at != 0u && entries[at].scalar == entries[at - 1u].scalar) ||
            !u32(&cmap, entries[at].scalar) || !u32(&cmap, entries[at].glyph)) {
            result = ASTRA_FONT_CONVERT_INVALID; goto done;
        }
    }
    if (!u32(&strikes, 1u) || !u32(&glyphs, font.glyph_count)) {
        result = ASTRA_FONT_CONVERT_NOMEM; goto done;
    }
    for (uint32_t at = 0u; at < font.glyph_count; ++at) {
        uint32_t offset = bitmap.length;
        uint32_t width = font.width[at], pitch = (width + 7u) / 8u;
        uint32_t height = font.height, size;
        int32_t advance = (int32_t)font.spacing[at] * 64;
        int32_t bearing_x = (int32_t)font.kerning[at] * 64;
        int32_t bearing_y = (int32_t)(font.baseline + 1u) * 64;
        if (advance < 0) { result = ASTRA_FONT_CONVERT_UNSUPPORTED; goto done; }
        if ((uint32_t)advance > max_advance) max_advance = (uint32_t)advance;
        if (width == 0u) {
            const uint8_t empty = 0u;
            width = height = pitch = size = 1u;
            bearing_x = bearing_y = 0;
            if (!append(&bitmap, &empty, 1u)) {
                result = ASTRA_FONT_CONVERT_NOMEM; goto done;
            }
        } else {
            size = pitch * height;
            if (!grow(&bitmap, size)) { result = ASTRA_FONT_CONVERT_NOMEM; goto done; }
            memset(bitmap.data + bitmap.length, 0, size);
            for (uint32_t row = 0u; row < height; ++row)
                for (uint32_t bit = 0u; bit < width; ++bit) {
                    uint32_t source_bit = font.location[at] + bit;
                    uint8_t source_byte = font.bitmap[row * font.modulo + source_bit / 8u];
                    if ((source_byte & (uint8_t)(0x80u >> (source_bit & 7u))) != 0u)
                        bitmap.data[bitmap.length + row * pitch + bit / 8u] |=
                            (uint8_t)(0x80u >> (bit & 7u));
                }
            bitmap.length += size;
        }
        if (!u32(&glyphs, at) || !u32(&glyphs, offset) || !u32(&glyphs, size) ||
            !u16(&glyphs, (uint16_t)width) || !u16(&glyphs, (uint16_t)height) ||
            !u16(&glyphs, (uint16_t)pitch) || !u16(&glyphs, 0u) ||
            !u32(&glyphs, (uint32_t)bearing_x) ||
            !u32(&glyphs, (uint32_t)bearing_y) ||
            !u32(&glyphs, (uint32_t)advance)) {
            result = ASTRA_FONT_CONVERT_NOMEM; goto done;
        }
    }
    if (max_advance == 0u || font.width[font.glyph_count - 1u] == 0u) {
        result = ASTRA_FONT_CONVERT_INVALID; goto done;
    }
    if (!u16(&strikes, 0u) || !u16(&strikes, 1u) ||
        !u16(&strikes, 0u) || !u16(&strikes, font.height) ||
        !u32(&strikes, (uint32_t)(font.baseline + 1u) * 64u) ||
        !u32(&strikes, (uint32_t)(font.height - font.baseline - 1u) * 64u) ||
        !u32(&strikes, 0u) || !u32(&strikes, 0u) || !u32(&strikes, 0u) ||
        !u32(&strikes, max_advance) || !u32(&strikes, 64u) ||
        !u32(&strikes, 64u) || !u32(&strikes, 64u) || !u32(&strikes, 64u) ||
        !u32(&strikes, 0u) || !u32(&strikes, font.glyph_count) ||
        !u32(&strikes, 32u) || !u32(&strikes, 0u) || !u32(&strikes, 0u)) {
        result = ASTRA_FONT_CONVERT_NOMEM; goto done;
    }
    astra_sha256_init(&hash);
    astra_sha256_update(&hash, source, source_length);
    astra_sha256_final(&hash, digest);
    if (!amiga_metadata(&name, &font, encoding, source_name, license, digest)) {
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
    if (codec != (iconv_t)-1) iconv_close(codec);
    return result;
}

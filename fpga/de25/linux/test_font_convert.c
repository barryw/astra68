#include "astra_font_convert.h"

#include <astra/afnt.h>
#include <astra/font_catalog.h>
#include <astra/theme.h>
#include <astra/endian.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_SFNT_NAMES_H
#include FT_TRUETYPE_IDS_H

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void test_notices(const uint8_t *source, uint32_t size,
                         const AstraAfntView *view)
{
    FT_Library library;
    FT_Face face;
    static const char digits[] = "0123456789abcdef";
    uint32_t notices = 0u;

    assert(FT_Init_FreeType(&library) == 0);
    assert(FT_New_Memory_Face(library, source, size, 0, &face) == 0);
    for (FT_UInt at = 0u; at < FT_Get_Sfnt_Name_Count(face); ++at) {
        FT_SfntName record;
        assert(FT_Get_Sfnt_Name(face, at, &record) == 0);
        if (record.name_id != TT_NAME_ID_COPYRIGHT &&
            record.name_id != TT_NAME_ID_LICENSE &&
            record.name_id != TT_NAME_ID_LICENSE_URL)
            continue;
        char *expected = malloc((size_t)record.string_len * 2u + 192u);
        assert(expected != NULL);
        int prefix = snprintf(expected, 192u,
            "\"name_id\":%u,\"platform_id\":%u,\"encoding_id\":%u,"
            "\"language_id\":%u,\"raw_hex\":\"", record.name_id,
            record.platform_id, record.encoding_id, record.language_id);
        assert(prefix > 0 && prefix < 192);
        for (FT_UInt byte = 0u; byte < record.string_len; ++byte) {
            expected[prefix + byte * 2u] = digits[record.string[byte] >> 4];
            expected[prefix + byte * 2u + 1u] = digits[record.string[byte] & 15u];
        }
        uint32_t length = (uint32_t)prefix + record.string_len * 2u;
        expected[length++] = '"';
        int found = 0;
        for (uint32_t byte = 0u; length <= view->name.length &&
             byte <= view->name.length - length; ++byte)
            found |= memcmp(view->name.bytes + byte, expected, length) == 0;
        assert(found && "embedded notice was lost or truncated");
        free(expected);
        ++notices;
    }
    assert(notices != 0u);
    FT_Done_Face(face);
    FT_Done_FreeType(library);
}

static void test_variable(void)
{
    static const char *styles[] = {"Regular", "Medium", "SemiBold", "Bold"};
    static const uint32_t instances[] = {0u, 2u, 3u, 4u};
    FILE *file = fopen("tests/fonts/STIXTwoText.ttf", "rb");
    uint8_t *source;
    long size;

    assert(file != NULL && fseek(file, 0, SEEK_END) == 0);
    size = ftell(file);
    assert(size > 0 && size <= 32L * 1024L * 1024L &&
           fseek(file, 0, SEEK_SET) == 0);
    source = malloc((size_t)size);
    assert(source != NULL &&
           fread(source, 1u, (size_t)size, file) == (size_t)size);
    assert(fclose(file) == 0);
    for (uint32_t at = 0u; at < 4u; ++at) {
        uint8_t *converted = NULL;
        uint32_t length = 0u, faces = 0u;
        AstraAfntView view;
        char style[64], marker[64];
        int found = 0;
        int result = astra_font_convert_outline(source, (uint32_t)size, at,
            "STIXTwoText.ttf", "OFL-1.1", &converted, &length, &faces);
        assert(result == ASTRA_FONT_CONVERT_OK && faces == 4u &&
               astra_afnt_validate(converted, length, &view) &&
               astra_afnt_name_string(&view, "style", style, sizeof(style)) &&
               strcmp(style, styles[at]) == 0);
        int marker_length = snprintf(marker, sizeof(marker),
            "\"source_instance_index\":%u", instances[at]);
        assert(marker_length > 0 && (uint32_t)marker_length <= view.name.length);
        for (uint32_t offset = 0u;
             offset <= view.name.length - (uint32_t)marker_length; ++offset)
            found |= memcmp(view.name.bytes + offset, marker,
                            (size_t)marker_length) == 0;
        assert(found);
        free(converted);
    }
    uint8_t *converted = NULL;
    uint32_t length = 0u, faces = 0u;
    assert(astra_font_convert_outline(source, (uint32_t)size, 4u,
        "STIXTwoText.ttf", "OFL-1.1", &converted, &length, &faces) ==
        ASTRA_FONT_CONVERT_INVALID && converted == NULL && faces == 4u);
    free(source);
}

static int name_contains(const AstraAfntView *view, const char *text)
{
    size_t length = strlen(text);
    for (uint32_t at = 0u; length <= view->name.length &&
         at <= view->name.length - length; ++at)
        if (memcmp(view->name.bytes + at, text, length) == 0) return 1;
    return 0;
}

static void test_notice_encodings(const uint8_t *source, uint32_t size)
{
    enum { NOTICE_BYTES = 32768u };
    /* Append a license record to a real SFNT; glyph tables stay untouched. */
    uint8_t *copy = malloc(size + 65536u);
    uint8_t *name = NULL, *record = NULL, *directory = NULL;
    uint32_t storage = 0u, table_length = 0u;
    assert(copy != NULL);
    memcpy(copy, source, size);
    for (uint32_t at = 0u; at < astra_load_be16(copy + 4u); ++at) {
        uint8_t *entry = copy + 12u + at * 16u;
        if (memcmp(entry, "name", 4u) == 0) {
            uint32_t offset = astra_load_be32(entry + 8u);
            uint32_t length = astra_load_be32(entry + 12u);
            assert(offset <= size && length <= size - offset);
            name = copy + offset;
            table_length = length;
            directory = entry;
            break;
        }
    }
    assert(name != NULL && directory != NULL && astra_load_be16(name) == 0u);
    storage = astra_load_be16(name + 4u);
    uint32_t count = astra_load_be16(name + 2u);
    uint8_t *extended = copy + size;
    assert(table_length + 18u + NOTICE_BYTES + 10u <= 65536u &&
           storage == 6u + count * 12u);
    memcpy(extended, name, storage);
    memcpy(extended + storage + 18u, name + storage, table_length - storage);
    astra_store_be16(extended, 1u);
    astra_store_be16(extended + 2u, (uint16_t)(count + 1u));
    astra_store_be16(extended + 4u, (uint16_t)(storage + 18u));
    record = extended + storage;
    memset(record, 0, 12u);
    astra_store_be16(record + 6u, TT_NAME_ID_LICENSE);
    astra_store_be16(record + 10u, (uint16_t)(table_length - storage));
    astra_store_be32(directory + 8u, size);
    astra_store_be32(directory + 12u, table_length + 18u + NOTICE_BYTES + 10u);
    uint8_t *text = extended + table_length + 18u;
    uint8_t *tag = record + 12u;
    astra_store_be16(tag, 1u);
    astra_store_be16(tag + 2u, 10u);
    astra_store_be16(tag + 4u, (uint16_t)(table_length - storage + NOTICE_BYTES));
    memcpy(text + NOTICE_BYTES, "\0e\0n\0-\0U\0S", 10u);
    uint32_t extended_size = size + table_length + 18u + NOTICE_BYTES + 10u;
    uint8_t *converted = NULL;
    uint32_t length = 0u, faces = 0u;
    AstraAfntView view;
    for (uint32_t variant = 0u; variant < 6u; ++variant) {
        astra_store_be16(record, TT_PLATFORM_MICROSOFT);
        astra_store_be16(record + 2u, TT_MS_ID_UNICODE_CS);
        astra_store_be16(record + 4u, 0x409u);
        astra_store_be16(record + 8u, NOTICE_BYTES);
        for (uint32_t at = 0u; at < NOTICE_BYTES; at += 2u) {
            text[at] = 0u; text[at + 1u] = 'L';
        }
        text[1] = '\n'; text[3] = '\r'; text[5] = '\t';
        text[6] = 0x03u; text[7] = 0xa9u;
        if (variant == 1u) { /* Unknown encoding retains bytes, no guessed text. */
            astra_store_be16(record + 2u, 0xffffu);
        } else if (variant == 2u) { /* Unpaired surrogate. */
            text[8] = 0xd8u; text[9] = 0u;
        } else if (variant == 3u) { /* Embedded NUL must not truncate a notice. */
            text[8] = 0u; text[9] = 0u;
        } else if (variant == 4u) {
            astra_store_be16(record, TT_PLATFORM_MACINTOSH);
            astra_store_be16(record + 2u, TT_MAC_ID_ROMAN);
            astra_store_be16(record + 8u, 512u);
            memset(text, 'M', 512u); text[0] = 0xa9u; text[1] = '\n';
        } else if (variant == 5u) {
            astra_store_be16(record + 4u, 0x8000u);
        }
        assert(astra_font_convert_outline(copy, extended_size, 0u, "notices.ttf", "",
            &converted, &length, &faces) == ASTRA_FONT_CONVERT_OK);
        assert(astra_afnt_validate(converted, length, &view));
        test_notices(copy, extended_size, &view);
        if (variant == 0u || variant == 5u) {
            char expected[NOTICE_BYTES] = "\"text\":\"\\u000a\\u000d\\u0009\xce\xa9";
            size_t prefix = strlen(expected);
            uint32_t letters = NOTICE_BYTES / 2u - 4u;
            memset(expected + prefix, 'L', letters);
            expected[prefix + letters] = '"'; expected[prefix + letters + 1u] = '\0';
            assert(name_contains(&view, expected));
            if (variant == 5u)
                assert(name_contains(&view,
                    "\"language_tag_raw_hex\":\"0065006e002d00550053\","
                    "\"language_tag\":\"en-US\""));
        } else if (variant == 4u) {
            assert(name_contains(&view, "\"text\":\"\xc2\xa9\\u000aMMMM"));
        } else {
            /* Match the chosen record through its closing brace, no text. */
            char tail[64];
            snprintf(tail, sizeof(tail), "%02x%02x\"}",
                     text[NOTICE_BYTES - 2u], text[NOTICE_BYTES - 1u]);
            assert(name_contains(&view, tail));
        }
        free(converted); converted = NULL;
    }
    free(copy);
}

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] :
        "../../../sw/userspace/graphics/fonts/atkinson-hyperlegible-next/AtkinsonHyperlegibleNext-Regular.ttf";
    uint32_t face = argc > 2 ? (uint32_t)strtoul(argv[2], NULL, 10) : 0u;
    FILE *file = fopen(path, "rb");
    long size;
    uint8_t *source, *converted;
    uint32_t converted_length, faces;
    AstraAfntView view;
    assert(file != NULL && fseek(file, 0, SEEK_END) == 0);
    size = ftell(file);
    assert(size > 0 && size <= 32L * 1024L * 1024L && fseek(file, 0, SEEK_SET) == 0);
    source = malloc((size_t)size);
    assert(source != NULL && fread(source, 1u, (size_t)size, file) == (size_t)size);
    assert(fclose(file) == 0);
    int result = astra_font_convert_outline(source, (uint32_t)size, face,
        "test.ttf", "OFL-1.1", &converted, &converted_length, &faces);
    if (result != ASTRA_FONT_CONVERT_OK)
        fprintf(stderr, "conversion status %d, faces %u\n", result, faces);
    assert(result == ASTRA_FONT_CONVERT_OK);
    assert(faces >= 1u && converted_length > 128u &&
           astra_afnt_validate(converted, converted_length, &view));
    AstraFontCatalogFace catalog_face;
    AstraFontCatalogStrike catalog_strikes[32];
    char catalog_family[256], catalog_style[128];
    assert(astra_font_catalog_face_from_afnt(converted, converted_length,
        "user:test", 9u, 0u, catalog_family, sizeof(catalog_family),
        catalog_style, sizeof(catalog_style), catalog_strikes, 32u,
        &catalog_face));
    fprintf(stderr, "face %u/%u AFNT %u bytes %s %s\n", face, faces,
            converted_length, catalog_family, catalog_style);
    assert(view.strike_count == 4u && view.face_glyphs > 100u);
    const uint16_t expected_sizes[] = {
        ASTRA_THEME_SYSTEM_BODY_FONT_HEIGHT,
        ASTRA_THEME_SYSTEM_TITLE_FONT_HEIGHT,
        ASTRA_THEME_SYSTEM_MENU_FONT_HEIGHT,
        20u
    };
    for (uint32_t at = 0u; at < view.strike_count; ++at) {
        AstraAfntStrikeInfo strike;
        assert(astra_afnt_strike_info(&view, at, &strike));
        assert(strike.pixel_height == expected_sizes[at] &&
               strike.bitmap_format == 5u);
    }
    char family[256], style[256];
    assert(astra_afnt_name_string(&view, "family", family, sizeof(family)));
    assert(astra_afnt_name_string(&view, "style", style, sizeof(style)));
    assert(family[0] != '\0' && style[0] != '\0');
    int found_license = 0;
    for (uint32_t at = 0; at + 7u <= view.name.length; ++at)
        found_license |= memcmp(view.name.bytes + at, "OFL-1.1", 7u) == 0;
    assert(found_license);
    test_notices(source, (uint32_t)size, &view);
    if (argc == 1) test_notice_encodings(source, (uint32_t)size);
    free(converted);
    memset(source, 0, (size_t)size < 16u ? (size_t)size : 16u);
    assert(astra_font_convert_outline(source, (uint32_t)size, face,
        "test.ttf", "OFL-1.1", &converted, &converted_length, &faces) !=
        ASTRA_FONT_CONVERT_OK);
    free(source);
    if (argc == 1) test_variable();
    puts("native outline-to-AFNT conversion passed");
    return 0;
}

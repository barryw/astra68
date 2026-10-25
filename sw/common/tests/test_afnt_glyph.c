#include <astra/afnt.h>

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void test_metadata(const AstraAfntView *view, const char *metadata,
                           int expected)
{
    AstraAfntChunk chunks[ASTRA_AFNT_CHUNK_COUNT] = {
        {(const uint8_t *)metadata, (uint32_t)strlen(metadata)}, view->cmap,
        view->strikes, view->glyphs, view->bitmap};
    uint32_t size;
    AstraAfntView parsed;
    assert(astra_afnt_pack(chunks, NULL, 0u, &size));
    uint8_t *packed = malloc(size);
    assert(packed != NULL && astra_afnt_pack(chunks, packed, size, &size));
    assert(astra_afnt_validate(packed, size, &parsed) == expected);
    free(packed);
}

int main(void)
{
    static const uint8_t letter_a[16] = {
        0x00, 0x00, 0x7c, 0xc6, 0xc6, 0xc6, 0xfe, 0xc6,
        0xc6, 0xc6, 0xc6, 0xc6, 0x00, 0x00, 0x00, 0x00
    };
    FILE *file = fopen("../userspace/graphics/fonts/astra-rescue-mono.afnt",
                       "rb");
    assert(file != NULL && fseek(file, 0, SEEK_END) == 0);
    long length = ftell(file);
    assert(length > 0 && (uint64_t)length <= UINT32_MAX &&
           fseek(file, 0, SEEK_SET) == 0);
    uint8_t *bytes = malloc((size_t)length);
    assert(bytes != NULL && fread(bytes, 1u, (size_t)length, file) ==
           (size_t)length && fclose(file) == 0);
    AstraAfntView view = {0};
    AstraAfntGlyphInfo glyph = {0}, replacement = {0}, absent = {0};

    assert(astra_afnt_validate(bytes, (uint32_t)length, &view));
    assert(astra_afnt_glyph_info(&view, 0u, 'A', &glyph));
    assert(glyph.bitmap_length == sizeof(letter_a) && glyph.width == 8u &&
           glyph.height == 16u && glyph.pitch == 1u &&
           glyph.advance_x == 8 * 64 &&
           memcmp(glyph.bitmap, letter_a, sizeof(letter_a)) == 0);
    assert(astra_afnt_glyph_info(&view, 0u, 0xfffdu, &replacement));
    assert(astra_afnt_glyph_info(&view, 0u, 0x110000u, &absent));
    assert(absent.bitmap == replacement.bitmap &&
           absent.bitmap_length == replacement.bitmap_length);
    assert(!astra_afnt_glyph_info(&view, 1u, 'A', &glyph));
    test_metadata(&view, "{\"family\":\"X\",\"license\":\"First\\nSecond\"}", 1);
    test_metadata(&view, " \n {\"family\":\"X\"}", 1);
    test_metadata(&view, "[]", 0);
    test_metadata(&view, "null", 0);
    test_metadata(&view, "{\"family\":\"X\",\"ignored\":[,]}", 0);
    test_metadata(&view, "{\"family\":\"X\",\"ignored\":broken}", 0);
    test_metadata(&view, "{\"family\":\"X\",\"ignored\":01}", 0);
    test_metadata(&view, "{\"family\":\"X\",\"ignored\":\"\\ud800\"}", 0);
    {
        uint8_t *metadata = malloc(ASTRA_AFNT_NAME_MAX + 1u);
        AstraAfntChunk chunks[ASTRA_AFNT_CHUNK_COUNT] = {
            {metadata, ASTRA_AFNT_NAME_MAX}, view.cmap, view.strikes,
            view.glyphs, view.bitmap};
        uint32_t packed_size;
        assert(metadata != NULL);
        memset(metadata, ' ', ASTRA_AFNT_NAME_MAX + 1u);
        memcpy(metadata, view.name.bytes, view.name.length);
        for (uint32_t extra = 0u; extra < 2u; ++extra) {
            AstraAfntView large;
            chunks[0].length = ASTRA_AFNT_NAME_MAX + extra;
            assert(astra_afnt_pack(chunks, NULL, 0u, &packed_size));
            uint8_t *packed = malloc(packed_size);
            assert(packed != NULL &&
                   astra_afnt_pack(chunks, packed, packed_size, &packed_size));
            assert(astra_afnt_validate(packed, packed_size, &large) == (extra == 0u));
            free(packed);
        }
        free(metadata);
    }
    view.cmap.length = 3u;
    assert(!astra_afnt_glyph_info(&view, 0u, 'A', &glyph));
    free(bytes);
    puts("AFNT glyph lookup passed");
    return 0;
}

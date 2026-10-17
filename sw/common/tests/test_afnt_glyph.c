#include <astra/afnt.h>

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    view.cmap.length = 3u;
    assert(!astra_afnt_glyph_info(&view, 0u, 'A', &glyph));
    free(bytes);
    puts("AFNT glyph lookup passed");
    return 0;
}

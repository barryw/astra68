#include "astra_font_convert.h"

#include <astra/afnt.h>

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void check(const char *path, uint16_t height)
{
    FILE *file = fopen(path, "rb");
    long size;
    uint8_t *source;
    assert(file != NULL && fseek(file, 0, SEEK_END) == 0);
    size = ftell(file);
    assert(size > 0 && size < 32L * 1024L * 1024L &&
           fseek(file, 0, SEEK_SET) == 0);
    source = malloc((size_t)size);
    assert(source != NULL && fread(source, 1u, (size_t)size, file) ==
           (size_t)size && fclose(file) == 0);

    const char *encodings[] = {"ISO-8859-2", "ISO-8859-1"};
    const uint32_t characters[] = {0x0104u, 0x00a1u};
    for (uint32_t at = 0u; at < 2u; ++at) {
        uint8_t *afnt = NULL;
        uint32_t afnt_size = 0u, faces = 0u;
        AstraAfntView view;
        AstraAfntStrikeInfo strike;
        AstraAfntGlyphInfo glyph;
        assert(astra_font_convert_amiga(source, (uint32_t)size,
            encodings[at], "ISOHelveticaPL.font", "AROS", &afnt,
            &afnt_size, &faces) == ASTRA_FONT_CONVERT_OK);
        assert(faces == 1u && afnt_size > 128u &&
               astra_afnt_validate(afnt, afnt_size, &view) &&
               view.strike_count == 1u && view.face_glyphs == 225u &&
               astra_afnt_strike_info(&view, 0u, &strike) &&
               strike.pixel_height == height && strike.bitmap_format == 1u &&
               astra_afnt_glyph_info(&view, 0u, characters[at], &glyph) &&
               glyph.bitmap_length > 0u &&
               astra_afnt_glyph_info(&view, 0u, 0xfffdu, &glyph));
        free(afnt);
    }
    uint8_t *afnt = NULL;
    uint32_t afnt_size = 0u, faces = 0u;
    assert(astra_font_convert_amiga(source, (uint32_t)size,
        "unlabeled", "ISOHelveticaPL.font", "AROS", &afnt,
        &afnt_size, &faces) == ASTRA_FONT_CONVERT_INVALID && afnt == NULL);
    free(source);
}

int main(void)
{
    check("../../../sw/userspace/graphics/fonts/aros-isohelvetica/11.amiga", 11u);
    check("../../../sw/userspace/graphics/fonts/aros-isohelvetica/13.amiga", 13u);
    puts("native Amiga-to-AFNT conversion passed");
    return 0;
}

#include "astra_font_convert.h"

#include <astra/afnt.h>
#include <astra/theme.h>

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] :
        "../../../sw/userspace/graphics/fonts/atkinson-hyperlegible-next/AtkinsonHyperlegibleNext-Regular.ttf";
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
    int result = astra_font_convert_outline(source, (uint32_t)size, 0u,
        "test.ttf", "OFL-1.1", &converted, &converted_length, &faces);
    if (result != ASTRA_FONT_CONVERT_OK)
        fprintf(stderr, "conversion status %d, faces %u\n", result, faces);
    assert(result == ASTRA_FONT_CONVERT_OK);
    assert(faces >= 1u && converted_length > 128u &&
           astra_afnt_validate(converted, converted_length, &view));
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
    free(converted);
    memset(source, 0, (size_t)size < 16u ? (size_t)size : 16u);
    assert(astra_font_convert_outline(source, (uint32_t)size, 0u,
        "test.ttf", "OFL-1.1", &converted, &converted_length, &faces) !=
        ASTRA_FONT_CONVERT_OK);
    free(source);
    puts("native outline-to-AFNT conversion passed");
    return 0;
}

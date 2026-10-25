#include "astra_amiga_font.h"

#include <astra/endian.h>

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *read_font(const char *path, uint32_t *length)
{
    FILE *file = fopen(path, "rb");
    uint8_t *bytes;
    long size;

    assert(file != NULL && fseek(file, 0, SEEK_END) == 0);
    size = ftell(file);
    assert(size > 0 && size < 32L * 1024L * 1024L &&
           fseek(file, 0, SEEK_SET) == 0);
    bytes = malloc((size_t)size);
    assert(bytes != NULL && fread(bytes, 1u, (size_t)size, file) ==
           (size_t)size && fclose(file) == 0);
    *length = (uint32_t)size;
    return bytes;
}

static void check_font(const char *path, uint16_t height)
{
    uint32_t length;
    uint8_t *bytes = read_font(path, &length);
    uint8_t *broken = malloc(length);
    AstraAmigaFont font;
    uint32_t varied_space = 0u, varied_kern = 0u;

    assert(broken != NULL && astra_amiga_font_parse(bytes, length, &font));
    assert(font.height == height && font.baseline < height &&
           font.first_code == 32u && font.last_code == 255u &&
           font.glyph_count == 225u && font.modulo != 0u &&
           font.bitmap_bytes == (uint32_t)font.modulo * height &&
           strncmp(font.name, "ISOHelveticaPL", 14u) == 0);
    for (uint32_t at = 0u; at < font.glyph_count; ++at) {
        varied_space |= font.spacing[at] != (int16_t)font.nominal_width;
        varied_kern |= font.kerning[at] != 0;
    }
    assert(varied_space && varied_kern &&
           font.width[font.glyph_count - 1u] != 0u);
    assert(!astra_amiga_font_parse(bytes, length - 4u, &font));

    memcpy(broken, bytes, length);
    astra_store_be32(broken, 0u);
    assert(!astra_amiga_font_parse(broken, length, &font));
    memcpy(broken, bytes, length);
    astra_store_be32(broken + 8u, 2u);
    assert(!astra_amiga_font_parse(broken, length, &font));
    memcpy(broken, bytes, length);
    astra_store_be16(broken + 28u + 22u, 0u);
    assert(!astra_amiga_font_parse(broken, length, &font));
    memcpy(broken, bytes, length);
    broken[28u + 84u] |= 0x40u;
    assert(!astra_amiga_font_parse(broken, length, &font));
    memcpy(broken, bytes, length);
    astra_store_be16(broken + 28u + 88u, height);
    assert(!astra_amiga_font_parse(broken, length, &font));
    memcpy(broken, bytes, length);
    astra_store_be32(broken + 28u + 102u, UINT32_MAX);
    assert(!astra_amiga_font_parse(broken, length, &font));
    memcpy(broken, bytes, length);
    uint32_t location = astra_load_be32(broken + 28u + 102u);
    astra_store_be16(broken + 32u + location + 2u, UINT16_MAX);
    assert(!astra_amiga_font_parse(broken, length, &font));
    memcpy(broken, bytes, length);
    uint32_t tail = 32u + astra_load_be32(broken + 28u) * 4u;
    assert(astra_load_be32(broken + tail) == 0x3ecu);
    astra_store_be32(broken + tail + 8u, 1u);
    assert(!astra_amiga_font_parse(broken, length, &font));
    free(broken);
    free(bytes);
}

int main(void)
{
    check_font("../../../sw/userspace/graphics/fonts/aros-isohelvetica/11.amiga",
               11u);
    check_font("../../../sw/userspace/graphics/fonts/aros-isohelvetica/13.amiga",
               13u);
    puts("bounded Amiga disk-font reader passed");
    return 0;
}

#include <astra/font_catalog.h>

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *read_file(const char *path, uint32_t *length)
{
    FILE *file = fopen(path, "rb");
    long size;
    uint8_t *bytes;

    assert(file != NULL && fseek(file, 0, SEEK_END) == 0);
    size = ftell(file);
    assert(size > 0 && (uint64_t)size <= UINT32_MAX &&
           fseek(file, 0, SEEK_SET) == 0);
    bytes = malloc((size_t)size);
    assert(bytes != NULL && fread(bytes, 1u, (size_t)size, file) ==
           (size_t)size && fclose(file) == 0);
    *length = (uint32_t)size;
    return bytes;
}

int main(void)
{
    const AstraFontCatalogStrike sans_strikes[] = {
        {0u, 11u, 5u, 0u}, {0u, 13u, 5u, 0u}, {0u, 16u, 5u, 0u}
    };
    const AstraFontCatalogStrike mono_strikes[] = {
        {8u, 16u, 1u, 0u}
    };
    const AstraFontCatalogFace faces[] = {
        {"system:astra-sans", 17u, "Astra Sans", 10u, "Regular", 7u,
         sans_strikes, 3u,
         ASTRA_FONT_CATALOG_SYSTEM | ASTRA_FONT_CATALOG_SELECTABLE},
        {"user:0123456789abcdef", 21u, "Iosevka", 7u, "Bold", 4u,
         mono_strikes, 1u, 0u}
    };
    uint32_t length = 0u;
    assert(astra_font_catalog_pack(faces, 2u, 7u, NULL, 0u, &length));
    uint8_t *bytes = malloc(length);
    assert(bytes != NULL);
    assert(!astra_font_catalog_pack(faces, 2u, 7u, bytes, length - 1u,
                                    &length));
    assert(astra_font_catalog_pack(faces, 2u, 7u, bytes, length, &length));
    assert(astra_font_catalog_validate(bytes, length));
    AstraFontCatalogFace face;
    assert(astra_font_catalog_face(bytes, length, 0u, &face));
    assert(face.family_length == 10u &&
           memcmp(face.family, "Astra Sans", 10u) == 0 &&
           face.strike_count == 3u && face.strikes[0].pixel_height == 11u &&
           face.strikes[2].pixel_height == 16u &&
           (face.flags & ASTRA_FONT_CATALOG_SELECTABLE) != 0u);
    assert(astra_font_catalog_face(bytes, length, 1u, &face));
    assert(face.style_length == 4u &&
           memcmp(face.style, "Bold", 4u) == 0 &&
           face.flags == 0u);
    assert(!astra_font_catalog_face(bytes, length, 2u, &face));
    uint8_t *unaligned = malloc(length + 1u);
    assert(unaligned != NULL);
    memcpy(unaligned + 1u, bytes, length);
    assert(!astra_font_catalog_face(unaligned + 1u, length, 0u, &face));
    free(unaligned);

    AstraFontCatalogRecord *record = (AstraFontCatalogRecord *)(void *)(
        bytes + sizeof(AstraFontCatalogHeader));
    uint32_t saved = record->strikes_offset;
    record->strikes_offset = length - 2u;
    assert(!astra_font_catalog_validate(bytes, length));
    record->strikes_offset = saved;
    saved = record->family_offset;
    bytes[saved] = 0xffu;
    assert(!astra_font_catalog_validate(bytes, length));
    bytes[saved] = 'A';
    record->flags = 4u;
    assert(!astra_font_catalog_validate(bytes, length));
    free(bytes);

    assert(!astra_font_catalog_pack(faces, 2u, 0u, NULL, 0u, &length));
    assert(!astra_font_catalog_pack(&(AstraFontCatalogFace){
        "../escape", 9u, "Name", 4u, "Regular", 7u,
        mono_strikes, 1u, 0u}, 1u, 1u, NULL, 0u, &length));

    const char *const shipped[] = {
        "fonts/astra-workbench.afnt", "fonts/astra-mono.afnt",
        "fonts/astra-rescue-mono.afnt"
    };
    for (uint32_t at = 0u; at < 3u; ++at) {
        uint8_t *afnt = read_file(shipped[at], &length);
        char family[256], style[256];
        AstraFontCatalogStrike strikes[64];
        AstraFontCatalogFace imported;
        assert(astra_font_catalog_face_from_afnt(
            afnt, length, "system:test", 11u, ASTRA_FONT_CATALOG_SYSTEM,
            family, sizeof(family), style, sizeof(style), strikes, 64u,
            &imported));
        assert(imported.family_length != 0u &&
               imported.style_length != 0u && imported.strike_count != 0u);
        uint32_t packed_size = 0u;
        assert(astra_font_catalog_pack(&imported, 1u, 1u, NULL, 0u,
                                       &packed_size));
        uint8_t *packed = malloc(packed_size);
        assert(packed != NULL && astra_font_catalog_pack(
            &imported, 1u, 1u, packed, packed_size, &packed_size));
        assert(astra_font_catalog_validate(packed, packed_size));
        free(packed);
        assert(!astra_font_catalog_face_from_afnt(
            afnt, length - 1u, "system:test", 11u,
            ASTRA_FONT_CATALOG_SYSTEM, family, sizeof(family), style,
            sizeof(style), strikes, 64u, &imported));
        assert(!astra_font_catalog_face_from_afnt(
            afnt, length, "system:test", 11u,
            ASTRA_FONT_CATALOG_SYSTEM, family, sizeof(family), style,
            sizeof(style), strikes, 0u, &imported));
        free(afnt);
    }
    puts("native font catalog snapshot passed");
    return 0;
}

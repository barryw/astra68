#include <astra/afnt.h>
#include <astra/crc32.h>

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t be32(const uint8_t *at)
{
    return (uint32_t)at[0] << 24 | (uint32_t)at[1] << 16 |
           (uint32_t)at[2] << 8 | at[3];
}

static void put32(uint8_t *at, uint32_t value)
{
    at[0] = (uint8_t)(value >> 24);
    at[1] = (uint8_t)(value >> 16);
    at[2] = (uint8_t)(value >> 8);
    at[3] = (uint8_t)value;
}

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
    assert(bytes != NULL && fread(bytes, 1u, (size_t)size, file) == (size_t)size);
    assert(fclose(file) == 0);
    *length = (uint32_t)size;
    return bytes;
}

static uint8_t *entry(uint8_t *bytes, const char *kind)
{
    for (uint32_t at = 0; at < 5u; ++at) {
        uint8_t *candidate = bytes + 28u + 20u * at;
        if (memcmp(candidate, kind, 4u) == 0) return candidate;
    }
    assert(0);
    return NULL;
}

static void seal(uint8_t *bytes, uint8_t *row)
{
    put32(row + 16u, astra_crc32(bytes + be32(row + 8u), be32(row + 12u)));
    put32(bytes + 20u, astra_crc32(bytes + 28u, 100u));
}

int main(int argc, char **argv)
{
    const char *defaults[] = {
        "fonts/astra-workbench.afnt", "fonts/astra-mono.afnt",
        "fonts/astra-rescue-mono.afnt"
    };
    uint32_t count = argc > 1 ? (uint32_t)(argc - 1) :
        (uint32_t)(sizeof(defaults) / sizeof(defaults[0]));
    for (uint32_t at = 0; at < count; ++at) {
        const char *path = argc > 1 ? argv[at + 1u] : defaults[at];
        uint32_t length;
        uint8_t *bytes = read_file(path, &length);
        AstraAfntView view;
        assert(astra_afnt_validate(bytes, length, &view));
        assert(view.strike_count > 0u && view.face_glyphs > 0u);
        AstraAfntChunk chunks[] = {
            view.name, view.cmap, view.strikes, view.glyphs, view.bitmap
        };
        uint32_t packed_length = 0u;
        assert(astra_afnt_pack(chunks, NULL, 0u, &packed_length));
        assert(packed_length == length);
        assert(!astra_afnt_pack(chunks, bytes, length - 1u, &packed_length));
        uint8_t *packed = malloc(packed_length);
        assert(packed != NULL &&
               astra_afnt_pack(chunks, packed, packed_length, &packed_length));
        assert(memcmp(packed, bytes, length) == 0);
        AstraAfntChunk overflow[ASTRA_AFNT_CHUNK_COUNT] = {
            view.name, view.cmap, view.strikes, view.glyphs, view.bitmap
        };
        overflow[ASTRA_AFNT_NAME].length = UINT32_MAX;
        assert(!astra_afnt_pack(overflow, NULL, 0u, &packed_length));
        free(packed);
        assert(!astra_afnt_validate(bytes, length - 1u, &view));
        bytes[20] ^= 1u;
        assert(!astra_afnt_validate(bytes, length, &view));
        bytes[20] ^= 1u;
        free(bytes);
    }
    uint32_t length;
    uint8_t *bytes = read_file(argc > 1 ? argv[1] : defaults[0], &length);
    uint8_t *glyph = entry(bytes, "GLYP");
    uint8_t *record = bytes + be32(glyph + 8u) + 4u;
    record[12u] = record[13u] = 0u;
    seal(bytes, glyph);
    assert(!astra_afnt_validate(bytes, length, &(AstraAfntView){0}));
    free(bytes);
    puts("AFNT native validation passed");
    return 0;
}

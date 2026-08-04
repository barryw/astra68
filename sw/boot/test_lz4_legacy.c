#include "lz4_legacy.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SPLASH_RAW_BYTES 350720u
#define SPLASH_RAW_CRC32 0x0c8b36ddu

static uint32_t crc32(const uint8_t *bytes, size_t size)
{
    uint32_t crc = 0xffffffffu;

    for (size_t index = 0; index < size; ++index) {
        crc ^= bytes[index];
        for (unsigned bit = 0; bit < 8u; ++bit)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

static uint8_t *read_file(const char *path, uint32_t *size)
{
    FILE *file = fopen(path, "rb");
    long length;
    uint8_t *bytes;

    assert(file != NULL);
    assert(fseek(file, 0, SEEK_END) == 0);
    length = ftell(file);
    assert(length > 0 && (unsigned long)length <= UINT32_MAX);
    assert(fseek(file, 0, SEEK_SET) == 0);
    bytes = malloc((size_t)length);
    assert(bytes != NULL);
    assert(fread(bytes, 1u, (size_t)length, file) == (size_t)length);
    assert(fclose(file) == 0);
    *size = (uint32_t)length;
    return bytes;
}

static void test_literal_and_rejections(void)
{
    static const uint8_t literal_frame[] = {
        0x02, 0x21, 0x4c, 0x18,
        0x06, 0x00, 0x00, 0x00,
        0x50, 'h', 'e', 'l', 'l', 'o'
    };
    static const uint8_t bad_offset_frame[] = {
        0x02, 0x21, 0x4c, 0x18,
        0x03, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00
    };
    uint8_t output[16] = {0};

    assert(astra_lz4_legacy_decode(
               literal_frame, sizeof(literal_frame), output,
               sizeof(output), 5u) == ASTRA_LZ4_OK);
    assert(memcmp(output, "hello", 5u) == 0);
    assert(astra_lz4_legacy_decode(
               literal_frame, sizeof(literal_frame) - 1u, output,
               sizeof(output), 5u) == ASTRA_LZ4_BAD_FRAME);
    assert(astra_lz4_legacy_decode(
               literal_frame, sizeof(literal_frame), output,
               4u, 5u) == ASTRA_LZ4_BAD_ARGUMENT);
    assert(astra_lz4_legacy_decode(
               literal_frame, sizeof(literal_frame), output,
               sizeof(output), 6u) == ASTRA_LZ4_SIZE_MISMATCH);
    assert(astra_lz4_legacy_decode(
               bad_offset_frame, sizeof(bad_offset_frame), output,
               sizeof(output), 4u) == ASTRA_LZ4_BAD_OFFSET);
}

static void test_checked_in_splash(void)
{
    uint32_t compressed_size;
    uint8_t *compressed = read_file(
        "assets/astra_boot_splash.pal8.lz4", &compressed_size);
    uint8_t *output = malloc(SPLASH_RAW_BYTES);

    assert(output != NULL);
    assert(astra_lz4_legacy_decode(
               compressed, compressed_size, output, SPLASH_RAW_BYTES,
               SPLASH_RAW_BYTES) == ASTRA_LZ4_OK);
    assert(crc32(output, SPLASH_RAW_BYTES) == SPLASH_RAW_CRC32);
    // The four reserved palette entries begin after the framebuffer pixels.
    assert(output[345600u + 252u * 4u + 0u] == 0xf2u);
    assert(output[345600u + 252u * 4u + 1u] == 0xecu);
    assert(output[345600u + 252u * 4u + 2u] == 0x38u);
    // The packed font is a true 8x16 bank with duplicated source rows.
    assert(output[346624u + (uint32_t)'A' * 16u] ==
           output[346624u + (uint32_t)'A' * 16u + 1u]);
    free(output);
    free(compressed);
}

int main(void)
{
    test_literal_and_rejections();
    test_checked_in_splash();
    puts("boot splash LZ4 tests: PASS");
    return 0;
}

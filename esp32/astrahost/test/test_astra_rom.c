#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "astra_rom.h"

#define PAYLOAD_SIZE 64u

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t size)
{
    for (size_t index = 0; index < size; ++index) {
        crc ^= data[index];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return crc;
}

static void write_be16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)(value >> 8);
    bytes[1] = (uint8_t)value;
}

static void write_be32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)(value >> 24);
    bytes[1] = (uint8_t)(value >> 16);
    bytes[2] = (uint8_t)(value >> 8);
    bytes[3] = (uint8_t)value;
}

static void make_image(uint8_t *image, uint32_t initial_pc)
{
    uint8_t *payload = image + ASTRA_ROM_HEADER_SIZE;
    memset(image, 0, ASTRA_ROM_HEADER_SIZE + PAYLOAD_SIZE);
    memcpy(image, "A68R", 4);
    write_be16(image + 4, 1);
    write_be16(image + 6, ASTRA_ROM_HEADER_SIZE);
    write_be32(image + 8, PAYLOAD_SIZE);
    write_be32(image + 16, 0xffe00000u);
    write_be32(image + 20, 0x03e00000u);
    write_be32(payload, 0x02000000u);
    write_be32(payload + 4, initial_pc);
    for (size_t index = 8; index < PAYLOAD_SIZE; ++index)
        payload[index] = (uint8_t)(index * 29u);
    write_be32(image + 12,
               ~crc32_update(0xffffffffu, payload, PAYLOAD_SIZE));
    write_be32(image + 28,
               ~crc32_update(0xffffffffu, image, 28));
}

static FILE *image_file(const uint8_t *image)
{
    FILE *file = tmpfile();
    assert(file != NULL);
    assert(fwrite(image, 1, ASTRA_ROM_HEADER_SIZE + PAYLOAD_SIZE, file) ==
           ASTRA_ROM_HEADER_SIZE + PAYLOAD_SIZE);
    assert(fflush(file) == 0);
    return file;
}

int main(void)
{
    uint8_t image[ASTRA_ROM_HEADER_SIZE + PAYLOAD_SIZE];
    astra_rom_info_t info;
    char error[96];

    make_image(image, 0xffe00400u);
    FILE *file = image_file(image);
    assert(astra_rom_validate(file, &info, error, sizeof(error)));
    assert(info.payload_size == PAYLOAD_SIZE);
    assert(info.initial_sp == 0x02000000u);
    assert(info.initial_pc == 0xffe00400u);
    fclose(file);

    image[ASTRA_ROM_HEADER_SIZE + 17] ^= 0x80;
    file = image_file(image);
    assert(!astra_rom_validate(file, &info, error, sizeof(error)));
    assert(strstr(error, "payload CRC") != NULL);
    fclose(file);

    make_image(image, 0x00100000u);
    file = image_file(image);
    assert(!astra_rom_validate(file, &info, error, sizeof(error)));
    assert(strstr(error, "initial PC") != NULL);
    fclose(file);

    puts("PASS AstraHost ROM validation");
    return 0;
}

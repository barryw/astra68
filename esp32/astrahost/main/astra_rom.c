#include "astra_rom.h"

#include <stdarg.h>
#include <string.h>

#define ASTRA_ROM_BASE 0xffe00000u
#define ASTRA_ROM_LIMIT 0xffe40000u
#define ASTRA_ROM_LOAD_ADDRESS 0x03e00000u
#define ASTRA_STACK_MIN 0x01ff8000u
#define ASTRA_STACK_MAX 0x02000000u

static uint32_t read_be32(const uint8_t *bytes)
{
    return ((uint32_t)bytes[0] << 24) |
           ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) |
           bytes[3];
}

static uint16_t read_be16(const uint8_t *bytes)
{
    return ((uint16_t)bytes[0] << 8) | bytes[1];
}

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t size)
{
    for (size_t index = 0; index < size; ++index) {
        crc ^= data[index];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return crc;
}

static bool fail(char *error, size_t error_size, const char *format, ...)
{
    if (error != NULL && error_size != 0) {
        va_list arguments;
        va_start(arguments, format);
        vsnprintf(error, error_size, format, arguments);
        va_end(arguments);
    }
    return false;
}

bool astra_rom_validate(FILE *file, astra_rom_info_t *info,
                        char *error, size_t error_size)
{
    uint8_t header[ASTRA_ROM_HEADER_SIZE];
    uint8_t buffer[1024];

    if (file == NULL || info == NULL)
        return fail(error, error_size, "invalid validator arguments");
    if (fseek(file, 0, SEEK_SET) != 0 ||
        fread(header, 1, sizeof(header), file) != sizeof(header))
        return fail(error, error_size, "cannot read ROM header");

    if (memcmp(header, "A68R", 4) != 0)
        return fail(error, error_size, "bad ROM magic");
    if (read_be16(header + 4) != 1 ||
        read_be16(header + 6) != ASTRA_ROM_HEADER_SIZE)
        return fail(error, error_size, "unsupported ROM format");

    uint32_t payload_size = read_be32(header + 8);
    uint32_t expected_crc = read_be32(header + 12);
    if (payload_size < 8 || payload_size > ASTRA_ROM_MAX_PAYLOAD)
        return fail(error, error_size, "invalid payload size %lu",
                    (unsigned long)payload_size);
    if (read_be32(header + 16) != ASTRA_ROM_BASE ||
        read_be32(header + 20) != ASTRA_ROM_LOAD_ADDRESS ||
        read_be32(header + 24) != 0)
        return fail(error, error_size, "unsupported ROM memory contract");
    if (~crc32_update(0xffffffffu, header, 28) != read_be32(header + 28))
        return fail(error, error_size, "ROM header CRC mismatch");

    if (fseek(file, 0, SEEK_END) != 0)
        return fail(error, error_size, "cannot determine ROM size");
    long file_size = ftell(file);
    if (file_size < 0 || (uint32_t)file_size !=
        ASTRA_ROM_HEADER_SIZE + payload_size)
        return fail(error, error_size, "ROM file length mismatch");

    if (fseek(file, ASTRA_ROM_HEADER_SIZE, SEEK_SET) != 0 ||
        fread(buffer, 1, 8, file) != 8)
        return fail(error, error_size, "cannot read ROM vectors");
    uint32_t initial_sp = read_be32(buffer);
    uint32_t initial_pc = read_be32(buffer + 4);
    if (initial_sp <= ASTRA_STACK_MIN || initial_sp > ASTRA_STACK_MAX)
        return fail(error, error_size, "invalid initial SP 0x%08lx",
                    (unsigned long)initial_sp);
    if (initial_pc < ASTRA_ROM_BASE || initial_pc >= ASTRA_ROM_LIMIT)
        return fail(error, error_size, "invalid initial PC 0x%08lx",
                    (unsigned long)initial_pc);

    if (fseek(file, ASTRA_ROM_HEADER_SIZE, SEEK_SET) != 0)
        return fail(error, error_size, "cannot seek to ROM payload");
    uint32_t crc = 0xffffffffu;
    uint32_t remaining = payload_size;
    while (remaining != 0) {
        size_t requested = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
        size_t received = fread(buffer, 1, requested, file);
        if (received != requested)
            return fail(error, error_size, "short ROM payload read");
        crc = crc32_update(crc, buffer, received);
        remaining -= received;
    }
    if (~crc != expected_crc)
        return fail(error, error_size, "ROM payload CRC mismatch");

    info->payload_size = payload_size;
    info->payload_crc32 = expected_crc;
    info->initial_sp = initial_sp;
    info->initial_pc = initial_pc;
    return true;
}

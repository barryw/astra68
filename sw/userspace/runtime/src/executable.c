#include <astra/endian.h>
#include <astra/process.h>
#include <astra/runtime.h>
#include <astra/syscall.h>

#include "stream_source.h"

#define ELF_IDENT_SIZE 16u
#define ELF_PROGRAM_HEADER_SIZE 32u
#define ELF_TYPE_EXEC 2u
#define ELF_MACHINE_68K 4u
#define ELF_VERSION_CURRENT 1u
#define ELF_CLASS_32 1u
#define ELF_DATA_BIG 2u
#define ELF_OSABI_SYSV 0u
#define ELF_PT_INTERP 3u
#define ELF_PF_R 4u

static int executable_identity_valid(const uint8_t *header)
{
    if (header[0] != 0x7fu || header[1] != 'E' || header[2] != 'L' ||
        header[3] != 'F' || header[4] != ELF_CLASS_32 ||
        header[5] != ELF_DATA_BIG || header[6] != ELF_VERSION_CURRENT ||
        header[7] != ELF_OSABI_SYSV || header[8] != 0u)
        return 0;
    for (uint32_t index = 9u; index < ELF_IDENT_SIZE; ++index) {
        if (header[index] != 0u)
            return 0;
    }
    return 1;
}

uint32_t astra_executable_interpreter(
    const AstraReadSource *source, char *identity, uint32_t capacity,
    uint32_t *identity_length)
{
    const uint8_t *header;
    uint32_t table_offset;
    uint32_t table_size;
    uint16_t header_count;
    uint32_t interpreter_offset = 0u;
    uint32_t interpreter_size = 0u;
    uint32_t status;

    if (identity_length != NULL)
        *identity_length = 0u;
    if (identity != NULL && capacity != 0u)
        identity[0] = '\0';
    if (source == NULL || source->read_at == NULL || identity == NULL ||
        capacity == 0u || identity_length == NULL ||
        source->length < ASTRA_EXECUTABLE_HEADER_SIZE)
        return ASTRA_SYSCALL_INVALID_ARGUMENT;

    status = astra_stream_read_exact(
        source->read_at, source->context, source->length, 0u,
        ASTRA_EXECUTABLE_HEADER_SIZE, &header, NULL);
    if (status != ASTRA_SYSCALL_OK)
        return status;
    if (!executable_identity_valid(header) ||
        astra_load_be16(header + 16u) != ELF_TYPE_EXEC ||
        astra_load_be16(header + 18u) != ELF_MACHINE_68K ||
        astra_load_be32(header + 20u) != ELF_VERSION_CURRENT ||
        astra_load_be32(header + 36u) != 0u ||
        astra_load_be16(header + 40u) != ASTRA_EXECUTABLE_HEADER_SIZE ||
        astra_load_be16(header + 42u) != ELF_PROGRAM_HEADER_SIZE)
        return ASTRA_SYSCALL_INVALID_ARGUMENT;

    header_count = astra_load_be16(header + 44u);
    table_offset = astra_load_be32(header + 28u);
    if (header_count == 0u)
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    table_size = (uint32_t)header_count * ELF_PROGRAM_HEADER_SIZE;
    if (table_offset > source->length ||
        table_size > source->length - table_offset)
        return ASTRA_SYSCALL_INVALID_ARGUMENT;

    for (uint32_t index = 0u; index < header_count; ++index) {
        const uint8_t *program_header;
        uint32_t offset = table_offset + index * ELF_PROGRAM_HEADER_SIZE;

        status = astra_stream_read_exact(
            source->read_at, source->context, source->length, offset,
            ELF_PROGRAM_HEADER_SIZE, &program_header, NULL);
        if (status != ASTRA_SYSCALL_OK)
            return status;
        if (astra_load_be32(program_header) != ELF_PT_INTERP)
            continue;
        if (interpreter_size != 0u ||
            astra_load_be32(program_header + 24u) != ELF_PF_R)
            return ASTRA_SYSCALL_INVALID_ARGUMENT;
        interpreter_offset = astra_load_be32(program_header + 4u);
        interpreter_size = astra_load_be32(program_header + 16u);
        if (interpreter_size < 2u ||
            interpreter_size != astra_load_be32(program_header + 20u) ||
            interpreter_offset > source->length ||
            interpreter_size > source->length - interpreter_offset)
            return ASTRA_SYSCALL_INVALID_ARGUMENT;
    }

    if (interpreter_size == 0u)
        return ASTRA_SYSCALL_OK;
    if (interpreter_size > capacity)
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    status = astra_stream_read_exact(
        source->read_at, source->context, source->length,
        interpreter_offset, interpreter_size, &header, NULL);
    if (status != ASTRA_SYSCALL_OK)
        return status;
    if (header[interpreter_size - 1u] != 0u)
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    for (uint32_t index = 0u; index + 1u < interpreter_size; ++index) {
        if (header[index] == 0u)
            return ASTRA_SYSCALL_INVALID_ARGUMENT;
        identity[index] = (char)header[index];
    }
    identity[interpreter_size - 1u] = '\0';
    *identity_length = interpreter_size - 1u;
    return ASTRA_SYSCALL_OK;
}

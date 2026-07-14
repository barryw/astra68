#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define ASTRA_ROM_HEADER_SIZE 32u
#define ASTRA_ROM_MAX_PAYLOAD 0x00040000u

typedef struct {
    uint32_t payload_size;
    uint32_t payload_crc32;
    uint32_t initial_sp;
    uint32_t initial_pc;
} astra_rom_info_t;

bool astra_rom_validate(FILE *file, astra_rom_info_t *info,
                        char *error, size_t error_size);

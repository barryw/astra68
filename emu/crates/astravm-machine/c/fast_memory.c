#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct astra_memory_regions {
    const uint8_t *rom;
    uint32_t rom_base;
    uint32_t rom_size;
    uint8_t *bram;
    uint32_t bram_base;
    uint32_t bram_size;
    uint8_t *sdram;
    uint32_t sdram_base;
    uint32_t sdram_size;
} astra_memory_regions;

static astra_memory_regions regions;

extern unsigned int astravm_bus_read_memory_8(unsigned int address);
extern unsigned int astravm_bus_read_memory_16(unsigned int address);
extern unsigned int astravm_bus_read_memory_32(unsigned int address);
extern void astravm_bus_write_memory_8(unsigned int address,
                                       unsigned int value);
extern void astravm_bus_write_memory_16(unsigned int address,
                                        unsigned int value);
extern void astravm_bus_write_memory_32(unsigned int address,
                                        unsigned int value);
extern void astravm_bus_after_memory_write(void);

void astra_m68k_set_memory_regions(const astra_memory_regions *memory)
{
    regions = *memory;
}

static inline bool contains(uint32_t address, uint32_t base, uint32_t size,
                            uint32_t width, uint32_t *offset)
{
    uint32_t candidate;

    if (address < base || width > size)
        return false;
    candidate = address - base;
    if (candidate > size - width)
        return false;
    *offset = candidate;
    return true;
}

static inline const uint8_t *read_pointer(uint32_t address, uint32_t width)
{
    uint32_t offset;

    if (regions.rom != NULL &&
        (contains(address, 0, regions.rom_size, width, &offset) ||
         contains(address, regions.rom_base, regions.rom_size, width,
                  &offset)))
        return regions.rom + offset;
    if (regions.bram != NULL &&
        contains(address, regions.bram_base, regions.bram_size, width,
                 &offset))
        return regions.bram + offset;
    if (regions.sdram != NULL &&
        contains(address, regions.sdram_base, regions.sdram_size, width,
                 &offset))
        return regions.sdram + offset;
    return NULL;
}

static inline uint8_t *write_pointer(uint32_t address, uint32_t width)
{
    uint32_t offset;

    if (regions.bram != NULL &&
        contains(address, regions.bram_base, regions.bram_size, width,
                 &offset))
        return regions.bram + offset;
    if (regions.sdram != NULL &&
        contains(address, regions.sdram_base, regions.sdram_size, width,
                 &offset))
        return regions.sdram + offset;
    return NULL;
}

static inline bool read_only_pointer(uint32_t address, uint32_t width)
{
    uint32_t offset;

    return regions.rom != NULL &&
           (contains(address, 0, regions.rom_size, width, &offset) ||
            contains(address, regions.rom_base, regions.rom_size, width,
                     &offset));
}

unsigned int m68k_read_memory_8(unsigned int address)
{
    const uint8_t *pointer = read_pointer(address, 1);

    return pointer != NULL ? pointer[0] : astravm_bus_read_memory_8(address);
}

unsigned int m68k_read_memory_16(unsigned int address)
{
    const uint8_t *pointer = read_pointer(address, 2);

    if (pointer == NULL)
        return astravm_bus_read_memory_16(address);
    return ((uint32_t)pointer[0] << 8) | pointer[1];
}

unsigned int m68k_read_memory_32(unsigned int address)
{
    const uint8_t *pointer = read_pointer(address, 4);

    if (pointer == NULL)
        return astravm_bus_read_memory_32(address);
    return ((uint32_t)pointer[0] << 24) | ((uint32_t)pointer[1] << 16) |
           ((uint32_t)pointer[2] << 8) | pointer[3];
}

void m68k_write_memory_8(unsigned int address, unsigned int value)
{
    uint8_t *pointer = write_pointer(address, 1);

    if (pointer != NULL) {
        pointer[0] = (uint8_t)value;
        astravm_bus_after_memory_write();
    } else if (!read_only_pointer(address, 1)) {
        astravm_bus_write_memory_8(address, value);
    }
}

void m68k_write_memory_16(unsigned int address, unsigned int value)
{
    uint8_t *pointer = write_pointer(address, 2);

    if (pointer != NULL) {
        pointer[0] = (uint8_t)(value >> 8);
        pointer[1] = (uint8_t)value;
        astravm_bus_after_memory_write();
    } else if (!read_only_pointer(address, 2)) {
        astravm_bus_write_memory_16(address, value);
    }
}

void m68k_write_memory_32(unsigned int address, unsigned int value)
{
    uint8_t *pointer = write_pointer(address, 4);

    if (pointer != NULL) {
        pointer[0] = (uint8_t)(value >> 24);
        pointer[1] = (uint8_t)(value >> 16);
        pointer[2] = (uint8_t)(value >> 8);
        pointer[3] = (uint8_t)value;
        astravm_bus_after_memory_write();
    } else if (!read_only_pointer(address, 4)) {
        astravm_bus_write_memory_32(address, value);
    }
}

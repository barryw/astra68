#ifndef ASTRA_ENDIAN_H
#define ASTRA_ENDIAN_H

#include <stdint.h>

static inline uint16_t astra_load_be16(const void *source)
{
#if defined(__m68k__)
    uint16_t value;

    __asm__ volatile("move.w (%1),%0"
                     : "=d"(value)
                     : "a"(source)
                     : "memory");
    return value;
#else
    const uint8_t *bytes = source;

    return (uint16_t)((uint16_t)bytes[0] << 8) | bytes[1];
#endif
}

static inline uint32_t astra_load_be32(const void *source)
{
#if defined(__m68k__)
    uint32_t value;

    __asm__ volatile("move.l (%1),%0"
                     : "=d"(value)
                     : "a"(source)
                     : "memory");
    return value;
#else
    const uint8_t *bytes = source;

    return (uint32_t)bytes[0] << 24 | (uint32_t)bytes[1] << 16 |
           (uint32_t)bytes[2] << 8 | bytes[3];
#endif
}

static inline uint64_t astra_load_be64(const void *source)
{
    const uint8_t *bytes = source;

    return (uint64_t)astra_load_be32(bytes) << 32 |
           astra_load_be32(bytes + 4u);
}

static inline void astra_store_be16(void *destination, uint16_t value)
{
#if defined(__m68k__)
    __asm__ volatile("move.w %1,(%0)"
                     :
                     : "a"(destination), "d"(value)
                     : "memory");
#else
    uint8_t *bytes = destination;

    bytes[0] = (uint8_t)(value >> 8);
    bytes[1] = (uint8_t)value;
#endif
}

static inline void astra_store_be32(void *destination, uint32_t value)
{
#if defined(__m68k__)
    __asm__ volatile("move.l %1,(%0)"
                     :
                     : "a"(destination), "d"(value)
                     : "memory");
#else
    uint8_t *bytes = destination;

    bytes[0] = (uint8_t)(value >> 24);
    bytes[1] = (uint8_t)(value >> 16);
    bytes[2] = (uint8_t)(value >> 8);
    bytes[3] = (uint8_t)value;
#endif
}

static inline void astra_store_be64(void *destination, uint64_t value)
{
    uint8_t *bytes = destination;

    astra_store_be32(bytes, (uint32_t)(value >> 32));
    astra_store_be32(bytes + 4u, (uint32_t)value);
}

#endif
